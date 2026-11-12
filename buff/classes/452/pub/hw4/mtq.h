#ifndef MTQ_H
#define MTQ_H

#include "deq.h"

// MT-safe wrapper around a Deq. One mutex guards the whole queue.
// Operations that cannot proceed block on condition variables:
//   put blocks while the queue is full   (only if capacity > 0)
//   get blocks while the queue is empty

typedef void *Mtq;

// Create a queue holding at most cap items; cap == 0 means unbounded.
extern Mtq mtq_new(int cap);

extern void mtq_head_put(Mtq q, Data d);
extern Data mtq_head_get(Mtq q);
extern void mtq_tail_put(Mtq q, Data d);
extern Data mtq_tail_get(Mtq q);

// Current number of items (a snapshot; it may change right after return).
extern int mtq_len(Mtq q);

// Destroy the queue. Calls f on each remaining item if f is non-null.
// No other thread may be using the queue.
extern void mtq_del(Mtq q, DeqMapF f);

#endif
