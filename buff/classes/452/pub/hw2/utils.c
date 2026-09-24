// ============================================================================
// utils.c — low-level helpers for the HW2 buddy-system allocator
// ============================================================================
// This module is the leaf of the dependency stack: every other module
// (bm, bbm, freelist, balloc) calls into it, but it calls into none of them.
// It provides four families of functions:
//
//   1. Memory      mmalloc / mmfree — the ONLY place in the whole allocator
//                  that touches mmap()/munmap(). HW2 requirement 2 mandates
//                  that mmap be called only via mmalloc and only during a
//                  bcreate() call; enforcing it here keeps that rule in one
//                  auditable spot.
//   2. Arithmetic  divup / bits2bytes — ceiling division and bit/byte
//                  conversion, used by the bitmap modules to size storage.
//   3. Level math  e2size / size2e — convert between a block's exponent e
//                  (block size = 2^e) and its byte size. This is the
//                  "round a request up to the next power of two" primitive
//                  that balloc() uses.
//   4. Bit ops     bitset / bitclr / bitinv / bittst — single-bit operations
//                  on an arbitrary byte array, used by bm.c (which treats
//                  its bitmap as a flat array of bytes).
// ============================================================================

// _GNU_SOURCE: MAP_ANONYMOUS is a GNU/BSD extension, not POSIX, so it is
// hidden by <sys/mman.h> under -std=c11 (strict ISO C); _GNU_SOURCE pulls it
// in. Same pattern as deq.c and tests.c.
#define _GNU_SOURCE

#include <stdlib.h>
#include <sys/mman.h>
#include "utils.h"

// ---------------------------------------------------------------------------
// 1. Memory: mmap wrappers
// ---------------------------------------------------------------------------

// mmalloc — allocate `size` bytes of anonymous private memory via mmap().
// This is the single gateway through which the allocator obtains its pool
// (and, transitively, the storage for bmcreate()'s bitmaps).
//
//   Returns a pointer to at least `size` usable bytes on success.
//   Returns (void *)-1 on failure, matching the convention used by the
//   provided bm.c/bbm.c ((long)p==-1 checks). mmap() itself returns
//   MAP_FAILED; we normalize it to -1 so every caller can use one test.
//   A valid mapping is never (void *)-1.
extern void *mmalloc(size_t size) {
  // Round up to a whole number of bytes; mmap takes any size but keeping
  // this explicit documents intent and guards against a zero-size call.
  if (size == 0)
    size = 1;
  void *p=mmap(0,size,
               PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS,
               -1,0);
  if (p==MAP_FAILED) {
    fprintf(stderr,"mmalloc: mmap failed for %zu bytes\n",size);
    return (void *)-1;
  }
  return p;
}

// mmfree — release memory previously obtained from mmalloc().
// Must be called with the exact pointer and size passed to mmalloc()
// (munmap requires the original mapping boundaries). NULL or the failure
// sentinel (void *)-1 are ignored, so double-mmfree of a failed allocation
// is harmless.
extern void mmfree(void *p, size_t size) {
  if (p==NULL || p==(void *)-1)
    return;
  if (size == 0)
    size = 1;
  munmap(p,size);
}

// ---------------------------------------------------------------------------
// 2. Arithmetic: ceiling division and bit/byte conversion
// ---------------------------------------------------------------------------

// divup — ceiling of n/d for positive integers: "how many d-sized chunks do
// I need to cover n items?" Example: divup(16,4)=4, divup(17,4)=5.
// Used everywhere a count must be rounded up to whole units (e.g., how many
// bytes are needed to hold N bits). Requires d>0.
extern size_t divup(size_t n, size_t d) {
  return (n+d-1)/d;
}

// bits2bytes — number of whole bytes needed to store `bits` bits:
// the ceiling of bits/8. Example: bits2bytes(1)=1, bits2bytes(9)=2.
extern size_t bits2bytes(size_t bits) {
  return divup(bits,bitsperbyte);
}

// ---------------------------------------------------------------------------
// 3. Level math: exponent <-> byte size
// ---------------------------------------------------------------------------

// e2size — the byte size of a block at level e, i.e., 2^e.
// Example: e2size(0)=1, e2size(3)=8.
extern size_t e2size(int e) {
  return (size_t)1<<e;
}

// size2e — the exponent of the smallest power of two that is >= size,
// i.e., ceil(log2(size)). This is the "round the request up" step of
// balloc(): a request for 3 bytes becomes level 2 (4 bytes).
//   Example: size2e(1)=0, size2e(2)=1, size2e(3)=2, size2e(4)=2,
//            size2e(5)=3.
// Requires size>0. Implementation: clear bits from the top until a power of
// two remains; if the result is strictly smaller than the original, one more
// doubling is needed.
extern int size2e(size_t size) {
  // Guard: undefined for 0 in this module's contract (balloc clamps first).
  if (size==0)
    return 0;
  size_t x=size;
  // Fold: repeatedly move the highest set bit down until x is a power of two.
  while (x>1) {
    // Find position of highest set bit by shifting down.
    size_t h=1;
    while ((h<<1)<=x)
      h<<=1;
    if (h==x)
      break;              // x is a power of two
    x=h;                  // truncate to that power of two and re-check
  }
  if (x<size)
    x<<=1;                // not an exact power of two: round up one level
  int e=0;
  while (x>1) { x>>=1; e++; }
  return e;
}

// ---------------------------------------------------------------------------
// 4. Bit operations on a byte array
// ---------------------------------------------------------------------------
// These operate on BIT `bit` of the byte ARRAY starting at address `p`, where
// bit 0 is the LEAST-significant bit of the first byte, bit 7 the MSB of the
// first byte, bit 8 the LSB of the second byte, and so on. Callers may pass
// any non-negative bit index; the function computes which byte of the array it
// lives in (bit/bitsperbyte) and the position within that byte (bit %
// bitsperbyte). bm.c relies on exactly this convention when it indexes its
// bitmap as "byte i/bitsperbyte, bit i%bitsperbyte".

// bitset — set bit `bit` in the array at p.
extern void bitset(void *p, int bit) {
  unsigned char *b=p+bit/bitsperbyte;
  *b |= (unsigned char)(1<<(bit%bitsperbyte));
}

// bitclr — clear bit `bit` in the array at p.
extern void bitclr(void *p, int bit) {
  unsigned char *b=p+bit/bitsperbyte;
  *b &= (unsigned char)~(1<<(bit%bitsperbyte));
}

// bitinv — invert bit `bit` in the array at p.
extern void bitinv(void *p, int bit) {
  unsigned char *b=p+bit/bitsperbyte;
  *b ^= (unsigned char)(1<<(bit%bitsperbyte));
}

// bittst — return 1 if bit `bit` of the array at p is set, else 0.
extern int bittst(void *p, int bit) {
  unsigned char *b=p+bit/bitsperbyte;
  return (*b & (unsigned char)(1<<(bit%bitsperbyte))) != 0;
}
