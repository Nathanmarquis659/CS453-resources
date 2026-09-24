// ============================================================================
// bbm.c — buddy-pair bitmap for the Buddy System (PROVIDED MODULE, logic
//         unchanged)
// ============================================================================
// A BBM is a BM (bm.h) specialized for level e: one bit per PAIR of adjacent
// 2^e blocks, rather than one bit per block. This directly implements HW2
// "Other Requirements" 1(c): "a bitmap, for each free list, to quickly
// determine whether a block's buddy is on that list... A pair's bit records
// that either buddy, or both buddies, are allocated." balloc.c is
// responsible for the SET/CLEAR discipline (when a bit flips); this module
// only provides the addressing arithmetic that turns a block address into
// the correct bit index, and the plain get/set/clear/test/print primitives
// (by delegating to bm.c once that index is known).
//
// The four baddr* helpers below implement "buddy" address arithmetic:
// because two buddies at level e differ from each other in EXACTLY bit e of
// their offset from the pool base (that is the defining property of the
// buddy system — see balloc.c's header comment), flipping/clearing/setting
// bit e of an address's pool-relative offset is how you move between a
// block and its buddy, or find the canonical ("low") address of a pair.
// ============================================================================

#include "bbm.h"
#include "bm.h"
#include "utils.h"

// mapsize — number of bits a level-e pair-bitmap needs for a pool of `size`
// bytes: divup(size, 2^e) level-e blocks, and each PAIR of blocks shares one
// bit, so divup(blocks, 2) bits. divup (not plain division) matters for
// non-power-of-two pools, where the last block or pair at a level may be a
// partial/unpaired remainder — rounding up guarantees a bit exists for it.
static size_t mapsize(size_t size, int e) {
  size_t blocksize=e2size(e);
  size_t blocks=divup(size,blocksize);
  size_t buddies=divup(blocks,2);
  return buddies;
}

// bitaddr — convert a block address `mem` at level e into its bit index in
// the level-e pair bitmap.
//   1. baddrclr(base,mem,e) clears bit e of mem's offset from base, which
//      (given mem is already aligned to 2^e) yields the LOW member of mem's
//      buddy pair — the canonical address that represents the whole pair.
//   2. Subtracting base converts that back to a plain byte offset.
//   3. Dividing by the block size (2^e) turns the byte offset into a block
//      INDEX (0, 1, 2, ...) at this level; dividing that by 2 turns the
//      block index into a PAIR index (pairs 0 and 1 -> pair-index 0, pairs
//      2 and 3 -> pair-index 1, etc.) — exactly the bit this pair owns.
static size_t bitaddr(void *base, void *mem, int e) {
  size_t addr=baddrclr(base,mem,e)-base;
  size_t blocksize=e2size(e);
  return addr/blocksize/2;
}

// bbmcreate — allocate a pair bitmap for level e over a pool of `size`
// bytes. All bits start clear, matching an all-free pool (bmcreate() zeros
// its storage), and are only ever set once balloc.c hands a block out of, or
// splits, that level's free list.
extern BBM bbmcreate(size_t size, int e) {
  return bmcreate(mapsize(size,e));
}

// bbmdelete — release a pair bitmap. A BBM is just a BM under the hood, so
// this is a direct pass-through to bmdelete().
extern void bbmdelete(BBM b) {
  bmdelete(b);
}

// bbmset — set the pair-bit that covers mem's pair at level e (mark "at
// least one member of this pair is allocated/unavailable").
extern void bbmset(BBM b, void *base, void *mem, int e) {
  bmset(b,bitaddr(base,mem,e));
}

// bbmclr — clear the pair-bit that covers mem's pair at level e (mark "both
// members of this pair are free"). balloc.c only calls this after directly
// verifying both buddies are on the free list — see bfree()'s merge loop.
extern void bbmclr(BBM b, void *base, void *mem, int e) {
  bmclr(b,bitaddr(base,mem,e));
}

// bbmtst — test the pair-bit that covers mem's pair at level e.
extern int bbmtst(BBM b, void *base, void *mem, int e) {
  return bmtst(b,bitaddr(base,mem,e));
}

// bbmprt — print a pair bitmap (delegates to bm.c's hex dump).
extern void bbmprt(BBM b) { bmprt(b); }

// --- baddr* family: buddy-address arithmetic ------------------------------
// All four operate on the OFFSET of mem from base (mem-base), never on mem
// directly, so the result is always expressed back in terms of base. `mask`
// is a single bit set at position e (1<<e) — the bit that, for two blocks of
// size 2^e, is exactly the bit that differs between a block and its buddy.

// baddrset — return the address of mem's buddy assuming mem is currently the
// LOW half of the pair (forces bit e of the offset to 1, i.e. moves to the
// high half). Only meaningful when mem is known to be level-e aligned.
extern void *baddrset(void *base, void *mem, int e) {
  unsigned int mask=1<<e;
  return base+((mem-base)|mask);
}

// baddrclr — return the address of the LOW member of mem's buddy pair
// (forces bit e of the offset to 0). This is how bitaddr() above finds the
// canonical/shared address for a pair regardless of which half mem is.
extern void *baddrclr(void *base, void *mem, int e) {
  unsigned int mask=~(1<<e);
  return base+((mem-base)&mask);
}

// baddrinv — return the address of mem's BUDDY, whichever half mem is
// (flips bit e of the offset). This is the general "give me my buddy"
// operation; balloc.c's bfree() merge loop uses the equivalent XOR directly
// on the offset rather than calling this helper, but the two are the same
// address arithmetic.
extern void *baddrinv(void *base, void *mem, int e) {
  unsigned int mask=1<<e;
  return base+((mem-base)^mask);
}

// baddrtst — return nonzero if bit e of mem's offset from base is set, i.e.
// whether mem is currently the HIGH half (1) or LOW half (0) of its buddy
// pair at level e.
extern int baddrtst(void *base, void *mem, int e) {
  unsigned int mask=1<<e;
  return (mem-base)&mask;
}