Makefile rules
make all: makes all pagers and statically linked tests
make static_tests: compiles all tests statically
make dyn_tests: compiles all tests dynamically

run ./apager /bin/ls -l
only apager works dynamically

hpager_p0: prefetch 0 pages
hpager_p1: prefetch 1 page
hpager_p2: prefetch 2 pages


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



the different pagers:
1. apager with static linking: all at once loader running statically linked executable
2. apager with dynamic linking: all at once loader running dynamically linked executable
3. dpager (static linking only): demand pager maps entry page initially, faults on others.
4. hpager prefetch=0 (static linking only): hybrid pager, maps initialized data eagerly, BSS on demand
4. hpager prefetch=1 (static linking only): hybrid pager, maps initialized data eagerly, BSS on demand, and prefetches 1 page on fault
5. hpager prefetch=2 (static linking only): hybrid pager, maps initialized data eagerly, BSS on demand, and prefetches 2 page on fault