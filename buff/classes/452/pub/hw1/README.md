#Queue/Deque with Anonymous Data {#mainpage}

* Author: Nathan Marquis
* Class: CS452 Operating Systems Section #002

##Overview

This program implements a generic doubly linked deque (double ended queue)
that can insert and remove data from either end in constant time. The
deque stores anonymous data using `void *`, so it works like Java's
`ArrayDeque<Object>` or Python's `collections.deque`, holding any kind of
pointer without knowing what it points to. The library supports adding
and removing from the head or tail, indexed access from either end,
removal of a specific value by pointer equality, mapping a function over
every element, and converting the deque to a string.

##Manifest

* `deq.c`, the implementation of the deque, including the private `Node`
  and `Rep` structs and all public `deq_*` functions.
* `deq.h`, the public interface and type declarations for the deque
  (provided by the course).
* `error.h`, a small helper macro used to report fatal errors such as a
  null pointer or a failed `malloc()` (provided by the course).
* `main.c`, a bespoke test harness exercising every public function.
* `GNUmakefile`, the build configuration used by `make` and `make try`.

##Building the project

From the project directory, run:

```
make
```

This compiles `deq.c` and `main.c` into an executable. To run the test
harness, execute the resulting binary, for example:

```
./deq
```

To check the implementation against the instructor provided reference
solution instead of this file's own `main.c`, set `prog=deq` in the
`GNUmakefile` and run:

```
make try
```

##Features and usage

The library exposes these operations, each available on the head and the
tail:

* `deq_new()`, create a new, empty deque.
* `deq_head_put(q, d)` / `deq_tail_put(q, d)`, insert data at that end.
* `deq_head_get(q)` / `deq_tail_get(q)`, remove and return the data at
  that end. Returns `0` if the deque is empty.
* `deq_head_ith(q, i)` / `deq_tail_ith(q, i)`, return the data at index
  `i` counting inward from that end, without removing it. Returns `0` for
  an index that is negative or out of range.
* `deq_head_rem(q, d)` / `deq_tail_rem(q, d)`, search from that end and
  remove the first node whose data pointer equals `d`. Returns `0` if not
  found.
* `deq_len(q)`, the current number of elements.
* `deq_map(q, f)`, call `f` on every element's data, from head to tail.
* `deq_del(q, f)`, free the deque. If `f` is given, it is called on each
  element's data first so the caller's payloads can be freed too.
* `deq_str(q, f)`, build a single string of the elements separated by
  spaces, using `f` to convert each element's data to a string (or
  treating the data as already a string if `f` is `0`).

A typical usage pattern is `q = deq_new()`, followed by any mix of
`put`/`get`/`ith`/`rem` calls, and finally `deq_del(q, f)` once the deque
is no longer needed.

##Testing

Testing was done with a dedicated `main.c` harness containing 40 checks,
each printing PASS or FAIL, with a summary count at the end. The checks
are grouped by category:

* Behavior on an empty deque (`get`, `ith`, `rem` all safely return `0`).
* Basic single element put and get on both the head and the tail.
* Ordering: FIFO behavior (put at tail, get from head) and LIFO behavior
  (put and get from the same end).
* `ith()` traversal, including the first and last valid index and out of
  range indexes (both negative and too large), plus confirming that
  `ith()` does not mutate the deque.
* `rem()` removing the head, the tail, a middle element, and the final
  remaining element, as well as attempting to remove a value that is not
  present.
* A mixed sequence of head and tail puts to confirm the two ends stay
  consistent with each other.
* `deq_str()` with a supplied stringify function, checked against the
  expected output string.

All 40 checks passed on the current implementation.

###Valgrind

The test binary was run under `valgrind --leak-check=full`. The full run
reported:

```
==192446== HEAP SUMMARY:
==192446==     in use at exit: 0 bytes in 0 blocks
==192446==   total heap usage: 39 allocs, 39 frees, 1,877 bytes allocated
==192446== 
==192446== All heap blocks were freed -- no leaks are possible
==192446== 
==192446== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

No leaks, no invalid reads or writes, and no other errors were reported.

###Known Bugs

None known at this time. The test suite has not yet exercised removing
duplicate values or storing real heap allocated payloads (as opposed to
integers packed into a pointer), so those cases should be treated as
untested rather than confirmed correct.

##Discussion

The biggest challenge was reasoning correctly about pointers and manual
memory management, since C does not zero initialize memory the way Java
does and does not garbage collect the way Java or Python do. Early
drafts of `get()` read a node's data after the node had already been
freed, which is undefined behavior in C rather than a clean exception
like it would be in Python. The fix was to always save a copy of the
data into a local variable before calling `free()`, and to save the
pointer to the node itself before any pointer surgery reassigned the
deque's head or tail pointers.

`put()` went through several revisions as well. The first version set
both the head and tail pointers to the new node unconditionally, which
worked for an empty deque but silently orphaned the existing nodes on
every later call. The fix was to only set both pointers when the deque
was empty, and otherwise only update the pointer for the end being
inserted at, after first linking the new node to the old boundary node.

A smaller but instructive issue was a C specific ordering problem: a
helper function was originally defined below the function that called
it, which does not work in C the way it would in Java or Python, since C
compiles top to bottom without a full pass to collect all symbols first.
The fix was to move the helper above its caller.

What finally "clicked" was seeing that the head/tail symmetry could be
captured entirely with a two element array indexed by an enum (`Head`,
`Tail`), plus a small helper to flip between them. Once that pattern was
in place, the head and tail versions of `put`, `get`, `ith`, and `rem`
all became the same code parameterized by which end was passed in,
rather than duplicated logic.

##Sources used

* Course lecture notes and the assignment specification (`hw.pdf`) for
  this project.
* Skeleton and reference files provided in `pub/hw1`.
* Claude Sonnet 5 for questions about implementation, and also to aid with documentation and the test suite in main
