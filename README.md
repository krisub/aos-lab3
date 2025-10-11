- musl-tools
- to compile C file with musl-gcc:
  - musl-gcc -static -o apager apager.c
  (compiles statically)

"apager" is all at once loader


https://elixir.bootlin.com/linux/v4.8/source/fs/binfmt_elf.c
- load_elf_binary is on l668.

kernel creates memory image for a new process by:
1. open executable (obviously lol)
2. read elf header to check its valid
3. maps each loadable segment (PT_LOAD) into memory with perms (rwe)
  a. have to page align mappings
4. set up bss
5. create + setup stack (argc, argv, env, + auxiliary vectors( which are what?))
  a. mmap MAP_ANON for new stack
6. pass control to new program at entry point (address in elf header)
  a. but need to zero GPRs!!!! libc init code will mess up if there is values in the registers
so this is basically gheithos syscall lab except for adding env and auxiliary vectors to stack

main header is Elf64_Ehdr
and program headers are Elf64_Phdr
readelf -l <executable> lets me see headers of executable
