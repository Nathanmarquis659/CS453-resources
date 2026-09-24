# HW2 "Memory Hole" — buddy-system allocator

CS 452 (Operating Systems). Implements the assignment's required interface in
`balloc.h`: a power-of-two buddy allocator over an mmap()ed pool, with
per-size free lists, buddy-pair bitmaps, no headers in allocated blocks, and
loud failure on hostile input.

## Build & test

```sh
make            # builds tests (sanitized) + deq_demo (unsanitized)
make test       # runs the full suite: ./tests all && ./tests crash && ./tests deq
make clean      # remove build artifacts
```

`tests.c` is a single binary with modes selected by argument:

| mode          | what it runs                                                            |
|---------------|-------------------------------------------------------------------------|
| `all` (default) | unit tests for every module + integration tests + 200k-op randomized stress (shadow model) |
| `utils`       | extended utils section, incl. cross-byte bit indices                    |
| `crash`       | hostile-input suite: each case runs in a forked subprocess and must die(1) with a diagnostic (or return NULL cleanly where specified) |
| `deq`         | 200k-op workload on the real Deq module (`deq.c/h`) through the provided `wrapper.c`, which overrides global malloc/free/realloc (req. 5). Linked unsanitized — see Makefile note. |

Everything is built with `gcc -std=c11 -Wall -Wextra -g -O0` plus
`-fsanitize=address,undefined`. The only exception is the deq build, which
links the provided `wrapper.c` (it overrides global malloc/free/realloc,
which conflicts with ASan's internals) — its objects are recompiled without
sanitizers for that target only.

**Why deq mode runs with unbuffered stdio:** in the `deq_demo` build, glibc
does not allocate the stdout buffer until the first `printf`. With malloc
overridden, that lazy 4 KiB allocation would be served from our 4 KiB pool —
consuming the *entire* pool before a single workload op runs (the next node
malloc then fails with "no free block of size >= 32"). `main()` therefore
calls `setvbuf(stdout, NULL, _IONBF, 0)` before dispatching: unbuffered
stdio never allocates from the pool. The same guard is harmless in the
sanitized build (no malloc override there).

## Files

| file             | role                                                                  |
|------------------|-----------------------------------------------------------------------|
| `balloc.h`       | required public interface (provided, unchanged)                        |
| `balloc.c`       | the allocator: bcreate/bdelete/balloc/bfree/bsize/bprint + test hooks  |
| `freelist.h/.c`  | per-size free lists (in-band intrusive + external small lists)         |
| `bm.h/.c`        | provided general bitmap module (unchanged)                             |
| `bbm.h/.c`       | provided buddy-bitmap module (unchanged)                               |
| `utils.h/.c`     | mmalloc/mmfree (the only mmap gateway), divup/bits2bytes, e2size/size2e, bit ops |
| `deq.h/.c`       | doubly-linked Deq module (put/get/ith/rem at both ends, map/del/str)   |
| `wrapper.c`      | provided malloc/free/realloc override for the Deq test (req. 5)        |
| `tests.c`        | all test suites, mode-driven (all/utils/crash/deq)                     |

## Requirement traceability (HW2)

1. **No headers in allocated blocks** — allocation size is recovered from
   the buddy-pair bitmaps (`buddy[]`), never from data stored in the block.
2. **mmap only via mmalloc, only during bcreate** — `utils.c:mmalloc` is the
   single mmap gateway; `bcreate` is the only call site that allocates the
   pool (everything else is management memory, also via mmalloc).
3. **Non-power-of-two pools** — capped at 2^u, rounded to a multiple of 2^l,
   then seeded by binary decomposition into powers of two.
4. **To-string tool** — `bprint` dumps lists and bitmaps; `freelistprint`
   dumps the list layer alone.
5. **Deq via wrapper.c** — `./tests deq` drives the real Deq module (`deq.c/h`,
   every op a heap node) for 200k push/pop/churn ops on our allocator through
   the provided wrapper.

## Design

### The buddy system in brief (OSTEP Ch. 15)

A pool of `size` bytes is carved into blocks whose sizes are powers of two,
2^e, with e in [l,u]. Every block has a unique **buddy** at each level: flip
bit e of its offset from the pool base (the `baddr*` helpers in bbm.c).

- **Allocating** a 2^e block from a larger free block *splits*: the parent is
  replaced by its two children, one child is handed out, the other stays on
  the smaller list. Splitting continues until the exact level is reached.
- **Freeing** merges: if the buddy (same size, offset differing only in bit e)
  is also free, the pair becomes their parent and the process repeats upward.

### State: `struct balloc`

| field          | meaning                                                        |
|----------------|----------------------------------------------------------------|
| `base`, `size` | the mmap()ed region (mmap called ONCE, inside `bcreate`, via `mmalloc()` — req. 2) |
| `l`, `u`       | exponent range: smallest block 2^l, largest 2^u                |
| `free`         | freelist module object — one free list per level [l,u]         |
| `buddy[l..u]`  | **buddy-pair bitmaps** (req. 1c): for each level e in [l,u] (including l itself), one bit per adjacent pair of 2^e blocks. Set iff at least one member of the pair is unavailable — allocated *or* split further ("either buddy, or both buddies, are allocated"). This is the ONLY per-block bookkeeping in the allocator: `bsize()`/`bfree()` also use it directly (see below) — there is no separate allocation bitmap. |
| `alloc_count`  | number of blocks currently handed out (for bprint)             |

The struct itself is mmalloc()ed: the whole program uses only mmap for memory.

### Pool creation and seeding (`bcreate`)

1. Validate l <= u, size > 0, exponents in range.
2. **Cap**: if `size > 2^u`, cap at 2^u (a larger request will fail — the
   assignment's stated behavior). Print a warning.
3. **Round down** to a multiple of 2^l; print a warning. Any remainder is
   unusable and left out.
4. mmalloc the struct, then mmalloc the pool region.
5. Create the freelist object and set its base (`freelistsetbase`).
6. Create one `buddy` pair bitmap per level l..u, including l itself (all
   bits start CLEAR — nothing is allocated).
7. **Seed** the free lists by binary decomposition of `size` into powers of
   two, largest first (req. 3): push a block of each size onto its own list
   via `freelistfree`. Example: size = 60 KiB, u = 16 -> one 32K + one 16K +
   one 8K + one 4K block.

### The bit discipline (pair bits) — set/clear rules

```
SET   buddy[e][pair]   when a level-e block LEAVES its free list: either it is
                       handed to an application (balloc) or it is split into
                       two children (split()).
CLEAR buddy[e][pair]   only in bfree(), and ONLY after a direct membership
                       test proves BOTH members of the pair are on list[e];
                       then both are removed and the merged parent is pushed
                       exactly once.
```

**Invariant INV** (by induction over operations): a block that is *on the free
list* for level e has all pair bits CLEAR in its subtree (levels <= e).

- Base case: after bcreate, every block is on a list and every bit is clear.
- balloc splits a chain of blocks; each split sets exactly the parent's pair
  bit at the level being split, and the children go onto lists with their
  lower bits still clear. The handed-out block has its own pair bit set (it
  left the list) and all lower bits clear. INV holds.
- bfree pushes the freed block, then merges only when both members are on the
  list, clearing the pair bit at each merged level. A block that lands on a
  list has all lower bits clear because any set lower bit would mean one of
  its descendants was allocated — impossible for a freshly freed or merged
  block.

Consequences used by the code:

- An allocated block at level e has pair-bit(e) SET and all lower pair bits
  CLEAR.
- After every bfree returns, no two adjacent free blocks of the same size
  exist (the merge loop runs to a fixed point), so a pair bit is 0 iff both
  members are on the list — which then immediately merges.

**Worked trace: pool = 16 bytes, l=0, u=4**

```
start:        L4:[+0]            bits all clear

balloc(4):    split +0(16) -> [+0,+8); split +0(8) -> [+0,+4)
              L3:[+8] L2:[+4]  hand out +0
              set bits: level4 pair(+0), level3 pair(+0), level2 pair(+0)

balloc(8):    pop +8 from L3
              set bit: level3 pair(+8)
              free now: [+4] on L2

bfree(+0):    push +0 on L2. Buddy of +0 at level 2 is +4 -> ON the list.
              remove both, CLEAR level2 pair(+0), push parent +0(8) on L3.
              Level 3: buddy of +0 is +8 -> NOT on the list (allocated).
              stop. State: L3:[+0]

bfree(+8):    push +8 on L3. Buddy +0 IS on L3 -> remove both, CLEAR level3
              pair(+0), push parent +0(16) on L4. Level 4 has no buddy in a
              16-byte pool (bit 4 of offset would exceed size) -> stop.
              State: L4:[+0] — the whole pool is back as one block.
```

**Trailing half-pair (non-power-of-two pools).** When the pool is not a power
of two, the top level can hold an odd number of blocks (e.g. 60 KiB has one
4 KiB block at level 12). Its unpaired member's pair bit was never SET by
anything (split only sets parent bits; balloc never reaches this level here),
so `bfree` clears it explicitly when the merge-back would complete into a
parent that does not fit — otherwise a stale 1 would contradict INV and could
make a later split/merge misread the pair's state.

### One bitmap, two jobs — and why the merge test still scans the list

The pair bitmaps implement req. 1c exactly, and per req. 1c's second
paragraph ("one way to determine a block's size is to use a bitmap, for each
free list, to determine whether either buddy block was allocated from that
list") they are also the *only* structure `bsize()`/`bfree()` need. There is
no separate per-block allocation bitmap.

A single bit per pair cannot, by itself, answer "is *this specific* buddy
free?" — both "buddy allocated" and "buddy split into children" present as a
set bit, and (new wrinkle) so does "I was just freed but my buddy is still
busy, so we haven't merged yet." (In the Linux page allocator every page has
its own bit, so the buddy check is a single bit test; bbm.c's compressed pair
bits trade that precision for half the memory.) Both ambiguities are resolved
the same way — a direct, exact scan of the free list:

- **Merge decision** (`bfree()`): `freelistcontains(buddy, g)` — O(blocks)
  worst case, at most size/2^e pointers (<= 1024 for the wrapper's 4 KiB
  pool).
- **Size / allocation-status decision** (`bsize()`): walk e = l..u; by
  invariant INV the smallest e whose pair bit is set for `mem` is `mem`'s own
  current resting level. That still doesn't say whether `mem` itself is the
  busy half or its buddy is — so `bsize()` asks the same question the merge
  logic already asks: is `mem`'s own block (rounded down to its level-e base,
  since `freelistcontains` matches by exact pointer) currently ON list[e]? If
  so, `mem` is free (the bit is set on its buddy's account) and `bsize()`
  dies with "not a current allocation." If not, `mem` is the allocated one
  and `bsize()` returns 2^e.

This makes `bsize()` O(levels) for the scan plus one O(blocks) list check,
and gives `bfree()` its double-free detector for free: a `mem` that isn't
currently allocated fails inside `bsize()` before `bfree()` ever touches the
free lists.

### Split / merge rules (code-level)

**balloc(size)**
1. e = max(l, ceil_pow2(size)). If size > 2^u: print a message, return NULL.
2. Find the smallest level f >= e with freelistcount(f) > 0; if none, NULL.
3. Split down from f to e, always continuing with the LOW child (baddrclr),
   so allocation addresses stay deterministic and low-address-first.
4. Pop the final block, set buddy[e], return it.

**bfree(mem)**
1. e = bsize(mem) — this is also the double-free/bogus-free check: bsize()
   dies if mem is not a current allocation (see "One bitmap, two jobs"
   above), so bfree() never touches the free lists for a bad pointer.
2. check_ptr: in-pool, block fits, aligned to 2^e relative to base.
3. Push mem on list[e].
4. Merge loop g = e..u-1: compute buddy offset (offset ^ 2^g). If the buddy
   would run past the end of the pool (non-power-of-two tail), break. If the
   buddy is NOT on list[g], break. Otherwise remove both, clear buddy[g],
   push the parent on list[g+1], and continue with the parent.

### Free-list storage (freelist module)

- **Levels e >= PTRLEVEL** (block can hold a void*, 3 on this platform):
  IN-BAND intrusive list — the first word of every FREE block is the pointer
  to the next free block of the same size (req. 1b); NULL marks the tail. The
  per-list HEAD lives in the FreeList struct (management memory outside the
  pool). ALLOCATED blocks carry no management data at all (req. 1a) — when a
  block is popped, its first word belongs to the caller.
- **Levels e < PTRLEVEL**: EXTERNAL list — a growable pointer array in
  management memory, because a 1- or 2-byte block cannot contain a pointer
  (only reachable when l < PTRLEVEL).
- Ordering: push-to-front / pop-from-front (LIFO), so repeated alloc/free
  cycles reuse the most recently freed block.

### Safety model (rubric: "documentation and error messages")

Every entry point validates its arguments BEFORE mutating state, with an
error message naming the exact problem: NULL pool/pointer, pointer outside
the pool, misaligned pointer, out-of-range request, double free, freeing a
never-allocated pointer. A corrupted or hostile caller can at worst crash
loudly — never silently corrupt a free list.

Threat model (the security lens): an attacker who can pass arbitrary pointers
to free() wants *silent* corruption — the classic path from heap bug to code
execution (use-after-free, fake chunk headers). We make every bad pointer
fatal and identifiable instead: `./tests crash` drives exactly these hostile
inputs in subprocesses and asserts each one dies with exit(1) plus a
diagnostic. The same philosophy as kernel heap hardening (SLAB redzones,
panic-on-corruption): detect loudly, never continue corrupted.

Allocation *failure* (pool exhausted, request too large) is not an error: it
returns NULL with a message, and the pool stays fully usable afterwards
(verified by the crash suite's alloc_huge case).

### bprint — the "to-string" tool (req. 4)

`bprint(pool)` dumps: pool geometry (base, size, range, allocated count),
each level's free-list contents as offsets from base (up to 8 entries), and
every buddy pair bitmap via `bbmprt`. It is the primary debugging view and is
safe for external small lists (it only walks in-band lists directly; see
`freelistptrlevel()`).

## Test summary

- `./tests all`: ~370k checks across utils, bm, bbm, freelist, bcreate/sizing/
  merge/exhaustion, and a 200k-op randomized stress run with an independent
  shadow model (no overlaps, capacity conserved, full pool reusable at the end).
- `./tests crash`: 10/10 hostile cases — double free, bogus/outside/misaligned/
  NULL frees, bogus bsize, oversize alloc, NULL-pool alloc, plus Deq error
  paths (get on empty, out-of-range ith) — each verified to exit with code 1
  and a diagnostic (or return NULL cleanly where specified).
- `./tests deq`: 200k operations on the real Deq module (push/pop at both
  ends + scratch-buffer churn) complete without error on the buddy allocator
  via wrapper.c.
- Deq API coverage: `test_deq_unit` (ordering, `ith`/`rem` from both ends,
  `map`, `str` with and without formatter, `del` with callback — under ASan)
  plus two hostile cases in the crash suite (`deq_get_empty`, `deq_ith_oob`).

## AI Usage

- A local open-weight model (Qwen3.8-27B) was used for help with documentation, testing, and
refactoring code to be more logical and readable. This assignment was very 
confusing for me. It certainly helped a lot to work through various flawed solutions to 
creating the buddy allocator, and I feel like I understand it much more. I had
it give me questions during the entire process to test my understanding, which 
helped solidify the concepts and provide a springboard for writing my code, especially
as C is not my strongest language. 