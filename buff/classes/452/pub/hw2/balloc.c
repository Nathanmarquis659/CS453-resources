// ============================================================================
// balloc.c — buddy-system memory allocator (HW2 "Memory Hole")
// ============================================================================
// This file is the heart of the project: it defines the public interface in
// balloc.h (bcreate/bdelete/balloc/bfree/bsize/bprint). The provided modules
// bm.c/bbm.c are used unchanged; freelist.c is our per-size free-list store;
// utils.c supplies the mmalloc gateway and bit/level arithmetic. See
// README.md for the full design write-up, including the bitmap discipline
// with a worked trace.
//
// ---------------------------------------------------------------------------
// ALGORITHM — the buddy system in brief (OSTEP Ch. 15)
// ---------------------------------------------------------------------------
// A pool of `size` bytes is carved into blocks whose sizes are powers of two,
// 2^e, with e in [l,u]. Every block has a unique "buddy" at each level: flip
// bit e of its offset from the pool base (bbm.c baddr* helpers). When a block
// is freed it MERGES with its buddy if that buddy is free and the same size —
// recursively upward. This bounds fragmentation.
//
// ---------------------------------------------------------------------------
// STATE (per pool) — struct balloc_t
// ---------------------------------------------------------------------------
//   base, size    the mmap()ed region. mmap is called ONCE, inside bcreate,
//                 via mmalloc() — hw2.pdf requirement 2. No other code in the
//                 project touches malloc/sbrk/brk.
//   l, u          exponent range: smallest block 2^l, largest 2^u.
//   free          freelist module object (one list per level [l,u]).
//   buddy[l..u]   the ONLY per-block bookkeeping in the allocator (hw2.pdf
//                 req. 1(c), Gorman's scheme): for each level e in [l,u], one
//                 bit per adjacent pair of 2^e blocks. A pair bit is SET when
//                 at least one member of the pair is unavailable — allocated
//                 *or* split further — ("either buddy, or both buddies, are
//                 allocated") and is cleared only when both members are free
//                 and merge. This single bitmap family is now also how
//                 bsize()/bfree() find a block's level and detect a bad free
//                 (see "DETERMINING A BLOCK'S SIZE" below) — hw2.pdf req.
//                 1(c) second paragraph: "One way to determine a block's
//                 size is to use a bitmap, for each free list, to determine
//                 whether either buddy block was allocated from that list."
//                 There is no separate per-block allocation bitmap.
//   alloc_count   number of blocks currently handed out (for bprint).
//
// ---------------------------------------------------------------------------
// THE BIT DISCIPLINE (pair bits) — set/clear rules
// ---------------------------------------------------------------------------
//   SET  pair-bit(pair, e)  when a level-e block LEAVES its free list: either
//                           it is handed to an application (balloc) or it is
//                           split into two children (split()). This now
//                           happens for EVERY level in [l,u], including l
//                           itself — there is no "no pairs at level l"
//                           special case, because bsize() needs a trackable
//                           bit at every level a block can actually occupy.
//   CLEAR pair-bit(pair, e) only in bfree(), and ONLY after a direct
//                           membership test proves BOTH members of the pair
//                           are on list[e]; then both are removed and the
//                           merged parent is pushed exactly once.
//
// Invariant INV (proved by induction in README.md §Bit discipline): a block
// that is on the free list for level e has ALL pair bits CLEAR in its subtree
// (levels <= e). Consequences used below:
//   * an allocated block at level e has pair-bit(e) SET and all lower pair
//     bits CLEAR;
//   * after every bfree returns, no two adjacent free blocks of the same size
//     exist (the merge loop runs to a fixed point), so a pair bit is 0 iff
//     both members are on the list — which then immediately merges.
//
// Why the MERGE decision still uses freelistcontains() (a direct scan of
// list[e]) instead of the pair bit: the pair bit records "at least one member
// is unavailable", so on its own it cannot tell "my buddy is allocated" apart
// from "my buddy was split into children" — either way the bit reads 1. A
// membership scan answers the merge question exactly. It is O(blocks) worst
// case — at most size/2^e pointers, i.e. <= 1024 for the wrapper's 4 KiB pool.
//
// ---------------------------------------------------------------------------
// DETERMINING A BLOCK'S SIZE (and detecting a bad free) FROM THE PAIR BITMAP
// ---------------------------------------------------------------------------
// A pair bit alone can't distinguish two situations that both read as "1":
//   (a) mem itself is currently allocated (or was split) — its OWN pair bit
//       was set directly;
//   (b) mem is currently FREE, resting on list[e], but its bit is *still*
//       set because its buddy is the one that is busy (a block that is freed
//       does not get its pair bit cleared until the buddy also frees and the
//       pair actually merges — see the bit discipline above).
// Both look identical as a single bit. The fix mirrors exactly how the merge
// logic above resolves the same ambiguity: once the scan below finds the
// SMALLEST level e whose pair bit is set for mem (by INV, this is mem's own
// current resting level, whichever of (a)/(b) it is), a single direct check
// — freelistcontains(mem, e) — tells them apart for certain: if mem is found
// ON list[e], it is case (b) (currently free — not a valid allocation to
// free or size); otherwise it is case (a) (currently allocated — bsize is
// e2size(e)). This costs one more O(blocks) scan, same order as the merge
// check already pays, and needs no second bitmap.
//
// ---------------------------------------------------------------------------
// SPLIT / MERGE RULES
// ---------------------------------------------------------------------------
//   balloc(size): e = max(l, ceil_pow2(size)). Find the smallest level f>=e
//     with a free block; split it down to e (always continuing with the low
//     child); pop one child; set its pair-bit(e). Requests > 2^u fail with
//     NULL ("a larger request will fail" — hw2.pdf).
//   bfree(mem):    e = bsize(mem) via the pair-bitmap scan above (this also
//     rejects a double free / bogus free, since a non-allocated mem is caught
//     inside bsize()). Validate the pointer, push onto list[e], then while
//     the buddy is on list[g] (direct membership test): remove both, clear
//     pair-bit(g), push parent.
//   bsize(mem):    smallest e with pair-bit(e) set for mem AND mem itself NOT
//     on list[e] (see above); 2^e if found, else a fatal error (bsize is
//     defined for allocations only — hw2.pdf).
//
// ---------------------------------------------------------------------------
// SAFETY (rubric: "documentation and error messages")
// ---------------------------------------------------------------------------
// Every entry point validates its arguments BEFORE mutating state, with an
// error message naming the exact problem: NULL pool/pointer, pointer outside
// the pool, misaligned pointer, out-of-range request, double free, freeing a
// never-allocated pointer. A corrupted or hostile caller can at worst crash
// loudly — never silently corrupt a free list. This is the same threat model
// as kernel heap hardening: an attacker who can pass arbitrary pointers to
// free() wants silent corruption (the path to code execution); we make every
// bad pointer fatal and identifiable. The hostile-input suite lives in
// tests.c (mode "crash").
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "balloc.h"
#include "bbm.h"
#include "bm.h"
#include "freelist.h"
#include "utils.h"

typedef struct balloc {
  void *base;       // start of the mmap()ed pool region
  size_t size;      // total bytes in the pool (multiple of 2^l, <= 2^u)
  int l, u;         // exponent range [l,u]
  FreeList free;    // freelist module object
  size_t alloc_count; // number of blocks currently handed out
  BBM buddy[64];    // pair bitmap for level e at buddy[e-l], e in [l,u] —
                     // the ONLY per-block bookkeeping (no ablock bitmap)
} balloc_t;

// ============================================================================
// SMALL HELPERS
// ============================================================================

static size_t structsize(int l, int u) {
  (void)l;
  (void)u;
  return sizeof(balloc_t);
}

static size_t nblocks(const balloc_t *p, int e) { return p->size / e2size(e); }

// die: print a precise error and exit. Used for programmer/hostile errors
// (bad API use), NOT for allocation failure (balloc returns NULL instead).
static void die(const balloc_t *p, const char *fn, const char *msg, void *ptr) {
  fprintf(stderr, "%s: %s (pool=%p ptr=%p size=%zu range=[%d,%d])\n", fn, msg,
          p ? (void *)p->base : 0, ptr, p ? p->size : 0, p ? p->l : -1,
          p ? p->u : -1);
  exit(1);
}

// check_ptr: validate `mem` is a plausible level-e block inside this pool.
static void check_ptr(balloc_t *p, const char *fn, void *mem, int e) {
  if (p == 0)
    die(p, fn, "NULL pool", mem);
  if (mem == 0 || (char *)mem < (char *)p->base)
    die(p, fn, "pointer not inside pool", mem);
  size_t off = (size_t)((char *)mem - (char *)p->base);
  if (off + e2size(e) > p->size)
    die(p, fn, "block runs past end of pool", mem);
  // Alignment is relative to the pool base: mmap() guarantees only page
  // alignment for the region start, but buddy math needs off % 2^e == 0.
  if (off % e2size(e) != 0)
    die(p, fn, "misaligned block pointer", mem);
}

// ---------------------------------------------------------------------------
// bcreate: build a new allocator.
//   * mmap() is called here and ONLY here (requirement 2), via mmalloc().
//   * If size is not a power of two or exceeds 2^u, the pool is capped at
//     2^u, rounded down to a multiple of 2^l, then decomposed into powers of
//     two (binary decomposition) and each piece seeds the free lists at its
//     own level (requirement 3). Example: size=60KiB, u=16 -> one 32K + one
//     16K + one 8K + one 4K block. Warnings are printed for capping/rounding.
//   * The struct itself is also mmalloc()ed: the whole program uses only
//     mmap for memory.
// ---------------------------------------------------------------------------
extern Balloc bcreate(unsigned int size, int l, int u) {
  if (l < 0 || u < l) {
    fprintf(stderr, "bcreate: bad range [%d,%d]\n", l, u);
    return 0;
  }
  if (u > 63 || u - l + 1 > 64) {
    fprintf(stderr, "bcreate: range [%d,%d] too large (max exponent 63)\n",
            l, u);
    return 0;
  }
  if (size == 0) {
    fprintf(stderr, "bcreate: size must be > 0\n");
    return 0;
  }
  size_t cap = e2size(u); // largest single block this allocator can serve
  if (size > cap) {
    fprintf(stderr,
            "bcreate: warning: size %u exceeds 2^%d=%zu; pool capped at %zu\n",
            size, u, cap, cap);
    size = cap;
  }
  size_t gran = e2size(l);
  if (size % gran != 0) {
    fprintf(stderr, "bcreate: warning: size %u not a multiple of 2^%d=%zu; "
                    "pool rounded down to %zu\n",
            size, l, gran, size - size % gran);
    size -= size % gran;
    if (size == 0) {
      fprintf(stderr, "bcreate: resulting pool is empty\n");
      return 0;
    }
  }

  balloc_t *p = mmalloc(structsize(l, u));
  if ((long)p == -1) {
    fprintf(stderr, "bcreate: out of memory for allocator struct\n");
    return 0;
  }
  void *base = mmalloc(size);
  if ((long)base == -1) {
    fprintf(stderr, "bcreate: out of memory for pool region\n");
    mmfree(p, structsize(l, u));
    return 0;
  }

  p->base = base;
  p->size = size;
  p->l = l;
  p->u = u;
  p->alloc_count = 0;
  p->free = freelistcreate(size, l, u);
  if (p->free == 0) {
    fprintf(stderr, "bcreate: out of memory for free lists\n");
    mmfree(base, size);
    mmfree(p, structsize(l, u));
    return 0;
  }
  freelistsetbase(p->free, base);

  // Pair bitmaps: level e has nblocks(e)/2 pairs (divup inside bbmcreate).
  // All bits start CLEAR ("nothing allocated"), matching an all-free pool.
  // Covers EVERY level in [l,u], including l itself: bsize()/bfree() now
  // rely on a trackable bit at a block's own level, however small l is.
  for (int e = l; e <= u; e++)
    p->buddy[e - l] = bbmcreate(size, e);

  // Seed the free lists by binary decomposition of `size` into powers of two.
  size_t off = 0;
  while (off < size) {
    int e = u;
    while (e > l && e2size(e) > size - off)
      e--;
    freelistfree(p->free, base, base + off, e, l);
    off += e2size(e);
  }
  return p;
}

// ---------------------------------------------------------------------------
// bdelete: tear the allocator down.
// ---------------------------------------------------------------------------
extern void bdelete(Balloc pool) {
  if (pool == 0)
    return;
  balloc_t *p = pool;
  for (int e = p->l; e <= p->u; e++)
    bbmdelete(p->buddy[e - p->l]);
  freelistdelete(p->free, p->l, p->u);
  mmfree(p->base, p->size);
  mmfree(p, structsize(p->l, p->u));
}

// ---------------------------------------------------------------------------
// split: split a FREE level-(e+1) block into its two level-e children.
//   * the parent is removed from list[e+1] and pushed nowhere;
//   * both children are pushed on list[e] (high child first, so the low child
//     ends up at the front — balloc always continues with the low child);
//   * the PARENT's pair bit (level e+1) is SET: at least one member of that
//     pair is now "unavailable" from the allocator's point of view;
//   * INV: the children go onto list[e] with all pair bits below e clear,
//     because the parent (which was on list[e+1]) had them clear.
// ---------------------------------------------------------------------------
static void split(balloc_t *p, void *parent, int e) {
  // parent is level e+1; children are level e
  check_ptr(p, "split", parent, e + 1);
  if (!freelistremove(p->free, parent, e + 1))
    die(p, "split", "parent not on its free list (corruption?)", parent);
  size_t off = (size_t)((char *)parent - (char *)p->base);
  void *lo = p->base + off;
  void *hi = p->base + (off | e2size(e)); // flip bit e of the offset
  freelistfree(p->free, p->base, hi, e, p->l);
  freelistfree(p->free, p->base, lo, e, p->l);
  bbmset(p->buddy[e + 1 - p->l], p->base, parent, e + 1); // parent pair now busy
}

// ---------------------------------------------------------------------------
// balloc: allocate `size` bytes. Returns a pointer to a block of size
// 2^e >= size (the true size is what bsize() reports), or NULL on failure.
//   * requests smaller than 2^l are rounded UP to 2^l;
//   * requests larger than 2^u fail (hw2.pdf: "a larger request will fail");
//   * search order: smallest usable level first, splitting the smallest
//     possible larger block — minimizes fragmentation growth.
// ---------------------------------------------------------------------------
extern void *balloc(Balloc pool, unsigned int size) {
  balloc_t *p = pool;
  if (p == 0) {
    fprintf(stderr, "balloc: NULL pool\n");
    return 0;
  }
  if (size > e2size(p->u)) {
    fprintf(stderr, "balloc: request %u exceeds max block 2^%d=%zu\n", size,
            p->u, e2size(p->u));
    return 0;
  }
  int e = size2e(size);
  if (e < p->l)
    e = p->l;

  // find the smallest level f >= e that has a free block
  int f = -1;
  for (int g = e; g <= p->u; g++) {
    if (freelistcount(p->free, g) > 0) {
      f = g;
      break;
    }
  }
  if (f < 0) {
    fprintf(stderr, "balloc: no free block of size >= %zu (request %u)\n",
            e2size(e), size);
    return 0;
  }

  // split level f down to level e, always continuing with the low child
  void *blk = freelistpeek(p->free, f);
  for (int g = f; g > e; g--) {
    split(p, blk, g - 1); // parent level g -> children level g-1
    blk = freelistpeek(p->free, g - 1); // front is the low child just made
  }

  // pop the final block and record the allocation
  blk = freelistalloc(p->free, p->base, e, p->l);
  if (blk == 0)
    die(p, "balloc", "internal error: lost a free block during split", 0);
  bbmset(p->buddy[e - p->l], p->base, blk, e); // pair-bit discipline, every
                                                // level including l
  p->alloc_count++;
  return blk;
}

// ---------------------------------------------------------------------------
// bsize: the TRUE size (2^e) of an allocation — not the request size.
//
// No separate per-block bitmap exists anymore (hw2.pdf req. 1(c), 2nd
// paragraph): the SAME pair bitmap used for the buddy-merge bookkeeping is
// reused to find a block's level. Walk e = l..u; by invariant INV, the
// smallest e whose pair bit is set for mem is mem's own current resting
// level (see the file header, "DETERMINING A BLOCK'S SIZE"). That single bit
// can't yet say whether mem itself is the busy one or its buddy is — so once
// found, freelistcontains(mem, e) settles it for certain:
//   * mem found ON list[e]  -> mem is currently FREE (not a live allocation:
//     the pair bit is set only because its buddy is busy) -> fatal.
//   * mem NOT on list[e]    -> mem is the allocated block -> return 2^e.
// If the scan reaches u with no pair bit set at all, mem was never allocated
// (or has fully merged back with no residual bit) -> fatal, same as before.
// ---------------------------------------------------------------------------
extern unsigned int bsize(Balloc pool, void *mem) {
  balloc_t *p = pool;
  if (p == 0) {
    fprintf(stderr, "bsize: NULL pool\n");
    return 0;
  }
  if (mem == 0 || (char *)mem < (char *)p->base)
    die(p, "bsize", "pointer not inside pool", mem);
  size_t off = (size_t)((char *)mem - (char *)p->base);
  if (off >= p->size)
    die(p, "bsize", "pointer past end of pool", mem);

  for (int e = p->l; e <= p->u; e++) {
    // Skip levels that have no blocks at all: in a non-power-of-two pool the
    // top level(s) can be empty (e.g. a 60 KiB pool with u=16 has zero
    // 64 KiB blocks). mem is guaranteed aligned to any level it could
    // legitimately occupy, so this is defensive, not load-bearing.
    if (nblocks(p, e) == 0)
      continue;
    if (!bbmtst(p->buddy[e - p->l], p->base, mem, e))
      continue; // this pair is entirely untouched at level e -> keep looking
    // Pair bit is set: either mem itself is busy here, or its buddy is.
    // freelistcontains() matches by EXACT pointer identity, so we must test
    // the level-e block's own base address, not `mem` verbatim — bsize()
    // must also work for a bogus offset that merely lands *inside* a block
    // (see check_ptr's separate, stricter alignment test in bfree()), so
    // `mem` itself may not be block-aligned even though the block it lands
    // in is. Rounding down here is exactly the "which block owns this byte"
    // step; it never changes the answer for a genuinely block-aligned mem.
    void *blk = p->base + (off & ~(e2size(e) - 1));
    // mem's containing block sitting on the free list at exactly this level
    // means IT is the free one — the pair bit is set on its buddy's
    // account, not this block's.
    if (freelistcontains(p->free, blk, e))
      die(p, "bsize", "pointer is not a current allocation (already free)",
          mem);
    return (unsigned int)e2size(e);
  }
  die(p, "bsize", "pointer is not a current allocation", mem);
  return 0; // unreachable: die() exits
}

// ---------------------------------------------------------------------------
// bfree: release `mem`. Finds its true level with bsize(), pushes it on the
// free list, then merges upward with its buddy while both are free.
//   * double-free / bogus-free defense: entirely inside bsize() now — bsize
//     dies if mem is not currently a live allocation (already free, never
//     allocated, or outside the pool), so by the time bfree proceeds past
//     that call, mem is provably a genuine, currently-allocated block.
//   * merge rule: after pushing mem on list[e], let b = buddy(mem, e). If b
//     is also on list[e] (direct membership test — exact where the pair bit
//     is ambiguous), remove both, clear the pair bit at level e, push the
//     merged parent on list[e+1], and repeat.
// ---------------------------------------------------------------------------
extern void bfree(Balloc pool, void *mem) {
  balloc_t *p = pool;
  if (p == 0) {
    fprintf(stderr, "bfree: NULL pool\n");
    return;
  }
  unsigned int sz = bsize(p, mem); // dies if not a current allocation
  int e = size2e(sz);

  check_ptr(p, "bfree", mem, e);

  freelistfree(p->free, p->base, mem, e, p->l);
  if (p->alloc_count > 0)
    p->alloc_count--;

  // merge upward while the buddy is free at the same level
  void *blk = mem;
  int g = e;
  for (; g < p->u; g++) {
    size_t boff = ((size_t)((char *)blk - (char *)p->base)) ^ e2size(g);
    if (boff + e2size(g) > p->size)
      break; // no buddy exists in this pool (non-power-of-two tail)
    void *buddy = p->base + boff;
    if (!freelistcontains(p->free, buddy, g))
      break; // buddy allocated or split — stop merging
    freelistremove(p->free, blk, g);
    freelistremove(p->free, buddy, g);
    bbmclr(p->buddy[g - p->l], p->base, blk, g); // both free again -> clear
    void *parent = p->base + (((size_t)((char *)blk - (char *)p->base)) &
                              ~(size_t)e2size(g));
    freelistfree(p->free, p->base, parent, g + 1, p->l);
    blk = parent;
  }

  // Settle the pair bit at the level where blk ended up (level g). The merge
  // loop only clears bits on a successful merge, so the two stop conditions
  // that leave blk sitting on L_g need explicit handling to keep INV:
  //   * no buddy exists in the pool (non-power-of-two tail): nothing in the
  //     pair can be allocated, so the bit must be clear even though it may
  //     have been set when blk was handed out or split;
  //   * top level (g == u): no merge check runs at this level, so if the
  //     buddy is also free on L_u, both members of the pair are free and the
  //     bit must be clear (there is no parent level to push onto).
  size_t boff = ((size_t)((char *)blk - (char *)p->base)) ^ e2size(g);
  if (boff + e2size(g) > p->size)
    bbmclr(p->buddy[g - p->l], p->base, blk, g);
  else if (g == p->u && freelistcontains(p->free, p->base + boff, g))
    bbmclr(p->buddy[g - p->l], p->base, blk, g);
}

// ---------------------------------------------------------------------------
// bpoolbase / bfreelist — internal test hooks, used by tests.c to check
// alignment/bounds relative to the region start and to exercise freelistprint
// directly. Deliberately NOT part of the required public interface in
// balloc.h.
// ---------------------------------------------------------------------------
extern void *bpoolbase(Balloc pool) {
  if (pool == 0)
    return 0;
  return ((balloc_t *)pool)->base;
}

extern FreeList bfreelist(Balloc pool) {
  if (pool == 0)
    return 0;
  return ((balloc_t *)pool)->free;
}

// ---------------------------------------------------------------------------
// bprint: human-readable dump of the whole allocator (the "to-string" tool
// hw2.pdf requirement 4 asks for). Prints pool geometry, per-level free-list
// contents, and the buddy-pair bitmaps. Primary debugging view.
// ---------------------------------------------------------------------------
extern void bprint(Balloc pool) {
  balloc_t *p = pool;
  if (p == 0) {
    fprintf(stderr, "bprint: NULL pool\n");
    return;
  }
  printf("pool: base=%p size=%zu range=[%d,%d] allocated-blocks=%zu\n", p->base,
         p->size, p->l, p->u, p->alloc_count);
  for (int e = p->l; e <= p->u; e++) {
    size_t n = freelistcount(p->free, e);
    printf("  level %2d (%6zu B): %4zu free", e, e2size(e), n);
    void *m = freelistpeek(p->free, e);
    int shown = 0;
    while (m && shown < 8) {
      printf(" %p", m);
      // Only in-band lists carry the next pointer inside the block; external
      // small lists (e < ptrlevel) must not be walked this way.
      if (e < freelistptrlevel())
        break;
      m = *(void **)m;
      shown++;
    }
    if (n > 8)
      printf(" ...");
    printf("\n");
  }
  for (int e = p->l; e <= p->u; e++) {
    printf("  buddy bits level %2d: ", e);
    bbmprt(p->buddy[e - p->l]);
  }
}