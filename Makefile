CC = musl-gcc # compiler

# -Wl,-Ttext-segment=0x08048000: linker flag to place the text segment at a specific address
CFLAGS = -static -g
APAGER_CFLAGS = -Wl,-Ttext-segment=0x08048000
TEST_CFLAGS = -no-pie

APAGER = apager
TEST_DIR = tests
TESTS = $(TEST_DIR)/simple $(TEST_DIR)/super_simple $(TEST_DIR)/dyn_test

all: $(APAGER) $(TESTS)

$(APAGER): apager.c apager.h
	$(CC) $(CFLAGS) $(APAGER_CFLAGS) -o $@ $<

$(TEST_DIR)/%: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $(TEST_CFLAGS) -o $@ $<

ARGS = $(filter-out $@,$(MAKECMDGOALS))

run: all
	$(if $(ARGS),,$(error no executable. Usage: make run ./test/exec))
	./$(APAGER) $(ARGS)

clean:
	rm -f $(APAGER) $(TESTS)

.PHONY: all run clean