# HW4 "Whack-a-Mole and Race Conditions"

CS 452 (Operating Systems). A multi-threaded producer/consumer simulation of
the arcade game. Producer threads create moles on a lawn, consumer threads
whack them, and the two sides communicate only through a thread-safe bounded
queue (`Mtq`) that wraps my own deque from HW1. It uses the provided
lawn/mole skeleton (FLTK graphics, or text output when `DISPLAY` is empty).

## Build & run

```sh
make                 # builds ./wam
make run             # runs ./wam with the graphical window
DISPLAY= ./wam       # text output to stdout instead of a window
make valgrind        # plain valgrind (will show FLTK noise)
./vg                 # valgrind with the FLTK suppressions (fltk.supp)
make clean           # remove build artifacts
```

Built with `gcc -g -Wall -D_GNU_SOURCE -MMD -pthread` and linked with
`g++ -pthread -lX11 -lfltk`. The FLTK and X11 development libraries must be
installed (on Debian/Ubuntu: `libfltk1.3-dev libx11-dev`). `deq.c` and `deq.h`
are symlinks to `../hw1`, the same way `error.h` is.

`main.c` starts 10 producer threads and 10 consumer threads against a queue of
capacity 4. Each producer sleeps for a random 1 to 5 seconds inside
`mole_new`, so several moles appear at once; the capacity limit makes
producers wait when the queue is full.

## Files

| file             | role                                                                  |
|------------------|-----------------------------------------------------------------------|
| `main.c`         | `produce` and `consume` thread bodies; creates the lawn and queue     |
| `thr.c/.h`       | runs one function in `n` threads (`thr_start`), waits for them (`thr_join`) |
| `mtq.c/.h`       | MT-safe bounded queue wrapped around `Deq`                            |
| `deq.c/.h`       | doubly-linked deque from HW1 (symlinks to `../hw1`)                   |
| `error.h`        | `WARN` / `ERROR` macros (symlink to `../hw1`)                         |
| `lawn.c/.h`      | lawn (window and FLTK thread), provided, unchanged                    |
| `mole.c/.h`      | mole creation and whacking, provided, unchanged                       |
| `lawnimp.cc/.h`  | FLTK drawing and the text-mode output, provided, unchanged            |
| `linkage.h`      | C/C++ linkage macro, provided, unchanged                              |
| `fltk.supp`, `vg`| valgrind suppressions for FLTK and the helper script (from `hw4.vg`)  |
| `GDB-threads`    | notes on debugging threads in gdb, provided                           |

## Design

### The thread module (`thr.c`)

`thr_start(n, f, arg)` creates `n` threads that each run `f(arg)` and returns
an opaque handle holding the `pthread_t` ids. `thr_join(t)` joins every thread
in the group, then frees the handle. All `n` threads share the same `arg`
pointer, so it must stay valid until `thr_join` returns and must only be read
by the threads. In `main.c` it points to an array `{queue, lawn}` that lives
in `main`'s stack frame, which outlives both joins.

### The MT-safe queue (`mtq.c`)

```
Mtq = { Deq q; int cap; mutex lock; cond notfull; cond notempty }
```

One mutex guards the whole queue, which the handout says is good enough. The
`Deq` is never touched without the lock held, and `deq.c` itself is unchanged.

- **put** takes the lock, waits on `notfull` while the queue is full (only if
  `cap > 0`), adds the item, signals `notempty`, and unlocks.
- **get** takes the lock, waits on `notempty` while the queue is empty, removes
  an item, signals `notfull`, and unlocks.

Two details matter for correctness:

1. **`while`, not `if`, around every `pthread_cond_wait`.** A woken thread must
   re-check the condition: another thread may have taken the slot or the item
   between the signal and the moment this thread got the lock back.
2. **`get` must wait for a non-empty queue.** My `deq_*_get` treats an empty
   deque as a fatal error, so the wrapper is what prevents that call.

Waiting on condition variables means blocked threads sleep instead of
spinning on the lock.

### Ordering in `main`

```
lawn_new -> mtq_new -> start producers -> start consumers
         -> join producers -> join consumers -> mtq_del -> lawn_free
```

`lawn_free` deletes the window and then joins the FLTK thread, so it runs only
after every mole is gone. `mtq_del` runs after both joins, when no other thread
can be using the queue. It is empty by then, because `n` items were put and
`n` were taken.

### Invariants

1. The `Deq` inside an `Mtq` is accessed only while holding that queue's lock.
2. `0 <= length <= cap` whenever `cap > 0`, as seen by any thread holding the
   lock.
3. Every thread started by `thr_start` is joined exactly once.
4. Every mole created by `mole_new` is freed by `mole_whack`.

### Error handling

Failures of `malloc`, `pthread_create`, `pthread_join`, and every mutex and
condition-variable call are reported with `ERROR` (file, line and the
`strerror` text) and exit. A zero pointer or a negative count or capacity is
also reported. These are programmer errors or resource exhaustion, so there is
nothing sensible to recover to.

### Known limitations

- **One lock per queue.** Producers and consumers contend for the same mutex.
  Fine for this program, but not the design for a high-throughput queue.
- **Only a FIFO workload is exercised.** `mtq` also offers `head_put` and
  `tail_get` for symmetry with `Deq`, but `main.c` uses only `tail_put` and
  `head_get`.
- **No shutdown signal.** `mtq_del` assumes every put has been matched by a
  get. A consumer blocked on an empty queue would never wake.
- **The window was not exercised.** I tested with `DISPLAY=` (text mode).

## Test summary

- `DISPLAY= ./wam` builds with no warnings; all 10 moles are created and
  whacked; the ten "creating" lines appear before the first "created", showing
  that the producers overlap.
- `./vg` (valgrind with `fltk.supp`): 0 errors, 0 bytes definitely, indirectly
  or possibly lost, 0 still reachable. Three FLTK blocks (1,784 bytes) are
  suppressed.
- `valgrind --tool=helgrind` with `fltk.supp`: 0 errors, so no data races were
  detected in the new code.
- Not done: a run of the intermediate "threads + unprotected `Deq`" version
  (step 3 of the handout) to see the crash.

## AI Usage
- Claude (Anthropic) was used to help plan the work through the versions of 
  `thr.c`, `mtq.c` and `main.c`, including the
  comments. I used it to ask questions and get clarification on what I did 
  not understand, as well as helping me understand tools like helgrind. 
