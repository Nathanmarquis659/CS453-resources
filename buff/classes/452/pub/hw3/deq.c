// deq.c -- a double-ended queue (deque), implemented as a doubly linked list.
//
// The shell uses it as a general-purpose list: a pipeline's commands, a
// sequence's pipelines, and the set of background jobs are all Deqs.
//
// The trick that keeps this file short: every node has TWO links, indexed by
// the constants Head and Tail, and every operation takes an "end" argument.
// The head and tail versions of an operation are then the same code, run with
// e = Head or e = Tail. For a node n:
//
//     n->link[Tail]  is the neighbor toward the tail  (the "next" node)
//     n->link[Head]  is the neighbor toward the head  (the "previous" node)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deq.h"
#include "error.h"

enum { Head, Tail };			// which end of the list
#define OTHER(e) (1 - (e))		// Head <-> Tail

typedef struct Node {
  struct Node *link[2];			// link[Head] = previous, link[Tail] = next
  Data data;
} *Node;

typedef struct {
  Node end[2];				// end[Head] = first node, end[Tail] = last
  int len;
} *Rep;

// Turn the opaque Deq handle into our private representation.
static Rep rep(Deq q) {
  if (!q)
    ERROR("null deque");
  return (Rep)q;
}

// Add a new node holding d at end e.
static void put(Rep r, int e, Data d) {
  Node n = malloc(sizeof(*n));
  if (!n)
    ERROR("malloc() failed");
  n->link[Head] = n->link[Tail] = 0;
  n->data = d;

  if (r->len == 0) {
    r->end[Head] = r->end[Tail] = n;	// first node is both head and tail
  } else {
    Node old = r->end[e];		// the node currently at that end
    old->link[e] = n;			// it now points outward to n...
    n->link[OTHER(e)] = old;		// ...and n points back at it
    r->end[e] = n;			// n is the new end
  }
  r->len++;
}

// Remove node n from the list (the node is NOT freed here).
// Each neighbor of n is re-pointed at the node on n's other side. If n has
// no neighbor on some side, then n was that end of the list, so the list's
// end pointer moves inward instead.
static void unlinkNode(Rep r, Node n) {
  for (int s = Head; s <= Tail; s++) {
    if (n->link[s])
      n->link[s]->link[OTHER(s)] = n->link[OTHER(s)];
    else
      r->end[s] = n->link[OTHER(s)];
  }
  r->len--;
}

// Remove and return the data at end e.
static Data get(Rep r, int e) {
  if (r->len == 0)
    ERROR("get from an empty deque");
  Node n = r->end[e];
  Data d = n->data;
  unlinkNode(r, n);
  free(n);
  return d;
}

// Return (without removing) the i-th item counting inward from end e.
static Data ith(Rep r, int e, int i) {
  if (i < 0 || i >= r->len)
    ERROR("index out of range");
  Node n = r->end[e];
  while (i-- > 0)
    n = n->link[OTHER(e)];
  return n->data;
}

// Remove and return the first item that == d, searching inward from end e.
// Returns 0 (and changes nothing) if there is no such item.
static Data rem(Rep r, int e, Data d) {
  for (Node n = r->end[e]; n; n = n->link[OTHER(e)]) {
    if (n->data == d) {
      unlinkNode(r, n);
      free(n);
      return d;
    }
  }
  return 0;
}

extern Deq deq_new() {
  Rep r = malloc(sizeof(*r));
  if (!r)
    ERROR("malloc() failed");
  r->end[Head] = r->end[Tail] = 0;
  r->len = 0;
  return r;
}

extern int deq_len(Deq q) { return rep(q)->len; }

// The public functions are thin wrappers that pick the end.
extern void deq_head_put(Deq q, Data d) {        put(rep(q), Head, d); }
extern Data deq_head_get(Deq q)         { return get(rep(q), Head);    }
extern Data deq_head_ith(Deq q, int i)  { return ith(rep(q), Head, i); }
extern Data deq_head_rem(Deq q, Data d) { return rem(rep(q), Head, d); }

extern void deq_tail_put(Deq q, Data d) {        put(rep(q), Tail, d); }
extern Data deq_tail_get(Deq q)         { return get(rep(q), Tail);    }
extern Data deq_tail_ith(Deq q, int i)  { return ith(rep(q), Tail, i); }
extern Data deq_tail_rem(Deq q, Data d) { return rem(rep(q), Tail, d); }

// Call f on every item, head to tail.
extern void deq_map(Deq q, DeqMapF f) {
  for (Node n = rep(q)->end[Head]; n; n = n->link[Tail])
    f(n->data);
}

// Free the deque. If f is non-null it is called on every item first, so the
// caller can free what the items point to.
extern void deq_del(Deq q, DeqMapF f) {
  if (f)
    deq_map(q, f);
  Node n = rep(q)->end[Head];
  while (n) {
    Node next = n->link[Tail];
    free(n);
    n = next;
  }
  free(q);
}

// Build a space-separated string of the items (f converts one item to a
// malloc'd string; if f is null the items are assumed to be strings already).
extern Str deq_str(Deq q, DeqStrF f) {
  char *s = strdup("");
  for (Node n = rep(q)->end[Head]; n; n = n->link[Tail]) {
    char *item = f ? f(n->data) : n->data;
    char *t;
    if (asprintf(&t, "%s%s%s", s, (*s ? " " : ""), item) < 0)
      ERROR("asprintf() failed");
    free(s);
    s = t;
    if (f)
      free(item);
  }
  return s;
}
