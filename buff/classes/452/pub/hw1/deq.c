#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deq.h"
#include "error.h"

// indices and size of array of node pointers; 0,1,2
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

// depending on 'end', append the provided data
static void put(Rep r, End e, Data d)
{
  // Create Node from Data
      // Set Node->np head/tail = 0
      // Set Data = Data
  // if Rep length is 0
      // Rep->ht[Head] points to Node
      // Rep->ht[Tail] points to Node
  // else
      // Rep->ht[End]->np[End] points to Node
      // Node->np[InverseEnd] points to Rep->ht[End]
      // Rep->ht[End] points to Node (Node is now the new End)
  // Rep length ++
}
static Data ith(Rep r, End e, int i)
{

  return 0;
}
static Data get(Rep r, End e)
{
  // if Rep length = 0
      // return 0
  // oldNode = pointer to Rep->ht[End]
  // oldData = oldNode->data
  // if Rep length = 1
      // Rep->ht[Head] = 0
      // Rep->ht[Tail] = 0
  // else
      // Rep->ht[End]->np[InverseEnd]->np[End] = 0 (Get End, find previous Node and set it's End to 00
      // Rep->ht[End] points to Rep->ht[End]->np[InverseEnd] (Assign previous Node as the new End)
  // Rep length --
  // free(oldNode)
  // return oldData

  return 0; // FIXME placeholder
}
static Data rem(Rep r, End e, Data d)
{

  return 0;
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
