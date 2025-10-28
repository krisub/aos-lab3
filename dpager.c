// demand loader
#define _GNU_SOURCE
#include "apager.h"
#include "stack_check.c"
#include <signal.h>

/*
musl-gcc -static -Wl,-Ttext-segment=0x08048000 -o apager apager.c
musl-gcc -static -no-pie -o ./tests/simple ./tests/simple.c
musl-gcc -static -o ./tests/dyn_test ./tests/dyn_test.c

(sudo) ./apager /bin/ls


gdb --args ./apager ./tests/simple
handle SIGSEGV nostop noprint pass
bt
info registers rsp
info frame
x/i $pc

*/

static Elf64_Phdr *g_phdrs;
static int g_phnum;
static int g_fd;
int map_segment(int fd, Elf64_Ehdr *ehdr, Elf64_Phdr *phdrs, int phnum);
static void page_fault_handler(int signum, siginfo_t *si, void *ucontext);

int main(int argc, char *argv[], char *envp[]) {

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

  // print_auxv(auxv); // print the auxiliary vector

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

  g_phdrs = phdrs;
  g_phnum = ehdr.e_phnum;
  g_fd = fd;

  if (check_overlap(phdrs, ehdr.e_phnum) < 0)
    CLEANUP_FAIL(fd, phdrs);

  if (map_segment(fd, &ehdr, phdrs, ehdr.e_phnum) < 0)
    CLEANUP_FAIL(fd, phdrs);

  void *stack_top = alloc_stack();
  if (stack_top == NULL)
    CLEANUP_FAIL(fd, phdrs);

  info.ehdr = &ehdr;
  info.phdrs = phdrs;
  void *stack_pointer = setup_stack(stack_top, &info);
  if (stack_pointer == NULL)
    CLEANUP_FAIL(fd, phdrs, stack_top);

  if (RUN_STACK_CHECK) {
    stack_check(stack_pointer, info.argc,
                info.argv); // function from stack_check.c (given)
  }

  struct sigaction sa;
  sa.sa_sigaction = page_fault_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction failed");
    return 1;
  }

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
 * map the page containing the entry point of the executable into memory
 * fd: file descriptor of the executable
 * ehdr: pointer to the ELF header of the executable
 * phdrs: pointer to the program headers of the executable
 * phnum: number of program headers
 * return 0 if successful, -1 if not
 */
int map_segment(int fd, Elf64_Ehdr *ehdr, Elf64_Phdr *phdrs, int phnum) {
  void *entry_point = (void *)ehdr->e_entry;
  debug("program entry point: %p", entry_point);

  // find the PT_LOAD segment containing the entry point
  Elf64_Phdr *entry_phdr = NULL;
  for (int i = 0; i < phnum; i++) {
    Elf64_Phdr *phdr = &phdrs[i];
    if (phdr->p_type == PT_LOAD) {
      void *seg_start = (void *)phdr->p_vaddr;
      void *seg_end = (void *)(phdr->p_vaddr + phdr->p_memsz);
      if (entry_point >= seg_start && entry_point < seg_end) {
        entry_phdr = phdr;
        break;
      }
    }
  }

  if (entry_phdr == NULL) {
    fprintf(stderr, "could not find a loadable segment for the entry point.\n");
    return -1;
  }

  Elf64_Addr entry_page_start = (Elf64_Addr)entry_point & ~(PG_SIZE - 1);
  debug("page containing entry point starts at: %p", (void *)entry_page_start);

  // map just the page containing the entry point
  int prot = get_mmap_prot(entry_phdr->p_flags);
  void *mapped_region =
      mmap((void *)entry_page_start, PG_SIZE, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

  if (mapped_region == MAP_FAILED) {
    perror("could not map entry point page :-|\n");
    return -1;
  }

  off_t file_offset = 0;
  size_t bytes_to_read = 0;

  if (entry_page_start < (entry_phdr->p_vaddr + entry_phdr->p_filesz)) {
    file_offset =
        entry_phdr->p_offset + (entry_page_start - entry_phdr->p_vaddr);

    Elf64_Addr seg_file_end = entry_phdr->p_vaddr + entry_phdr->p_filesz;
    Elf64_Addr page_end = entry_page_start + PG_SIZE;

    if (page_end > seg_file_end) {
      bytes_to_read = seg_file_end - entry_page_start;
    } else {
      bytes_to_read = PG_SIZE;
    }

    if (lseek(fd, file_offset, SEEK_SET) != file_offset) {
      perror("could not seek to entry point page content in file :-|\n");
      return -1;
    }

    ssize_t bytes_read = read(fd, (void *)entry_page_start, bytes_to_read);
    if (bytes_read < 0) {
      perror("could not read entry point page content :-|\n");
      return -1;
    }
  }

  if (mprotect(mapped_region, PG_SIZE, prot) != 0) {
    perror("could not set final memory permissions for entry page :-|\n");
    return -1;
  }

  if (PRINT_MAP) {
    fprintf(stdout,
            "dpager: mapped entry page: base_addr=%-#12lx file_offset=%-#12lx "
            "size=%-#12lx\n\n",
            entry_page_start, file_offset, PG_SIZE);
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
  Elf64_auxv_t copy_auxv[info->auxc + 1];
  char *child_exec_path = NULL;

  // argv -> stack
  for (int i = 0; i < info->argc; i++) {
    size_t len = strlen(info->argv[i]) + 1;
    sp -= len;
    copy_argv[i] = sp;
    memcpy(sp, info->argv[i], len);
  }
  copy_argv[info->argc] = NULL;
  child_exec_path = copy_argv[0];

  // envp -> stack
  for (int i = 0; i < info->envc; i++) {
    size_t len = strlen(info->envp[i]) + 1;
    sp -= len;
    copy_envp[i] = sp;
    memcpy(sp, info->envp[i], len);
  }
  copy_envp[info->envc] = NULL;

  Elf64_Addr phdr_addr = 0;
  for (int i = 0; i < info->ehdr->e_phnum; i++) {
    Elf64_Phdr *phdr = &info->phdrs[i];
    if (phdr->p_type == PT_LOAD && info->ehdr->e_phoff >= phdr->p_offset &&
        info->ehdr->e_phoff < (phdr->p_offset + phdr->p_filesz)) {
      phdr_addr = phdr->p_vaddr + (info->ehdr->e_phoff - phdr->p_offset);
      debug("found program headers in PT_LOAD segment %d at address %p\n", i,
            (void *)phdr_addr);
      break;
    }
  }

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
    case AT_EXECFN: // filename of the program
      copy_auxv[i].a_un.a_val = (Elf64_Addr)child_exec_path;
      break;
    }
  }

  size_t auxv_size = sizeof(Elf64_auxv_t) * (info->auxc + 1);
  size_t envp_size = sizeof(char *) * (info->envc + 1);
  size_t argv_size = sizeof(char *) * (info->argc + 1);
  size_t total_size = auxv_size + envp_size + argv_size + sizeof(uint64_t);

  sp = (char *)((uintptr_t)(sp - total_size) & ~(uintptr_t)0xF);

  char *base = sp;

  *(uint64_t *)base = (uint64_t)info->argc;
  base += sizeof(uint64_t);

  memcpy(base, copy_argv, argv_size);
  base += argv_size;

  memcpy(base, copy_envp, envp_size);
  base += envp_size;

  memcpy(base, copy_auxv, auxv_size);

  // print_auxv(copy_auxv);
  debug("stack setup complete! new stack pointer: %p\n", sp);

  return sp;
}

/**
 * signal handler for SIGSEGV to handle page faults
 * signum: signal number
 * si: siginfo_t struct containing info about the signal
 * ucontext: pointer to the ucontext_t struct containing the context
 */
static void page_fault_handler(int signum, siginfo_t *si, void *ucontext) {
  void *fault_addr = si->si_addr;
  debug("caught SIGSEGV at fault address: %p", fault_addr);

  Elf64_Phdr *fault_phdr = NULL;
  for (int i = 0; i < g_phnum; i++) {
    Elf64_Phdr *phdr = &g_phdrs[i];
    if (phdr->p_type == PT_LOAD) {
      void *seg_start = (void *)phdr->p_vaddr;
      void *seg_end = (void *)(phdr->p_vaddr + phdr->p_memsz);
      if (fault_addr >= seg_start && fault_addr < seg_end) {
        fault_phdr = phdr;
        break;
      }
    }
  }

  if (fault_phdr == NULL) {
    fprintf(stderr,
            "memory access error: address %p is not in a valid segment.\n",
            fault_addr);
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
    return;
  }

  Elf64_Addr page_start = (Elf64_Addr)fault_addr & ~(PG_SIZE - 1);

  int prot = get_mmap_prot(fault_phdr->p_flags);
  void *mapped_region =
      mmap((void *)page_start, PG_SIZE, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

  if (mapped_region == MAP_FAILED) {
    perror("could not map faulting page :-(\n");
    exit(1);
  }

  size_t bytes_to_read = 0;

  if (page_start < (fault_phdr->p_vaddr + fault_phdr->p_filesz)) {
    off_t file_offset =
        fault_phdr->p_offset + (page_start - fault_phdr->p_vaddr);

    Elf64_Addr seg_file_end = fault_phdr->p_vaddr + fault_phdr->p_filesz;
    Elf64_Addr page_end = page_start + PG_SIZE;

    if (page_end > seg_file_end) {
      bytes_to_read = seg_file_end - page_start;
    } else {
      bytes_to_read = PG_SIZE;
    }

    if (lseek(g_fd, file_offset, SEEK_SET) != file_offset) {
      perror("could not seek to page content in file\n");
      exit(1);
    }
    if (read(g_fd, (void *)page_start, bytes_to_read) < 0) {
      perror("could not read page content from file\n");
      exit(1);
    }
  }

  if (mprotect(mapped_region, PG_SIZE, prot) != 0) {
    perror("could not set final memory permissions for fault page\n");
    exit(1);
  }

  if (PRINT_MAP) { 
    fprintf(stdout, "handled page fault: mapped page at base_addr=%-#12lx\n", page_start);
  }
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
