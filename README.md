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
  - and in userspace

main header is Elf64_Ehdr
and program headers are Elf64_Phdr
readelf -l <executable> lets me see headers of executable


gcc -o super_simple ./tests/super_simple.c
LD_DEBUG=symbols ./super_simple > LD_DEBUG_super_simple.txt
LD_DEBUG=symbols ./super_simple 2>&1 | grep "symbol=" | wc -l
  188

gcc -shared -fPIC -o my_malloc.so my_malloc.c -ldl
gcc -o ./tests/dyn_test ./tests/dyn_test.c
LD_PRELOAD=./my_malloc.so ./tests/dyn_test
  malloc called with size: 1024
  malloc returned: 0x561c50e5c2a0
  (dynamic linking) this is my test executable! :)
  malloc called with size: 64
  malloc returned: 0x561c50e5c6b0