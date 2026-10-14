CS 452 HW3 -- Unix shell (corrected + simplified)

Build and test (run from this directory):
    make            build ./shell
    make test       run the regression tests (Test/run shell)
    make valgrind   run the shell under valgrind
    make clean      remove build products

Module map (each .c has a comment at the top explaining its job):
    Shell.c        main(): read a line, run it, repeat
    Scanner.c      line of text -> tokens
    Parser.c       tokens -> parse tree (Tree.h); recursive descent
    Interpreter.c  parse tree -> runnable Sequence/Pipeline/Command objects
    Sequence.c     runs pipelines one after another
    Pipeline.c     fork one child per command, wire up pipes, wait
    Command.c      one command: builtins, redirection, exec
    Jobs.c         background pipelines; reaping finished children
    deq.c          double-ended queue used by all of the above

Fixes relative to the first version
    * "cd" with no argument crashed (NULL pointer) -> now prints a warning
    * "cd -" failed the first time it was used     -> fixed
    * a syntax error (e.g. "echo a >") exited the shell -> now reports the
      error, discards the line, and keeps going
    * "echo a |" (nothing after the pipe) was silently accepted -> now an error
    * history file was saved in the current directory -> now ~/.shell_history
      and only used in interactive mode
    * Test_pwd had a path from another machine hard-coded; Test_cd_dash did
      not really test "cd -"; both fixed, and tests added for every bug above
    * removed core dumps, object files and the binary from the submission
