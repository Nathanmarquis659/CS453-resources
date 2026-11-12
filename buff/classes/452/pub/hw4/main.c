#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "lawn.h"
#include "mole.h"
#include "mtq.h"
#include "thr.h"

// Each thread receives {Mtq, Lawn}. The array is only read, so all
// threads can safely share it.

// Producer: create one mole and put it on the queue. Blocks if the
// queue is full.
static void *produce(void *a) {
  void **arg = a;
  Mtq q = (Mtq)arg[0];
  Lawn l = (Lawn)arg[1];
  mtq_tail_put(q, mole_new(l, 0, 0));
  return 0;
}

// Consumer: take one mole off the queue (blocking if empty), whack it.
static void *consume(void *a) {
  void **arg = a;
  Mtq q = (Mtq)arg[0];
  mole_whack((Mole)mtq_head_get(q));
  return 0;
}

int main() {
  srandom(time(0));
  const int n = 10;     // moles to produce and consume
  const int cap = 4;    // queue capacity; small, to cause congestion
  Lawn lawn = lawn_new(0, 0);
  Mtq q = mtq_new(cap);
  void *arg[] = { q, lawn };

  Thr p = thr_start(n, produce, arg);
  Thr c = thr_start(n, consume, arg);
  thr_join(p);
  thr_join(c);

  mtq_del(q, 0);        // empty by now: n puts, n gets
  lawn_free(lawn);
}
