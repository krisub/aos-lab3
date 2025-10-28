CC = musl-gcc # compiler

# -Wl,-Ttext-segment=0x08048000: linker flag to place the text segment at a specific address
# CFLAGS = -static -g
# APAGER_CFLAGS = -Wl,-Ttext-segment=0x08048000
# TEST_CFLAGS = -no-pie
CFLAGS = -g # general for all tests and pagers
LOADER_CFLAGS = -static -Wl,-Ttext-segment=0x08048000 # always
STATIC_TEST_CFLAGS = -static -no-pie -Wl,-Ttext-segment=0x40000000 # for static tests
DYNAMIC_TEST_CFLAGS = -Wl,-Ttext-segment=0x40000000 # no extra flags (only apager)

APAGER = apager
DPAGER = dpager
HPAGER_P0 = hpager_p0 # prefetch 0 pages
HPAGER_P1 = hpager_p1 # prefetch 1 page
HPAGER_P2 = hpager_p2 # prefetch 2 pages
LOADERS = $(APAGER) $(DPAGER) $(HPAGER_P0) $(HPAGER_P1) $(HPAGER_P2)

TEST_DIR = tests
TEST_SRCS := $(shell find $(TEST_DIR) -name '*.c')
TESTS_STATIC := $(patsubst %.c,%_static,$(TEST_SRCS))
TESTS_DYN := $(patsubst %.c,%_dyn,$(TEST_SRCS))

all: $(LOADERS) $(TESTS_STATIC) # default all will compile statically
all_apager: $(APAGER) $(TESTS_DYN) # all with dynamic apager
all_dpager: $(DPAGER) $(TESTS_STATIC) # all with static dpager
all_hpager: $(HPAGER_P0) $(HPAGER_P1) $(HPAGER_P2) $(TESTS_STATIC) # all with static hpager

$(APAGER): apager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -o $@ $<

$(DPAGER): dpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -o $@ $<

$(HPAGER_P0): hpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -DPREFETCH=0 -o $@ hpager.c

$(HPAGER_P1): hpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -DPREFETCH=1 -o $@ hpager.c

$(HPAGER_P2): hpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -DPREFETCH=2 -o $@ hpager.c

%_static: %.c
	@mkdir -p $(dir $@) 
	$(CC) $(CFLAGS) $(STATIC_TEST_CFLAGS) -o $@ $<

%_dyn: %.c
	@mkdir -p $(dir $@) 
	$(CC) $(CFLAGS) $(DYNAMIC_TEST_CFLAGS) -o $@ $< -lm

.PHONY: static_tests dyn_tests
static_tests: $(TESTS_STATIC)
dyn_tests: $(TESTS_DYN)

ARGS = $(filter-out $@,$(MAKECMDGOALS))

run: all
	$(if $(ARGS),,$(error no executable. Usage: make run ./test/exec))
	./$(APAGER) $(ARGS)

run_apager: all_apager
	$(if $(ARGS),,$(error no executable. Usage: make run_apager ARGS=./tests/simple_dyn))
	./$(APAGER) $(ARGS)

run_dpager: all_dpager
	$(if $(ARGS),,$(error no executable. Usage: make run_dpager ARGS=./tests/simple_static))
	./$(DPAGER) $(ARGS)

# default will be hpager with 1 page prefetch
run_hpager: all_hpager
	$(if $(ARGS),,$(error no executable. Usage: make run_hpager ARGS=./tests/simple_static))
	./$(HPAGER_P1) $(ARGS)

clean:
	rm -f $(LOADERS) $(TESTS_STATIC) $(TESTS_DYN)

.PHONY: all all_apager all_dpager static_tests dyn_tests run_apager run_dpager test clean