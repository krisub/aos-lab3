#ifndef APAGER_H
#define APAGER_H

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PG_SIZE sysconf(_SC_PAGESIZE)
#define DEBUG 1 // 0: debugging off, 1: debugging on

#if DEBUG
#define debug(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define debug(fmt, ...)                                                        \
  do {                                                                         \
  } while (0)
#endif

int validate_ehdr(int fd, Elf64_Ehdr *ehdr);
Elf64_Phdr *load_phdrs(int fd, Elf64_Ehdr *ehdr);
int map_segments(int fd, Elf64_Phdr *phdrs, int phnum);

/**
 * get the mmap protection flags from the program header flags (helper)
 */
int get_mmap_prot(Elf64_Word p_flags) {
  int prot = 0;
  if (p_flags & PF_R)
    prot |= PROT_READ;
  if (p_flags & PF_W)
    prot |= PROT_WRITE;
  if (p_flags & PF_X)
    prot |= PROT_EXEC;
  return prot;
}

#endif // APAGER_H