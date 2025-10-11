#ifndef APAGER_H
#define APAGER_H

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PG_SIZE sysconf(_SC_PAGESIZE)
#define STACK_SIZE (1024 * 1024) // 1MB stack size
#define DEBUG 1                  // 0: debugging off, 1: debugging on

#if DEBUG
#define debug(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define debug(fmt, ...)                                                        \
  do {                                                                         \
  } while (0)
#endif

#define CLEANUP_FAIL(fd, ...)                                                  \
  do {                                                                         \
    void *ptrs[] = {__VA_ARGS__};                                              \
    for (size_t i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i++)                \
      if (ptrs[i])                                                             \
        free(ptrs[i]);                                                         \
    if ((fd) >= 0)                                                             \
      close(fd);                                                               \
    return 1;                                                                  \
  } while (0)

typedef struct {
  int argc;
  char **argv;
  int envc;
  char **envp;
  int auxc;
  Elf64_auxv_t *auxv;
  Elf64_Ehdr *ehdr;
  Elf64_Phdr *phdrs;
} loader_info_t;

int validate_ehdr(int fd, Elf64_Ehdr *ehdr);
Elf64_Phdr *load_phdrs(int fd, Elf64_Ehdr *ehdr);
int check_overlap(Elf64_Phdr *phdrs, int phnum);
int map_segments(int fd, Elf64_Phdr *phdrs, int phnum);
void *alloc_stack(void);
void *setup_stack(void *stack_top, const loader_info_t *info);
__attribute__((noreturn)) void run_exec(void *entry_point, void *stack_pointer);

/**
 * get the mmap protection flags from the program header flags (helper)
 */
int get_mmap_prot(Elf64_Word p_flags) {
  int prot = 0;
  if (p_flags & PF_R)
    prot |= PROT_READ;
  if (p_flags & PF_W)
    prot |= PROT_WRITE;
  if (p_flags & PF_X) {
    debug("X flag found in program header, setting PROT_EXEC\n");
    prot |= PROT_EXEC;
  }
  return prot;
}

#endif // APAGER_H