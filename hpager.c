// hybrid loader
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