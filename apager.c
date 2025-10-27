// all at once loader
#include "apager.h"
#include "stack_check.c"

/*
musl-gcc -static -Wl,-Ttext-segment=0x08048000 -o apager apager.c
musl-gcc -static -no-pie -o ./tests/simple ./tests/simple.c
musl-gcc -static -o ./tests/dyn_test ./tests/dyn_test.c

(sudo) ./apager /bin/ls


gdb --args ./apager ./tests/simple
bt
info registers rsp
info frame
x/i $pc

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

  const char *interp_path = get_interpreter_path(fd, phdrs, ehdr.e_phnum);
  void *interp_entry_point = NULL;

  if (interp_path != NULL) {
    debug("opening interpreter: %s\n", interp_path);
    int interp_fd = open(interp_path, O_RDONLY);
    if (interp_fd < 0) {
        perror("could not open interpreter\n");
        free((void*)interp_path);
        CLEANUP_FAIL(fd, phdrs); 
    }

    Elf64_Ehdr interp_ehdr;
    if (validate_ehdr(interp_fd, &interp_ehdr) < 0) {
        fprintf(stderr, "interpreter is not a valid ELF file\n");
        free((void*)interp_path);
        close(interp_fd);
        CLEANUP_FAIL(fd, phdrs);
    }

    Elf64_Phdr *interp_phdrs = load_phdrs(interp_fd, &interp_ehdr);
    if (interp_phdrs == NULL) {
        free((void*)interp_path);
        close(interp_fd);
        CLEANUP_FAIL(fd, phdrs);
    }

    void *interp_base_addr = map_dynamic_segments(interp_fd, interp_phdrs, interp_ehdr.e_phnum);
    if (interp_base_addr == NULL) {
        fprintf(stderr, "failed to map interpreter\n");
        free((void*)interp_path);
        close(interp_fd);
        free(interp_phdrs);
        CLEANUP_FAIL(fd, phdrs);
    }

    // real entry point is the base address + the header's entry offset
    interp_entry_point = (void *)((Elf64_Addr)interp_base_addr + interp_ehdr.e_entry);

    debug("Interpreter loaded. Base: %p, Entry: %p\n", 
          interp_base_addr, interp_entry_point);
    
    free(interp_phdrs);
    close(interp_fd);
    free((void*)interp_path);

  } else { // static
    if (map_segments(fd, phdrs, ehdr.e_phnum) < 0)
      CLEANUP_FAIL(fd, phdrs);
    
    interp_entry_point = (void*)ehdr.e_entry; // entry point = program entry point
  }

  if (check_overlap(phdrs, ehdr.e_phnum) < 0)
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
 * search for a PT_INTERP segment in the program headers
 * if exists read the interpreter's path string from the file
 * fd: file descriptor of the executable
 * phdrs: pointer to the program headers of the executable
 * phnum: number of program headers
 * return a (malloc'd) string containing the interpreter path,
 * or NULL if not found (static executable)
 */
const char *get_interpreter_path(int fd, Elf64_Phdr *phdrs, int phnum) {
    for (int i = 0; i < phnum; i++) {
        if (phdrs[i].p_type == PT_INTERP) {
            size_t path_size = phdrs[i].p_filesz;
            char *path = malloc(path_size + 1);
            if (path == NULL) {
                perror("malloc for interpreter path");
                return NULL;
            }

            if (lseek(fd, phdrs[i].p_offset, SEEK_SET) != phdrs[i].p_offset) {
                perror("could not lseek to PT_INTERP\n");
                free(path);
                return NULL;
            }
            
            if (read(fd, path, path_size) != path_size) {
                perror("could not read PT_INTERP\n");
                free(path);
                return NULL;
            }
            
            path[path_size] = '\0';
            
            debug("found interpreter: %s\n", path);
            return path;
        }
    }

    debug("no interpreter found, assuming static executable\n");
    return NULL;
}


/**
 * maps the loadable segments of a dynamic (ET_DYN) executable and
 * lets the kernel choose the base address
 * fd: file descriptor of the executable
 * phdrs: pointer to the program headers
 * phnum: number of program headers
 * returns base address where the executable was loaded, or NULL on failure
 */
void *map_dynamic_segments(int fd, Elf64_Phdr *phdrs, int phnum) {
    void *base_addr = NULL;
    Elf64_Addr load_bias = 0;

    for (int i = 0; i < phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        int prot = get_mmap_prot(phdr->p_flags);

        Elf64_Addr vaddr = phdr->p_vaddr;
        Elf64_Addr map_addr_aligned = vaddr & ~(PG_SIZE - 1);
        Elf64_Addr page_offset = vaddr - map_addr_aligned;
        size_t map_size_aligned = phdr->p_memsz + page_offset;

        void *map_target_addr;
        int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;

        if (base_addr == NULL) { // first segment let kernel choose base
            map_target_addr = NULL;
        } else { // map relative to base
            map_flags |= MAP_FIXED;
            map_target_addr = (void *)(load_bias + map_addr_aligned);
        }

        void *mapped_region =
            mmap(map_target_addr, map_size_aligned, PROT_READ | PROT_WRITE,
                 map_flags, -1, 0);

        if (mapped_region == MAP_FAILED) {
            perror("dyn_map: mmap failed\n");
            return NULL;
        }

        if (base_addr == NULL) { // save base address and load bias
            base_addr = mapped_region;
            load_bias = (Elf64_Addr)base_addr - map_addr_aligned;
            debug("interpreter mapped at base: %p (load_bias: 0x%lx)\n",
                  base_addr, load_bias);
        }

        if (phdr->p_filesz > 0) {
            if (lseek(fd, phdr->p_offset, SEEK_SET) != phdr->p_offset) {
                perror("dyn_map: lseek\n"); return NULL;
            }

            void *read_dest = (void *)(load_bias + phdr->p_vaddr);
            if (read(fd, read_dest, phdr->p_filesz) != phdr->p_filesz) {
                perror("dyn_map: read\n"); return NULL;
            }
        }

        void *protect_addr = (void *)(load_bias + map_addr_aligned);
        if (mprotect(protect_addr, map_size_aligned, prot) != 0) {
            perror("dyn_map: mprotect\n"); return NULL;
        }

        fprintf(stderr,
                "mapped interp segment: base_addr=%-#12lx file_offset=%-#12lx "
                "size=%-#12lx\n\n",
                (Elf64_Addr)protect_addr, phdr->p_offset, map_size_aligned);
    }

    return base_addr;
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

    int prot = get_mmap_prot(phdr->p_flags);
    void *map_start = (void *)phdr->p_vaddr;
    size_t map_size = phdr->p_memsz;

    Elf64_Addr vaddr = phdr->p_vaddr;
    Elf64_Addr map_addr_aligned = vaddr & ~(PG_SIZE - 1);
    Elf64_Addr page_offset = vaddr - map_addr_aligned;
    size_t map_size_aligned = map_size + page_offset;

    void *mapped_region =
        mmap((void *)map_addr_aligned, map_size_aligned, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

    if (mapped_region == MAP_FAILED) {
      perror("could not reserve memory for segment :-|\n");
      return -1;
    }

    if (phdr->p_filesz > 0) { // read segment content from file (otherwise zeroed -> bss)
      if (lseek(fd, phdr->p_offset, SEEK_SET) != phdr->p_offset) {
        perror("could not seek to segment file offset :-|\n");
        return -1;
      }

      ssize_t bytes_read = read(fd, (void *)phdr->p_vaddr, phdr->p_filesz);
      if (bytes_read != phdr->p_filesz) {
        perror("could not read segment content :-|\n");
        return -1;
      }
    }

    if (mprotect(mapped_region, map_size_aligned, prot) != 0) {
      perror("could not set final memory permissions :-|\n");
      return -1;
    }

    fprintf(stderr,
            "mapped segment: base_addr=%-#12lx file_offset=%-#12lx "
            "size=%-#12lx\n\n",
            vaddr, phdr->p_offset, map_size);
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