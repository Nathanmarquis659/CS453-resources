// ============================================================================
// freelist.h — per-size free lists for the buddy-system allocator (HW2)
// ============================================================================
// This module owns the FREE-LIST half of the buddy system: one singly-linked
// list of free blocks per level e in [l,u], where a level-e block is 2^e
// bytes. The ALLOCATION POLICY (which block to pick, when to split, when to
// merge) lives in balloc.c; this module only stores and retrieves blocks.
//
// Management-data policy (HW2 req. 1a/1b):
//   * ALLOCATED blocks carry NO management data — the caller receives the
//     full 2^e bytes.
//   * FREE blocks of size >= sizeof(void*) (levels e >= PTRLEVEL, 3 on this
//     platform) carry their list link IN-BAND: the first word of every free
//     block is a pointer to the next free block of the same size (NULL at
//     the tail). The per-list HEAD pointers live in the FreeList struct,
//     which is management memory outside the pool region.
//   * Levels below PTRLEVEL cannot physically hold a pointer inside their
//     blocks, so those lists use external storage (a growable pointer array
//     in management memory). Both forms are "a list of free blocks of that
//     size"; the in-band form is used wherever it fits.
//
// List order: pushes go to the FRONT and pops come from the FRONT (LIFO),
// so repeated alloc/free cycles reuse the most recently freed block — good
// for cache locality and for keeping the split/merge churn small.
// ============================================================================

#ifndef FREELIST_H
#define FREELIST_H

#include <stdio.h>

typedef void *FreeList;

// freelistcreate — build the list-management structure for a pool of `size`
// bytes with levels l..u (l <= u, both >= 0). All lists start EMPTY; the
// caller seeds them via freelistfree() (balloc.c does this by decomposing
// `size` into powers of two — HW2 req. 3). Returns NULL on bad arguments or
// out-of-memory.
extern FreeList freelistcreate(size_t size, int l, int u);

// freelistdelete — release the struct and any external list storage.
extern void     freelistdelete(FreeList f, int l, int u);

// freelistalloc — pop and return the front block of L_e, or NULL if empty.
// The returned block's in-band word is left as-is: the caller now owns
// those bytes (HW2 req. 1a).
extern void *freelistalloc(FreeList f, void *base, int e, int l);

// freelistfree — push `mem` onto the front of L_e. For in-band levels the
// next pointer is written into the block's own first word (HW2 req. 1b).
extern void  freelistfree(FreeList f, void *base, void *mem, int e, int l);

// freelistsize — total number of free blocks across ALL levels [l,u].
// (The base/mem parameters are kept for interface compatibility with the
// assignment's header; the count is a whole-pool property.)
extern int   freelistsize(FreeList f, void *base, void *mem, int l, int u);

// freelistprint — dump every free list to stdout (debugging / bprint).
extern void  freelistprint(FreeList f, int l, int u);

// ---------------------------------------------------------------------------
// INTERNAL interface — used by balloc.c and the tests to inspect a free list
// without going through the public push/pop API. Not part of the assignment's
// required interface; they exist for modularity so that balloc.c never
// touches the list internals directly. All functions take the level e
// explicitly and validate it against the range stored at creation time.
// ---------------------------------------------------------------------------

// Head of the free list for level e (NULL if the list is empty).
extern void *freelistpeek(FreeList f, int e);

// Number of blocks currently on the free list for level e.
extern size_t freelistcount(FreeList f, int e);

// 1 if `mem` is on the free list for level e (pointer identity), else 0.
extern int freelistcontains(FreeList f, void *mem, int e);

// Remove `mem` from the free list for level e. Returns 1 if found and
// removed, 0 otherwise.
extern int freelistremove(FreeList f, void *mem, int e);

// Record the pool's base address so freelistprint can show offsets (set once
// by balloc after creation).
extern void freelistsetbase(FreeList f, void *base);

// Smallest level e whose block can hold a void* in-band. Levels below this
// use external storage, so callers must NOT follow the in-band next pointer
// (the first word of the block) for them.
extern int freelistptrlevel(void);

#endif
