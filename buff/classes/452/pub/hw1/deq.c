#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deq.h"
#include "error.h"

// indices and size of array of node pointers; 0,1,2
typedef enum {Head,Tail,Ends} End;

// Helper function to get the Inverse end (i.e. Head and Tail)
static int inverseEnd(int e)
{
  if (e == Head) return Tail;
  if (e == Tail) return Head;
  else return -1;
}

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

static void put(Rep r, End e, Data d)
{
  // Create Node from Data
  Node newNode = (Node)malloc(sizeof(*newNode));
  if (!newNode) ERROR("malloc() failed");
  // Set Node->np head/tail = 0
  newNode->np[Head] = 0;
  newNode->np[Tail] = 0;
  // Set Data = Data
  newNode->data = d;
  // if Rep length is 0
  if (r->len == 0)
  {
    // Rep->ht[Head] points to Node
    r->ht[Head] = newNode;
    // Rep->ht[Tail] points to Node
    r->ht[Tail] = newNode;
  }
  // else
  else {
    // Rep->ht[End]->np[End] points to Node (last element now points to Node)
    r->ht[e]->np[e] = newNode;
    // Node->np[InverseEnd] points to Rep->ht[End]
    newNode->np[inverseEnd(e)] = r->ht[e];
    // Rep->ht[End] points to Node (Node is now the new End)
    r->ht[e] = newNode;
  }
  // Rep length ++
  r->len++;
}

static Data ith(Rep r, End e, int i)
{
  // Check if index > length-1 (out of bounds)
  if (i > r->len - 1 || i < 0)
  {
    ERROR("Index out of bounds");
  }

  Node n = r->ht[e];
  // From end, move inward for index steps
  for (int j = 0; j < i; j++)
  {
    n = n->np[inverseEnd(e)];
  }
  // Return Node->data at index
  return n->data;
}

static Data get(Rep r, End e)
{
  // Check if length is 0
  if (r->len == 0) ERROR("Empty queue");
  // lastNode = pointer to Rep->ht[End]
  Node lastNode = r->ht[e];
  Node prevNode = r->ht[e]->np[inverseEnd(e)];
  // oldData = lastNode->data
  Data oldData = lastNode->data;
  // if Rep length = 1
  if (r->len == 1)
  {
    // Rep->ht[Head] = 0
    r->ht[Head] = 0;
    // Rep->ht[Tail] = 0
    r->ht[Tail] = 0;
  }
  else
  {
    // Rep->ht[End]->np[InverseEnd]->np[End] = 0 (Get End Node -> find previous Node ->  prevNode = End and references 0)
    prevNode->np[e] = 0;
    // Rep->ht[End] points to Rep->ht[End]->np[InverseEnd] (Assign previous Node as the new End)
    r->ht[e] = prevNode;
  }
  // Rep length --
  r->len--;
  // free(oldNode)
  free(lastNode);
  // return oldData
  return oldData;
}

static Data rem(Rep r, End e, Data d)
{
  // if length = 0 return 0
  if (r->len == 0) return 0;

  Node n = r->ht[e];
  // From end, move inward for index steps
  while (n && n->data != d)
  {
    n = n->np[inverseEnd(e)];
  }
  if (!n) return 0; // Element not found

    if (n == r->ht[Head] && n == r->ht[Tail]) // This means we are the only node, cleanup to length 0
    {
      // Rep->ht = 0 for head and tail
      r->ht[Head] = 0;
      r->ht[Tail] = 0;
    } else if (n == r->ht[Head]) // This means n is head
    {
      // r->ht[Head] = next
      r->ht[Head] = n->np[Tail];
      // next->np[Head] = 0
      r->ht[Head]->np[Head] = 0;
    } else if (n == r->ht[Tail]) // This means n is tail
    {
      // r->ht[Tail] = prev
      r->ht[Tail] = n->np[Head];
      // prev->np[Tail] = 0
      r->ht[Tail]->np[Tail] = 0;
    } else // n is in the middle; not head or tail
    {
      // prev->np[Tail] = next
      n->np[Head]->np[Tail] = n->np[Tail];
      // next->np[Head] = tail
      n->np[Tail]->np[Head] = n->np[Head];
    }

  // r length --
  r->len--;
  // Save n_data from n
  Data n_data = n->data;
  // Free n
  free(n);
  // return n_data
  return n_data;
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
