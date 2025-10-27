CC = musl-gcc # compiler

# -Wl,-Ttext-segment=0x08048000: linker flag to place the text segment at a specific address
# CFLAGS = -static -g
# APAGER_CFLAGS = -Wl,-Ttext-segment=0x08048000
# TEST_CFLAGS = -no-pie
CFLAGS = -g # general for all tests and pagers
LOADER_CFLAGS = -static -Wl,-Ttext-segment=0x08048000 # always
STATIC_TEST_CFLAGS = -static -no-pie # for static tests
DYNAMIC_TEST_CFLAGS = # no extra flags (only apager)

APAGER = apager
DPAGER = dpager
HPAGER = hpager
LOADERS = $(APAGER) $(DPAGER) $(HPAGER)

TEST_DIR = tests
TESTS_STATIC = $(TEST_DIR)/simple_static $(TEST_DIR)/super_simple_static $(TEST_DIR)/dyn_test_static $(TEST_DIR)/null_test_static
TESTS_DYN = $(TEST_DIR)/simple_dyn $(TEST_DIR)/super_simple_dyn $(TEST_DIR)/dyn_test_dyn $(TEST_DIR)/null_test_dyn

all: $(LOADERS) $(TESTS_STATIC) # default all will compile statically
all_apager: $(APAGER) $(TESTS_DYN) # all with dynamic apager
all_dpager: $(DPAGER) $(TESTS_STATIC) # all with static dpager

$(APAGER): apager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -o $@ $<

$(DPAGER): dpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -o $@ $<

$(HPAGER): hpager.c apager.h
	$(CC) $(CFLAGS) $(LOADER_CFLAGS) -o $@ $<

$(TEST_DIR)/%_static: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $(STATIC_TEST_CFLAGS) -o $@ $<

$(TEST_DIR)/%_dyn: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $(DYNAMIC_TEST_CFLAGS) -o $@ $<

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

test: all_apager all_dpager $(HPAGER)
	@echo ""
	@echo "--- Running apager (dynamic tests) ---"
	@for test in $(TESTS_DYN); do \
		echo "--- Running ./$(APAGER) $$test ---"; \
		./$(APAGER) $$test; \
		echo ""; \
	done
	@echo "--- Running dpager (static tests) ---"
	@for test in $(TESTS_STATIC); do \
		echo "--- Running ./$(DPAGER) $$test ---"; \
		./$(DPAGER) $$test; \
		echo ""; \
	done
	@echo "--- Running hpager (static tests) ---"
	@for test in $(TESTS_STATIC); do \
		echo "--- Running ./$(HPAGER) $$test ---"; \
		./$(HPAGER) $$test; \
		echo ""; \
	done
	@echo "--- All tests complete ---"

clean:
	rm -f $(LOADERS) $(TESTS_STATIC) $(TESTS_DYN)

.PHONY: all all_apager all_dpager run_apager run_dpager clean