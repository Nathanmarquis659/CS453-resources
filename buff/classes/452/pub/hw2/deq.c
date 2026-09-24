// deq_str() uses asprintf (a GNU extension) and strdup (POSIX), which are
// not declared under -std=c11; _GNU_SOURCE pulls in both.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deq.h"
#include "error.h"

// indices and size of array of node pointers
typedef enum {Head,Tail,Ends} End;

typedef struct Node {
  struct Node *np[Ends];        // next/prev neighbors
  Data data;
} *Node;

typedef struct {
  Node ht[Ends];                // head/tail nodes
  int len;
} *Rep;

static Rep rep(Deq q) {
  if (!q) ERROR("zero pointer");
  return (Rep)q;
}

// insert a new node holding d at end e; len++
static void put(Rep r, End e, Data d) {
  Node n=(Node)malloc(sizeof(*n));
  if (!n) ERROR("malloc() failed");
  n->data=d;
  if (e==Head) {
    n->np[Head]=0;
    n->np[Tail]=r->ht[Head];
    if (n->np[Tail]) n->np[Tail]->np[Head]=n;
    else r->ht[Tail]=n;          // list was empty
    r->ht[Head]=n;
  } else {
    n->np[Tail]=0;
    n->np[Head]=r->ht[Tail];
    if (n->np[Head]) n->np[Head]->np[Tail]=n;
    else r->ht[Head]=n;          // list was empty
    r->ht[Tail]=n;
  }
  r->len++;
}

// remove and return the node at end e; len-- (ERROR if empty)
static Data get(Rep r, End e) {
  Node n=r->ht[e];
  if (!n) ERROR("get from empty deq");
  Data d=n->data;
  if (e==Head) {
    r->ht[Head]=n->np[Tail];
    if (n->np[Tail]) n->np[Tail]->np[Head]=0;
    else r->ht[Tail]=0;          // was the only node
  } else {
    r->ht[Tail]=n->np[Head];
    if (n->np[Head]) n->np[Head]->np[Tail]=0;
    else r->ht[Head]=0;          // was the only node
  }
  free(n);
  r->len--;
  return d;
}

// return the element i positions in from end e (0-base); len unchanged
static Data ith(Rep r, End e, int i) {
  if (i<0 || i>=r->len) ERROR("ith out of range");
  Node n=r->ht[e];
  for (int k=0; k<i; k++) n=n->np[1-e];
  return n->data;
}

// remove the first node (searching from end e) whose data == d and return
// it; len-- iff found, else returns 0 with len unchanged
static Data rem(Rep r, End e, Data d) {
  Node n=r->ht[e];
  while (n && n->data!=d) n=n->np[1-e];
  if (!n) return 0;
  if (n->np[Head]) n->np[Head]->np[Tail]=n->np[Tail];
  else r->ht[Head]=n->np[Tail];
  if (n->np[Tail]) n->np[Tail]->np[Head]=n->np[Head];
  else r->ht[Tail]=n->np[Head];
  Data x=n->data;
  free(n);
  r->len--;
  return x;
}

extern Deq deq_new() {
  Rep r=(Rep)malloc(sizeof(*r));
  if (!r) ERROR("malloc() failed");
  r->ht[Head]=0;
  r->ht[Tail]=0;
  r->len=0;
  return r;
}

extern int deq_len(Deq q) { return rep(q)->len; }

extern void deq_head_put(Deq q, Data d) {        put(rep(q),Head,d); }
extern Data deq_head_get(Deq q)         { return get(rep(q),Head);   }
extern Data deq_head_ith(Deq q, int i)  { return ith(rep(q),Head,i); }
extern Data deq_head_rem(Deq q, Data d) { return rem(rep(q),Head,d); }

extern void deq_tail_put(Deq q, Data d) {        put(rep(q),Tail,d); }
extern Data deq_tail_get(Deq q)         { return get(rep(q),Tail);   }
extern Data deq_tail_ith(Deq q, int i)  { return ith(rep(q),Tail,i); }
extern Data deq_tail_rem(Deq q, Data d) { return rem(rep(q),Tail,d); }

extern void deq_map(Deq q, DeqMapF f) {
  for (Node n=rep(q)->ht[Head]; n; n=n->np[Tail])
    f(n->data);
}

extern void deq_del(Deq q, DeqMapF f) {
  if (f) deq_map(q,f);
  Node curr=rep(q)->ht[Head];
  while (curr) {
    Node next=curr->np[Tail];
    free(curr);
    curr=next;
  }
  free(q);
}

extern Str deq_str(Deq q, DeqStrF f) {
  char *s=strdup("");
  for (Node n=rep(q)->ht[Head]; n; n=n->np[Tail]) {
    char *d=f ? f(n->data) : n->data;
    char *t; asprintf(&t,"%s%s%s",s,(*s ? " " : ""),d);
    free(s); s=t;
    if (f) free(d);
  }
  return s;
}
