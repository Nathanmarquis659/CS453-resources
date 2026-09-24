// ============================================================================
// bm.c — general-purpose bitmap (PROVIDED MODULE, logic unchanged)
// ============================================================================
// A BM is an opaque handle to a flat array of bits, sized once at creation
// and never resized. Storage layout: one size_t holding the bit count,
// immediately followed by ceil(bits/8) bytes of bit storage. bmcreate()
// hands back a pointer to the byte storage (NOT the struct start); bmbits()
// walks one size_t backward to recover the count. This "hidden header just
// before the data" trick is why every other function can take a bare BM and
// still know its own size, without a separate struct/length pair.
//
// Per HW2 requirement 2, the only memory this module ever touches comes from
// mmalloc()/mmfree() (utils.c's mmap gateway) — bm.c itself calls no
// malloc/free/sbrk/brk. bmcreate() is called exclusively from balloc.c's
// bcreate() (once per level, for both the per-block ablock[] bitmaps and,
// indirectly through bbmcreate(), the buddy-pair bitmaps), so all bitmap
// storage is allocated during a single bcreate() call, consistent with that
// requirement.
// ============================================================================

#include <stdlib.h>
#include <string.h>
#include "bm.h"
#include "utils.h"

// bmbits — recover the bit count stored just before the bitmap's data.
// b points at byte 0 of the bit storage; the size_t immediately preceding it
// (cast back through a size_t*) holds the count passed to bmcreate().
static size_t bmbits(BM b) { size_t *bits=b; return *--bits; }

// bmbytes — number of whole bytes backing this bitmap (ceil(bits/8)).
static size_t bmbytes(BM b) { return bits2bytes(bmbits(b)); }

// ok — bounds-check bit index i against the bitmap's stored bit count.
// A bad index is a programmer error (not a runtime allocation-failure
// condition), so this prints a diagnostic and terminates immediately rather
// than returning an error code that every caller would have to check.
static void ok(BM b, size_t i) {
  if (i<bmbits(b))
    return;
  fprintf(stderr,"bitmap index out of range\n");
  exit(1);
}         

// bmcreate — allocate a bitmap that can hold `bits` bits.
//   * one mmalloc() call gets both the hidden size_t header and the byte
//     storage in a single contiguous block: sizeof(size_t) + bits2bytes(bits).
//   * on out-of-memory (mmalloc returns the -1 sentinel), return 0/NULL so
//     the caller (bcreate) can fail bcreate() cleanly instead of crashing.
//   * the header word is written, the returned handle is advanced past it
//     (BM b = ++p), and the data bytes are zeroed — a fresh bitmap starts
//     with every bit clear.
extern BM bmcreate(size_t bits) {
  size_t bytes=bits2bytes(bits);
  size_t *p=mmalloc(sizeof(size_t)+bytes);
  if ((long)p==-1)
    return 0;
  *p=bits;
  BM b=++p;
  memset(b,0,bytes);
  return b;
}

// bmdelete — free a bitmap created by bmcreate(). Must walk back to the
// hidden header (same trick as bmbits()) to recover the ORIGINAL pointer and
// size that were passed to mmalloc(), because mmfree()/munmap() require the
// exact base address and length of the original mapping.
extern void bmdelete(BM b) {
  size_t *p=b;
  p--;
  mmfree(p,sizeof(size_t)+bits2bytes(*p));
}

// bmset — set bit i to 1. Delegates the actual bit-twiddling to utils.c's
// bitset(), after converting the bitmap-relative index i into a
// byte-offset/bit-within-byte pair (i/bitsperbyte, i%bitsperbyte).
extern void bmset(BM b, size_t i) {
  ok(b,i); bitset(b+i/bitsperbyte,i%bitsperbyte);
}

// bmclr — clear bit i to 0. Same indexing convention as bmset().
extern void bmclr(BM b, size_t i) {
  ok(b,i); bitclr(b+i/bitsperbyte,i%bitsperbyte);
}

// bmtst — return 1 if bit i is set, 0 otherwise. Same indexing convention.
extern int bmtst(BM b, size_t i) {
  ok(b,i); return bittst(b+i/bitsperbyte,i%bitsperbyte);
}

// bmprt — print the bitmap as hex bytes, most-significant byte first, space-
// separated, terminated with a newline. Purely a debugging aid — this is
// what balloc.c's bprint() calls (via bbmprt) to show the buddy-pair bitmaps
// (hw2.pdf requirement 4, the "to-string" tool).
extern void bmprt(BM b) {
  for (int byte=bmbytes(b)-1; byte>=0; byte--)
    printf("%02x%s",((char *)b)[byte],(byte ? " " : "\n"));
}