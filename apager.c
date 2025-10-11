// all at once loader
#include "apager.h"

/*
musl-gcc -static -o apager apager.c
(sudo) ./apager /bin/ls
*/

int main(int argc, char *argv[]) {
  // should only pass in the executable path
  if (argc != 2) {
    fprintf(stderr, "usage: %s <executable>\n", argv[0]);
    return 1;
  }

  const char *exec_path = argv[1];
  debug("loading executable: %s\n", exec_path);

  // open the executable file
  int fd = open(exec_path, O_RDONLY);
  if (fd < 0) {
    perror("could not open executable :(\n");
    return 1;
  }

  Elf64_Ehdr ehdr;
  if (validate_ehdr(fd, &ehdr) < 0) {
    close(fd);
    return 1;
  }
  debug("executable loaded! entry point address: 0x%lx\n", ehdr.e_entry);

  Elf64_Phdr *phdrs = load_phdrs(fd, &ehdr);
  if (phdrs == NULL) {
    close(fd);
    return 1;
  }

  if (map_segments(fd, phdrs, ehdr.e_phnum) < 0) {
    free(phdrs);
    close(fd);
    return 1;
  }

  close(fd);
  return 0;
}

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

    fprintf(
        stderr,
        "mapped segment: base_addr=%-#12lx  file_offset=%-#12lx  size=%-#12lx\n",
        map_addr, file_offset, map_size);

    /*
    BSS segment is the segment that is larger in memory than in file (p_memsz >
    p_filesz). but mmap automatically fills the extra space with zeros, so i
    don't need to handle it explicitly.
    */
  }

  return 0;
}