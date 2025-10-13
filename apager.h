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
#define DEBUG 0                  // 0: debugging off, 1: debugging on
/*#define MAX_AUX 128 // uncomment to use print_auxv */

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

/* uncomment to use print_auxv
typedef struct {
  unsigned long type;
  char name[64];
} aux_def_t;
*/

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
    debug("R flag found in program header, setting PROT_READ\n");
    prot |= PROT_READ;
  if (p_flags & PF_W)
    debug("W flag found in program header, setting PROT_WRITE\n");
    prot |= PROT_WRITE;
  if (p_flags & PF_X) {
    debug("X flag found in program header, setting PROT_EXEC\n");
    prot |= PROT_EXEC;
  }
  return prot;
}

/* uncomment to use print_auxv 

static int load_aux_defs(aux_def_t *defs, size_t max_defs) {
  const char *paths[] = {"/usr/include/linux/auxvec.h",
                         "/usr/include/asm/auxvec.h"};
  size_t count = 0;

  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    FILE *f = fopen(paths[i], "r");
    if (!f)
      continue;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "#define AT_", 10) != 0)
        continue;

      char name[64];
      unsigned long value;
      if (sscanf(line, "#define %63s %lu", name, &value) == 2) {
        if (count < max_defs) {
          defs[count].type = value;
          strncpy(defs[count].name, name, sizeof(defs[count].name) - 1);
          defs[count].name[sizeof(defs[count].name) - 1] = '\0';
          count++;
        }
      }
    }
    fclose(f);
  }
  return (int)count;
}

static const char *find_aux_name(aux_def_t *defs, int count,
                                 unsigned long type) {
  for (int i = 0; i < count; i++)
    if (defs[i].type == type)
      return defs[i].name;

  if (type >= 18 && type <= 22)
    return "AT_RESERVED";

  switch (type) {
  case 32:
    return "AT_SYSINFO";
  case 33:
    return "AT_SYSINFO_EHDR";
  case 34:
    return "AT_L1I_CACHESHAPE";
  case 35:
    return "AT_L1D_CACHESHAPE";
  case 36:
    return "AT_L2_CACHESHAPE";
  case 37:
    return "AT_L3_CACHESHAPE";
  case 38:
    return "AT_L1I_CACHESIZE";
  case 39:
    return "AT_L1D_CACHESIZE";
  case 40:
    return "AT_L2_CACHESIZE";
  case 41:
    return "AT_L3_CACHESIZE";
  case 42:
    return "AT_L1I_CACHEGEOMETRY";
  case 43:
    return "AT_L1D_CACHEGEOMETRY";
  case 44:
    return "AT_L2_CACHEGEOMETRY";
  case 45:
    return "AT_L3_CACHEGEOMETRY";
  case 46:
    return "AT_MINSIGSTKSZ"; // sometimes older distros miss this one
  case 47:
    return "AT_PRESERVE_ARGV0"; // newer kernel hint
  case 48:
    return "AT_EXECFN2"; // rarely used variant
  case 49:
    return "AT_RSEQ_FEATURE_SIZE";
  case 50:
    return "AT_RSEQ_ALIGN";
  case 51:
    return "AT_MINSIGSTKSZ"; // defined in modern glibc headers
  default:
    return "UNKNOWN";
  }
}

static void print_auxv(const Elf64_auxv_t *auxv) {
  aux_def_t defs[MAX_AUX];
  int def_count = load_aux_defs(defs, MAX_AUX);

  debug("\n=== Auxiliary Vector ===\n");

  for (const Elf64_auxv_t *a = auxv; a->a_type != AT_NULL; a++) {
    const char *name = find_aux_name(defs, def_count, a->a_type);

    debug("Type: %-18s (%-3lu)  ", name, a->a_type);

    switch (a->a_type) {
    case AT_EXECFN:
    case AT_PLATFORM:
    case AT_BASE_PLATFORM:
      debug("Value: %s\n", (char *)a->a_un.a_val);
      break;
    case AT_RANDOM:
      debug("Value: %p (16 random bytes)\n", (void *)a->a_un.a_val);
      break;
    default:
      debug("Value: 0x%lx\n", a->a_un.a_val);
      break;
    }
  }
  debug("========================\n\n");
}
*/

#endif // APAGER_H