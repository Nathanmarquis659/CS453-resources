prog=deq

include ../GNUmakefile

try: main.o libdeq.so
	gcc -o $@ $< -L. -ldeq -Wl,-rpath=.

clean:: ; rm -f $(prog) *.o *.d *.i try
valgrind: $(prog) ; $@ --leak-check=full --show-leak-kinds=all ./$< $(args)
