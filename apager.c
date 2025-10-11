// all at once loader
#include "apager.h"
#include "stack_check.c"

/*
musl-gcc -static -Wl,-Ttext-segment=0x08048000 -o apager apager.c
musl-gcc -static -no-pie -o simple simple.c
musl-gcc -static -o dyn_test dyn_test.c

(sudo) ./apager /bin/ls

*/

int main(int argc, char *argv[], char *envp[]) {

  if (argc != 2) {
    fprintf(stderr, "usage: %s <executable>\n", argv[0]);
    return 1;
  }

  int envc = 0;
  for (envc = 0; envp[envc] != NULL; envc++)
    ;

  Elf64_auxv_t *auxv = (Elf64_auxv_t *)(envp + envc + 1);
  int auxc = 0;
  for (auxc = 0; auxv[auxc].a_type != AT_NULL; auxc++)
    ;

  loader_info_t info = {
      .argc = argc - 1, // don't count the loader itself
      .argv = argv + 1,
      .envc = envc,
      .envp = envp,
      .auxc = auxc,
      .auxv = auxv,
  };

  const char *exec_path = argv[1];
  debug("loading executable: %s\n", exec_path);

  // open the executable file
  int fd = open(exec_path, O_RDONLY);
  if (fd < 0) {
    perror("could not open executable :(\n");
    return 1;
  }

  Elf64_Ehdr ehdr;
  if (validate_ehdr(fd, &ehdr) < 0)
    CLEANUP_FAIL(fd);
  debug("executable loaded! entry point address: 0x%lx\n", ehdr.e_entry);

  Elf64_Phdr *phdrs = load_phdrs(fd, &ehdr);
  if (phdrs == NULL)
    CLEANUP_FAIL(fd);

  if (check_overlap(phdrs, ehdr.e_phnum) < 0)
    CLEANUP_FAIL(fd, phdrs);

  if (map_segments(fd, phdrs, ehdr.e_phnum) < 0)
    CLEANUP_FAIL(fd, phdrs);

  void *stack_top = alloc_stack();
  if (stack_top == NULL)
    CLEANUP_FAIL(fd, phdrs);

  info.ehdr = &ehdr;
  info.phdrs = phdrs;
  void *stack_pointer = setup_stack(stack_top, &info);
  if (stack_pointer == NULL)
    CLEANUP_FAIL(fd, phdrs, stack_top);
  stack_check(stack_pointer, info.argc,
              info.argv); // function from stack_check.c (given)

  close(fd);

  debug("transferring control to entry point %p with stack at %p",
        (void *)ehdr.e_entry, stack_pointer);

  run_exec((void *)ehdr.e_entry, stack_pointer);

  // should never get here
  fprintf(stderr, "error: returned from child program :(\n");
  return 0;
}

// below this is all the helper functions

/**
 * validate the ELF header of the executable
 * fd: file descriptor of the executable
 * ehdr: pointer to store the ELF header
 * return 0 if valid, -1 if not valid
 */
int validate_ehdr(int fd, Elf64_Ehdr *ehdr) {

  if (read(fd, ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
    perror("could not read ELF header :[\n");
    return -1;
  }

  if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
      ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
    fprintf(stderr, "wrong ELF magic number :[\n");
    return -1;
  }

  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    fprintf(stderr, "not a 64-bit executable :[\n");
    return -1;
  }

  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    fprintf(stderr, "not an (PI)executable :[\n");
    return -1;
  }

  return 0;
}

/**
 * load the program headers of the executable
 * fd: file descriptor of the executable
 * ehdr: pointer to the ELF header of the executable
 * return pointer to the program headers if successful, NULL if not
 */
Elf64_Phdr *load_phdrs(int fd, Elf64_Ehdr *ehdr) {
  size_t ph_table_size = ehdr->e_phnum * ehdr->e_phentsize;
  if (ph_table_size == 0) {
    fprintf(stderr, "no program headers found... :{\n");
    return NULL;
  }

  Elf64_Phdr *phdrs = malloc(ph_table_size);
  if (phdrs == NULL) {
    perror("could not malloc for phdrs... :{\n");
    return NULL;
  }

  if (lseek(fd, ehdr->e_phoff, SEEK_SET) != ehdr->e_phoff) {
    perror("could not seek to program headers... :{\n");
    free(phdrs);
    return NULL;
  }

  if (read(fd, phdrs, ph_table_size) != ph_table_size) {
    perror("could not read program headers... :{\n");
    free(phdrs);
    return NULL;
  }

  return phdrs;
}

/**
 * check if the target executable overlaps with the loader in memory
 * phdrs: pointer to the program headers of the executable
 * phnum: number of program headers
 * return 0 if no overlap, -1 if overlap
 */
int check_overlap(Elf64_Phdr *phdrs, int phnum) {
  // start and end of loader in memory
  extern char _start[], _end[];

  void *loader_start = (void *)_start;
  void *loader_end = (void *)_end;

  debug("loader memory range: %p - %p", loader_start, loader_end);

  for (int i = 0; i < phnum; i++) {
    Elf64_Phdr *phdr = &phdrs[i];

    if (phdr->p_type == PT_LOAD) {
      void *target_start = (void *)phdr->p_vaddr;
      void *target_end = (void *)(phdr->p_vaddr + phdr->p_memsz);

      if (loader_start < target_end && target_start < loader_end) {
        fprintf(stderr, "target executable overlaps with loader memory!!\n");
        fprintf(stderr, "loader: %p-%p, target seegment: %p-%p\n", loader_start,
                loader_end, target_start, target_end);
        return -1;
      }
    }
  }

  return 0; // no overlap
}

/**
 * map the loadable segments of the executable into memory
 * fd: file descriptor of the executable
 * phdrs: pointer to the program headers of the executable
 * phnum: number of program headers
 * return 0 if successful, -1 if not
 */
int map_segments(int fd, Elf64_Phdr *phdrs, int phnum) {

  for (int i = 0; i < phnum; i++) {
    Elf64_Phdr *phdr = &phdrs[i];

    if (phdr->p_type != PT_LOAD) {
      continue;
    }

    Elf64_Addr vaddr = phdr->p_vaddr;
    Elf64_Addr map_addr = vaddr & ~(PG_SIZE - 1); // page aligned address
    Elf64_Addr page_offset = vaddr - map_addr;

    size_t map_size = phdr->p_memsz + page_offset;
    off_t file_offset = phdr->p_offset & ~(PG_SIZE - 1);
    int prot = get_mmap_prot(phdr->p_flags);

    void *mapped = mmap((void *)map_addr, map_size, prot,
                        MAP_PRIVATE | MAP_FIXED, fd, file_offset);
    if (mapped == MAP_FAILED) {
      debug("mmap failed: addr=0x%lx, size=0x%lx, prot=0x%x, offset=0x%lx\n",
            map_addr, map_size, prot, file_offset);
      perror("mmap failed :-|\n");
      return -1;
    }

    fprintf(stderr,
            "mapped segment: base_addr=%-#12lx  file_offset=%-#12lx  "
            "size=%-#12lx\n",
            map_addr, file_offset, map_size);

    /*
    BSS segment is the segment that is larger in memory than in file (p_memsz >
    p_filesz). but mmap automatically fills the extra space with zeros, so i
    don't need to handle it explicitly.
    */
  }

  return 0;
}

/**
 * allocate a stack for the child program
 * return pointer to the top of the stack if successful, NULL if not
 */
void *alloc_stack(void) {
  void *stack_bottom = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (stack_bottom == MAP_FAILED) {
    perror("could not mmap stack :-0\n");
    return NULL;
  }

  // stack grows down
  void *stack_top = stack_bottom + STACK_SIZE;
  return stack_top;
}

/**
 * setup the stack for the child program
 * stack_top: pointer to the top of the stack
 * info: pointer to the loader_info_t struct containing argc, argv, envp, auxv
 * return pointer to the new stack pointer if successful, NULL if not
 */
void *setup_stack(void *stack_top, const loader_info_t *info) {
  char *sp = (char *)stack_top;
  char *copy_argv[info->argc + 1];
  char *copy_envp[info->envc + 1];

  // argv -> stack
  for (int i = 0; i < info->argc; i++) {
    size_t len = strlen(info->argv[i]) + 1;
    sp -= len;
    copy_argv[i] = sp;
    memcpy(sp, info->argv[i], len);
  }
  copy_argv[info->argc] = NULL;

  // envp -> stack
  for (int i = 0; i < info->envc; i++) {
    size_t len = strlen(info->envp[i]) + 1;
    sp -= len;
    copy_envp[i] = sp;
    memcpy(sp, info->envp[i], len);
  }
  copy_envp[info->envc] = NULL;

  sp = (char *)((uintptr_t)sp & ~(uintptr_t)0xF); // align to 16 bytes

  Elf64_Addr phdr_addr = 0;
  for (int i = 0; i < info->ehdr->e_phnum; i++) {
    if (info->phdrs[i].p_type == PT_PHDR) {
      phdr_addr = info->phdrs[i].p_vaddr;
      break;
    }
  }

  Elf64_auxv_t copy_auxv[info->auxc + 1];
  memcpy(copy_auxv, info->auxv, sizeof(Elf64_auxv_t) * (info->auxc + 1));

  for (int i = 0; i < info->auxc; i++) {
    switch (copy_auxv[i].a_type) {
    case AT_PHDR: // ptr to program headers
      copy_auxv[i].a_un.a_val = phdr_addr;
      break;
    case AT_PHENT: // size of program header entry
      copy_auxv[i].a_un.a_val = info->ehdr->e_phentsize;
      break;
    case AT_PHNUM: // number of program headers
      copy_auxv[i].a_un.a_val = info->ehdr->e_phnum;
      break;
    case AT_ENTRY: // entry point of the program
      copy_auxv[i].a_un.a_val = info->ehdr->e_entry;
      break;
    }
  }

  size_t auxv_size = sizeof(Elf64_auxv_t) * (info->auxc + 1);
  sp -= auxv_size;
  memcpy(sp, copy_auxv, auxv_size);

  size_t envp_size = sizeof(char *) * (info->envc + 1);
  sp -= envp_size;
  memcpy(sp, copy_envp, envp_size);

  size_t argv_size = sizeof(char *) * (info->argc + 1);
  sp -= argv_size;
  memcpy(sp, copy_argv, argv_size);

  sp -= sizeof(uint64_t);
  *(uint64_t *)sp = (uint64_t)info->argc;

  return sp;
}

/**
 * transfer control to the child program
 * entry_point: address of the entry point of the child program
 * stack_pointer: pointer to the top of the stack for the child program
 * does not return
 */
__attribute__((noreturn)) void run_exec(void *entry_point,
                                        void *stack_pointer) {
  __asm__ volatile(
      // stack pointer
      "mov %0, %%rsp\n"
      "and $-16, %%rsp\n" // align down to 16 bytes
      "sub $8, %%rsp\n"   // reserve space so push makes RSP % 16 == 8

      // entry point
      "push %1\n"

      // zero gprs
      "xor %%rax, %%rax\n"
      "xor %%rbx, %%rbx\n"
      "xor %%rcx, %%rcx\n"
      "xor %%rdx, %%rdx\n"
      "xor %%rsi, %%rsi\n"
      "xor %%rdi, %%rdi\n"
      "xor %%rbp, %%rbp\n"
      "xor %%r8, %%r8\n"
      "xor %%r9, %%r9\n"
      "xor %%r10, %%r10\n"
      "xor %%r11, %%r11\n"
      "xor %%r12, %%r12\n"
      "xor %%r13, %%r13\n"
      "xor %%r14, %%r14\n"
      "xor %%r15, %%r15\n"

      // use ret to jump to entry point
      "ret\n"
      :                                      // no output
      : "r"(stack_pointer), "r"(entry_point) // input
      : "memory"                             // registers we just overwrote
  );
  __builtin_unreachable(); // should never get here
}