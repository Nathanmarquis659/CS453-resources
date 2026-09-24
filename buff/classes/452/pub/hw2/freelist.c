// ============================================================================
// freelist.c — per-size free lists for the buddy-system allocator (HW2)
// ============================================================================
// See freelist.h for the public contract and the internal helpers used by
// balloc.c. This module is a pure LIST STORE: it holds one singly-linked
// list of free blocks per level e in [l,u] (a level-e block is 2^e bytes)
// and provides push/pop/contains/remove/count. It deliberately does NOT make
// allocation-policy decisions — choosing which block to split, when to merge
// buddies, and maintaining the buddy bitmaps all happen in balloc.c, which
// keeps this module small, testable, and free of bitmap dependencies.
//
// Storage forms (HW2 req. 1b):
//   * Levels e >= PTRLEVEL (block can hold a void*): IN-BAND intrusive list.
//     The first word of every FREE block is the pointer to the next free
//     block of the same size; NULL marks the tail. The per-list HEAD lives in
//     this module's struct (management memory outside the pool region).
//     ALLOCATED blocks carry no management data at all (HW2 req. 1a) — when a
//     block is popped, its first word belongs to the caller.
//   * Levels e < PTRLEVEL: EXTERNAL list — a growable pointer array in
//     management memory, because a 1- or 2-byte block cannot contain a
//     pointer. (Only reachable when l < PTRLEVEL.)
//
// Ordering: push-to-front / pop-from-front (LIFO). Reusing the most recently
// freed block keeps allocation churn and fragmentation low in practice.
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freelist.h"
#include "utils.h" // mmalloc, e2size

// Smallest level whose block (2^e bytes) can hold a void* in-band.
static int ptrlevel(void) {
  int e=0;
  while (e2size(e)<sizeof(void*))
    e++;
  return e;
}

struct SmallList {
  void **items;   // block pointers (management memory)
  size_t count;
  size_t cap;
};

struct FreeList {
  size_t size;             // pool byte size (informational / validation)
  void *base;              // pool base address, set via freelistsetbase()
  int l, u;                // level range
  int n;                   // number of levels (u-l+1)
  void **heads;            // [n] in-band list heads (valid for e >= PTRLEVEL)
  size_t *counts;          // [n] block counts per level
  struct SmallList *small; // [n] external lists (used for e < PTRLEVEL)
};

static int idx(FreeList f, int e) {
  return e-((struct FreeList *)f)->l;
}

// ---------------------------------------------------------------------------
// freelistcreate / freelistdelete / freelistsetbase
// ---------------------------------------------------------------------------

extern FreeList freelistcreate(size_t size, int l, int u) {
  if (size==0 || l<0 || u<l) {
    fprintf(stderr,"freelistcreate: bad arguments (size=%zu l=%d u=%d)\n",
            size,l,u);
    return NULL;
  }
  struct FreeList *fl=mmalloc(sizeof *fl);
  if (fl==NULL || (long)fl==-1) {
    fprintf(stderr,"freelistcreate: out of memory (struct)\n");
    return NULL;
  }
  fl->size=size; fl->base=NULL; fl->l=l; fl->u=u; fl->n=u-l+1;

  // One buffer holds both per-level arrays: heads[] then counts[].
  void *buf=mmalloc(fl->n*(sizeof(void *)+sizeof(size_t)));
  if (buf==NULL || (long)buf==-1) {
    fprintf(stderr,"freelistcreate: out of memory (arrays)\n");
    mmfree(fl,sizeof *fl);
    return NULL;
  }
  fl->heads=(void **)buf;
  fl->counts=(size_t *)(buf+fl->n*sizeof(void *));

  fl->small=mmalloc(fl->n*sizeof(struct SmallList));
  if (fl->small==NULL || (long)fl->small==-1) {
    fprintf(stderr,"freelistcreate: out of memory (small lists)\n");
    mmfree(buf,fl->n*(sizeof(void *)+sizeof(size_t)));
    mmfree(fl,sizeof *fl);
    return NULL;
  }

  for (int e=l;e<=u;e++) {
    fl->heads[idx(fl,e)]=NULL;
    fl->counts[idx(fl,e)]=0;
    if (e<ptrlevel()) {
      fl->small[idx(fl,e)].items=NULL;
      fl->small[idx(fl,e)].count=0;
      fl->small[idx(fl,e)].cap=0;
    }
  }
  return fl;
}

extern void freelistdelete(FreeList f, int l, int u) {
  if (f==NULL)
    return;
  struct FreeList *fl=f;
  for (int e=l;e<=u;e++)
    if (e<ptrlevel() && fl->small[idx(f,e)].items)
      mmfree(fl->small[idx(f,e)].items,fl->small[idx(f,e)].cap*sizeof(void *));
  mmfree(fl->small,fl->n*sizeof(struct SmallList));
  mmfree((void *)fl->heads,fl->n*(sizeof(void *)+sizeof(size_t)));
  mmfree(fl,sizeof *fl);
}

// freelistsetbase — record the pool's base address so freelistprint can show
// offsets. Called once by balloc after creation (the lists are created before
// the region is known).
extern void freelistsetbase(FreeList f, void *base) {
  ((struct FreeList *)f)->base=base;
}

// freelistptrlevel — see freelist.h.
extern int freelistptrlevel(void) {
  return ptrlevel();
}

// ---------------------------------------------------------------------------
// Internal helpers (freelist.h): peek / count / contains / remove
// ---------------------------------------------------------------------------

extern void *freelistpeek(FreeList f, int e) {
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u)
    return NULL;
  if (e>=ptrlevel())
    return fl->heads[idx(f,e)];
  struct SmallList *s=&fl->small[idx(f,e)];
  return s->count ? s->items[0] : NULL;   // front of the external list
}

extern size_t freelistcount(FreeList f, int e) {
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u)
    return 0;
  return fl->counts[idx(f,e)];
}

extern int freelistcontains(FreeList f, void *mem, int e) {
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u || mem==NULL)
    return 0;
  if (e>=ptrlevel()) {
    for (void *p=fl->heads[idx(f,e)]; p; p=*(void **)p)
      if (p==mem)
        return 1;
    return 0;
  }
  struct SmallList *s=&fl->small[idx(f,e)];
  for (size_t i=0;i<s->count;i++)
    if (s->items[i]==mem)
      return 1;
  return 0;
}

extern int freelistremove(FreeList f, void *mem, int e) {
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u || mem==NULL)
    return 0;
  if (e>=ptrlevel()) {
    // Unlink from the intrusive singly-linked list.
    void **pp=&fl->heads[idx(f,e)];
    while (*pp) {
      if (*pp==mem) {
        *pp=*(void **)*pp;
        fl->counts[idx(f,e)]--;
        return 1;
      }
      pp=(void **)*pp;
    }
    return 0;
  }
  struct SmallList *s=&fl->small[idx(f,e)];
  for (size_t i=0;i<s->count;i++) {
    if (s->items[i]==mem) {
      s->items[i]=s->items[s->count-1];   // swap-remove; order not meaningful
      s->count--;
      fl->counts[idx(f,e)]--;
      return 1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Public API: alloc (pop) / free (push) / size (count) / print
// ---------------------------------------------------------------------------

extern void *freelistalloc(FreeList f, void *base, int e, int l) {
  (void)base; (void)l;
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u)
    return NULL;
  if (e>=ptrlevel()) {
    void *p=fl->heads[idx(f,e)];
    if (p==NULL)
      return NULL;
    fl->heads[idx(f,e)] = *(void **)p;   // follow the in-band next pointer
    fl->counts[idx(f,e)]--;
    return p;                            // caller now owns the block's bytes
  }
  struct SmallList *s=&fl->small[idx(f,e)];
  if (s->count==0)
    return NULL;
  void *p=s->items[0];                   // pop from the front
  s->items[0]=s->items[s->count-1];
  s->count--;
  fl->counts[idx(f,e)]--;
  return p;
}

extern void freelistfree(FreeList f, void *base, void *mem, int e, int l) {
  (void)base; (void)l;
  struct FreeList *fl=f;
  if (e<fl->l || e>fl->u || mem==NULL) {
    fprintf(stderr,"freelistfree: bad level or NULL pointer\n");
    exit(1);
  }
  if (e>=ptrlevel()) {
    *(void **)mem=fl->heads[idx(f,e)];   // in-band next pointer (HW2 req. 1b)
    fl->heads[idx(f,e)]=mem;             // push to front
    fl->counts[idx(f,e)]++;
    return;
  }
  struct SmallList *s=&fl->small[idx(f,e)];
  if (s->count==s->cap) {
    size_t ncap=s->cap ? s->cap*2 : 4;
    void **ni=mmalloc(ncap*sizeof(void *));
    if (ni==NULL || (long)ni==-1) {
      fprintf(stderr,"freelistfree: out of memory growing L%d\n",e);
      exit(1);
    }
    if (s->items) {
      memcpy(ni,s->items,s->count*sizeof(void *));
      mmfree(s->items,s->cap*sizeof(void *));
    }
    s->items=ni;
    s->cap=ncap;
  }
  s->items[s->count++]=mem;              // append == front-push (LIFO)
  fl->counts[idx(f,e)]++;
}

// freelistsize — number of free blocks on L_e. (The base/mem parameters are
// kept for interface compatibility with the assignment's header; the count is
// a per-level property.)
extern int freelistsize(FreeList f, void *base, void *mem, int l, int u) {
  (void)base; (void)mem; (void)l; (void)u;
  struct FreeList *fl=f;
  size_t total=0;
  for (int e=fl->l;e<=fl->u;e++)
    total+=fl->counts[idx(f,e)];
  return (int)total;
}

// freelistprint — dump every free list to stdout. Offsets are shown relative
// to the pool base when it has been set. NULL is a documented no-op.
extern void freelistprint(FreeList f, int l, int u) {
  if (f==NULL) {
    printf("FreeList: NULL\n");
    return;
  }
  struct FreeList *fl=f;
  char *base=(char *)fl->base;
  for (int e=l;e<=u;e++) {
    printf("L%d(%zuB): ",e,e2size(e));
    if (e>=ptrlevel()) {
      int first=1;
      for (void *p=fl->heads[idx(f,e)]; p; p=*(void **)p) {
        printf("%s%s%td",first ? "" : " -> ",base ? "+" : "",
               base ? (char *)p-base : (long)p);
        first=0;
      }
    } else {
      struct SmallList *s=&fl->small[idx(f,e)];
      for (size_t i=0;i<s->count;i++)
        printf("%s%s%td",i ? " -> " : "",base ? "+" : "",
               base ? (char *)s->items[i]-base : (long)s->items[i]);
    }
    if (fl->counts[idx(f,e)]==0)
      printf("empty");
    printf(" .\n");
  }
}
