# CS 452 HW3 -- a simple Unix shell
#
#   make            build ./shell
#   make test       run the regression tests (Test/run) against ./shell
#   make valgrind   run ./shell under valgrind (interactively)
#   make clean      delete everything make built

CC      = gcc
CFLAGS  = -g -Wall -D_GNU_SOURCE -MMD	# -MMD writes .d files so that editing
LDLIBS  = -lreadline -lncurses		# a header rebuilds what includes it

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
PROG = shell

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

test: $(PROG)
	Test/run $(PROG)

valgrind: $(PROG)
	valgrind --leak-check=full --show-leak-kinds=all \
	    --suppressions=rl.supp ./$(PROG)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(PROG) Test/*/out

-include $(OBJS:.o=.d)

.PHONY: all test valgrind clean
