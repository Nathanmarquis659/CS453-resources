# Makefile — CS 452 HW2 "Memory Hole" (buddy-system allocator)
#
# Targets:
#   make            build tests (sanitized) and deq_demo (unsanitized)
#   make test       build + run the full suite: all, crash, deq
#   make clean      remove all build artifacts
#
# The library itself is: utils.c bm.c bbm.c freelist.c balloc.c
# (bm.c/bbm.c are the provided modules, used unchanged.)

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -O0
SAN     = -fsanitize=address,undefined
# onyx: /usr/lib64 is missing libubsan.so.1 (libgcc package incomplete), so
# the shared-UBSan link script fails; link the static UBSan runtime from the
# gcc tree instead. No-op for the unsanitized deq_demo target.
LDLIBS  = -static-libubsan

LIBOBJS = utils.o bm.o bbm.o freelist.o balloc.o

all: tests deq_demo

%.o: %.c
	$(CC) $(CFLAGS) $(SAN) -c $< -o $@

# Sanitized test binary (modes: all, utils, crash, deq).
# deq.o is linked so mode "deq" drives the real Deq module; without it,
# test_deq() would fail to link — that's intentional.
tests: tests.o deq.o $(LIBOBJS)
	$(CC) $(CFLAGS) $(SAN) -o $@ $^ $(LDLIBS)

# Deq demo links the provided wrapper.c, which overrides malloc/free/realloc,
# with tests.c (mode "deq") as the workload driver, driving the real Deq
# module (deq.o).
# NOTE: built WITHOUT sanitizers — overriding global malloc conflicts with
# ASan's own internal use of malloc (our pool is 4 KiB; ASan would die).
# The library objects are recompiled without SAN for this target only.
deq_demo: tests_nosan.o wrapper_nosan.o deq_nosan.o utils_nosan.o bm_nosan.o bbm_nosan.o freelist_nosan.o balloc_nosan.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%_nosan.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	@echo '--- unit/integration/stress + crash-safety suite ---'
	./tests all
	@echo '--- Deq workload on the buddy allocator (via wrapper.c, req. 5) ---'
	./deq_demo deq
	@echo 'ALL TEST SUITES PASSED'

clean:
	rm -f *.o tests deq_demo

.PHONY: all test clean
