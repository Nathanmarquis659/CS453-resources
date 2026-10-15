# HW3 "Execution Integrity and Do Process" Unix shell

CS 452 (Operating Systems). A small Unix shell built on fork/exec/wait. It
supports sequences (`;`, `&`), pipelines (`|`), I/O redirection (`<`, `>`), and
the builtins `exit`, `pwd`, `cd` (including `cd -`) and `history`. It uses the
provided scanner/parser/interpreter skeleton, my own deque from HW1, and a
regression suite run through the required `Test/run`.

## Build & test

```sh
make            # builds ./shell
make test       # runs the regression suite: Test/run shell
make valgrind   # runs ./shell under valgrind with the readline suppressions
make clean      # remove build artifacts and generated test output
```

`Test/run` feeds each `Test/Test_*/inp` to the shell, saves stdout in `out`,
and compares it with `exp` (ignoring whitespace). stderr is not compared, so
warnings still show on screen. It exits non-zero if any test fails.

| group             | what it covers                                                           |
|-------------------|--------------------------------------------------------------------------|
| basics            | `echo`, arguments, `;` sequences, `exit` mid-line, `exit` stops the line |
| builtins          | `pwd`, `cd`, `cd -` (including as the *first* `cd`), bare `cd`, bad `cd`, `history` |
| redirection       | `<`, `>`, both together, and builtin output redirected to a file         |
| pipelines         | 2- and 3-stage pipes, builtins inside pipes, 1 MB through three stages   |
| background        | `&` returns to the prompt immediately; `exit` waits for jobs             |
| handout examples  | the six sequence/pipeline inputs from page 5 of the handout              |
| robustness        | syntax errors, unknown program, failed redirect — shell must keep running |

Built with `gcc -g -Wall -D_GNU_SOURCE -MMD` and linked with
`-lreadline -lncurses`. The `-MMD` flag writes `.d` dependency files so editing
a header rebuilds everything that includes it.

**Why the handout example `pwd & cd /etc ; pwd` is not in the suite:** one of
its two output lines is `/tmp` (the background child) and the other is `/etc`
(the foreground `pwd` after the shell moved), and their order depends on
process scheduling. No fixed `exp` file could pass every time. The other five
examples print identical lines wherever they race, so they are safe to assert.

## Files

| file             | role                                                                  |
|------------------|-----------------------------------------------------------------------|
| `Shell.c`        | `main()`: read a line with readline, run it, repeat; history file     |
| `Scanner.c/.h`   | line to tokens; operators `\| & ; < >` are always their own token     |
| `Parser.c/.h`    | recursive-descent parser, one function per grammar rule; frees tree   |
| `Tree.c/.h`      | parse-tree node types (provided, unchanged)                           |
| `Interpreter.c/.h` | parse tree to runnable `Sequence` / `Pipeline` / `Command` objects  |
| `Sequence.c/.h`  | runs pipelines one after another; stops on `exit`                     |
| `Pipeline.c/.h`  | forks one child per command, wires pipes, waits (the only `fork()`)   |
| `Command.c/.h`   | one command: builtins, redirection, `execvp`                          |
| `Jobs.c/.h`      | background pipelines; reaping finished children; `drainJobs` on exit  |
| `deq.c/.h`       | doubly-linked double-ended queue (replaces the binary-only library)   |
| `error.h`        | `WARN` / `ERROR` macros (provided, unchanged)                         |
| `Test/`          | regression tester (`run`) and one `Test_*` directory per test         |
| `rl.supp`, `vg`  | valgrind suppressions for readline and the helper script (provided)   |
| `grammar.txt`    | the grammar from the handout                                          |

## Requirement traceability (HW3)

1. **Builtins run in the shell's process** unless backgrounded or in a
   pipeline — `Pipeline.c:execPipeline` calls `runBuiltin` only for a lone
   foreground builtin; otherwise the child runs it through `execChild`.
2. **Non-builtins via fork/exec, parent waits** — `startChildren` forks,
   `Command.c:execChild` calls `execvp`, `waitForeground` blocks until every
   child of the pipeline is reaped.
3. **`&` does not block, and children are eventually reaped** — a background
   pipeline is handed to `Jobs`; `reapJobs` runs before every prompt.
4. **Pipelines** — `Pipeline.c` creates the pipes, `dup2`s them onto each
   child's stdin/stdout, and closes every unused end.
5. **Redirection** — `Command.c:redirect` opens the files and `dup2`s them onto
   fd 0/1; the same helper serves both the in-shell and in-child paths.
6. **`exit` waits for background jobs** — `Shell.c` calls `drainJobs` before
   returning.
7. **`cd -`** — `previousDir` remembers the directory before the last
   successful `cd`.
8. **`history`** — walks `history_list()` from the GNU History library.
9. **Own deque** — `deq.c` replaces the provided shared-library binary.
10. **Regression tester required** — `Test/run`, wired to `make test`.
11. **valgrind-clean** — 0 errors, 0 definitely/indirectly/possibly lost bytes
    (a few terminal-library blocks are reported as still reachable).
12. **Modularity** — each stage of the pipeline from text to process has its
    own module with a header describing its interface.

## Design

### Where each command runs

```
lone foreground builtin (cd, pwd, exit, history)  -> in the SHELL process
anything else (programs, piped/backgrounded builtins) -> in a forked CHILD
```

This one rule explains the handout's examples. A child's `chdir` dies with the
child, so `cd /etc | pwd` still prints the old directory, while `cd /etc ; pwd`
prints `/etc` because the shell itself moved.

### Pipeline execution (`Pipeline.c`)

For `a | b | c` the shell creates two pipes and three children:

```
a --stdout--> [pipe 0] --stdin--> b --stdout--> [pipe 1] --stdin--> c
```

Each child `dup2`s its two pipe ends onto fd 0/1, then **closes every pipe
descriptor**, then calls `execChild`, which applies any `<`/`>` (a redirection
overrides a pipe, as in bash). The parent closes all pipe ends right after
forking. A reader only sees end-of-file when *every* copy of the write end is
closed, so one stray descriptor in any process would hang the pipeline.

`Pipeline.c` owns the topology (pipes, forks, `dup2`); `Command.c` exposes only
`execChild(cmd)` and `runBuiltin(cmd)`. `Pipeline` uses `Command`, never the
reverse.

### Invariants

1. Every pipe end is closed in every process that does not use it.
2. Every child is reaped exactly once.
3. The shell's own stdin/stdout are never left redirected: `runBuiltin` saves
   copies with `dup`, applies the redirection, runs, flushes, and restores —
   including when the redirection itself fails.

### Waiting and reaping (`Jobs.c`)

`waitpid(-1, ...)` returns *any* child. A foreground wait that happens to reap
a background job's child credits it to that job (`creditJobs`) instead of
losing it. `reapJobs` uses `WNOHANG` in a loop before each prompt, and
`drainJobs` blocks until every job is done. A `Pipeline` records one PID slot
per command and a pending count; a job is freed when the count reaches zero.

### Parsing (`Parser.c`)

The parser handles **one pipeline at a time**: parse, run, parse the next. This
is required for correctness — in `cd /etc ; pwd` the `cd` must already have
happened, and in `exit ; echo hi` the `echo` must not run. The consequence is
that in `echo a ; echo b >` the first command has already run when the error in
the second is found (bash would reject the whole line first).

### Builtins (`Command.c`)

Builtins are a small table of `{name, function}`. Every builtin validates its
argument count first and does nothing on a mismatch (so a bare `cd` prints a
warning instead of dereferencing a NULL `argv[1]`). `cd` asks `getcwd()` for the
current directory each time instead of caching it, and updates `previousDir`
only *after* `chdir` succeeds.

### fork and stdio

`fflush(NULL)` runs before every `fork`, because the child inherits a copy of
the parent's unflushed stdio buffers and would otherwise print them a second
time. Children leave with `_exit`, not `exit`, for the same reason.

### Memory ownership

| object            | owner / rule                                                    |
|-------------------|-----------------------------------------------------------------|
| parse tree        | owns all its strings; `Parser.c` frees it after each pipeline   |
| `Command`         | makes its own copies of the words and file names                |
| `Pipeline`        | owns its `Command`s and its PID array                           |
| `Sequence`        | owns its `Pipeline`s (any not run because of `exit` are freed)  |
| `Jobs`            | owns background `Pipeline`s until their children are reaped     |

### Safety model

Every error that the user can cause is reported and survived: a syntax error
(missing file name, stray operator, empty side of `|`), an unknown program
(exit status 127), an unopenable redirection file, a failed `cd`, or a wrong
argument count. Only `fork`, `pipe` and `malloc` failures are fatal. Programs
are started with `execvp` and an argument *array*, so nothing is re-parsed by a
second shell (no command injection through arguments). Output files are created
with mode 0644.

### Known limitations

- **Ctrl-C during a foreground command kills the shell too.** The terminal sends
  `SIGINT` to the whole foreground process group, which includes the shell.
  Fixing it properly is job control (CS 552): `setpgid`, `tcsetpgrp`, ignoring
  `SIGINT`/`SIGTSTP`/`SIGTTOU` in the shell, and restoring defaults in children.
- **A finished background child stays a zombie until the next Enter.** Reaping
  happens before each prompt rather than from a `SIGCHLD` handler.
- **No quoting, variables, globbing, or `$?`.** The scanner splits on
  whitespace and exit statuses are discarded.
- **CS 552 features are not implemented:** subshells `( )`, groups `{ }`,
  `jobs`, `fg`, `bg`.

## Test summary

- `make test`: 25 tests, 0 failures. Run 30 times in a row to check for
  flakiness: 0 failed runs.
- valgrind over every test input: 0 errors, 0 definitely/indirectly/possibly
  lost bytes.
- The regression tests added for the bare-`cd` crash, the first-`cd -` failure
  and the fatal syntax error were also run against the previous version of the
  shell to confirm they fail there (segfault, `2` instead of `1`, and shell
  exit respectively).
- Tested interactively through a pseudo-terminal: prompt, history, `cd -`,
  Ctrl-D exit, and `~/.shell_history` written on exit.

## AI Usage
- A local model (Qwen3.8-27B) was used to help create the first revision of the
  codebase and discuss the design with me so that I understood better what was
  going on. It helped to cleanup broken code and thoughts in order to get to a
  minimally workable solution.
- Claude (Anthropic) was used to review my first version of this shell against
  the handout, which found segfaults on a bare `cd`, a broken first `cd -`, a
  parser that exited the whole shell on a syntax error, and two misleading
  tests. It was then used to fix those, simplify and comment the code, add
  regression tests, and write a level-by-level explanation guide that I used to
  study how the shell works.