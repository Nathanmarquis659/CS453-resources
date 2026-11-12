#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "mtq.h"
#include "error.h"

typedef enum {Head, Tail} End;

typedef struct {
  Deq q;                  // the MT-unsafe queue; touch only with lock held
  int cap;                // maximum length; 0 means unbounded
  pthread_mutex_t lock;   // the one lock guarding q
  pthread_cond_t notfull; // signaled when an item is removed
  pthread_cond_t notempty;// signaled when an item is added
} *Rep;

static Rep rep(Mtq q) {
  if (!q) ERROR("zero pointer");
  return (Rep)q;
}

static void check(int e, const char *what) {
  if (e) ERROR("%s failed: %s", what, strerror(e));
}

static void lock(Rep r)   { check(pthread_mutex_lock(&r->lock), "pthread_mutex_lock()"); }
static void unlock(Rep r) { check(pthread_mutex_unlock(&r->lock), "pthread_mutex_unlock()"); }

extern Mtq mtq_new(int cap) {
  if (cap < 0) ERROR("negative capacity: %d", cap);
  Rep r = (Rep)malloc(sizeof(*r));
  if (!r) ERROR("malloc() failed");
  r->q = deq_new();
  r->cap = cap;
  check(pthread_mutex_init(&r->lock, 0), "pthread_mutex_init()");
  check(pthread_cond_init(&r->notfull, 0), "pthread_cond_init()");
  check(pthread_cond_init(&r->notempty, 0), "pthread_cond_init()");
  return r;
}

static void put(Rep r, End e, Data d) {
  lock(r);
  // while, not if: another putter may fill the slot before we run again
  while (r->cap && deq_len(r->q) >= r->cap)
    check(pthread_cond_wait(&r->notfull, &r->lock), "pthread_cond_wait()");
  if (e == Head) deq_head_put(r->q, d); else deq_tail_put(r->q, d);
  check(pthread_cond_signal(&r->notempty), "pthread_cond_signal()");
  unlock(r);
}

static Data get(Rep r, End e) {
  lock(r);
  // deq_*_get is a fatal error on an empty deque, so we must wait here
  while (deq_len(r->q) == 0)
    check(pthread_cond_wait(&r->notempty, &r->lock), "pthread_cond_wait()");
  Data d = (e == Head) ? deq_head_get(r->q) : deq_tail_get(r->q);
  check(pthread_cond_signal(&r->notfull), "pthread_cond_signal()");
  unlock(r);
  return d;
}

extern void mtq_head_put(Mtq q, Data d) {        put(rep(q), Head, d); }
extern Data mtq_head_get(Mtq q)         { return get(rep(q), Head);    }
extern void mtq_tail_put(Mtq q, Data d) {        put(rep(q), Tail, d); }
extern Data mtq_tail_get(Mtq q)         { return get(rep(q), Tail);    }

extern int mtq_len(Mtq q) {
  Rep r = rep(q);
  lock(r);
  int n = deq_len(r->q);
  unlock(r);
  return n;
}

extern void mtq_del(Mtq q, DeqMapF f) {
  Rep r = rep(q);
  deq_del(r->q, f);
  check(pthread_cond_destroy(&r->notfull), "pthread_cond_destroy()");
  check(pthread_cond_destroy(&r->notempty), "pthread_cond_destroy()");
  check(pthread_mutex_destroy(&r->lock), "pthread_mutex_destroy()");
  free(r);
}
