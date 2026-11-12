#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "thr.h"
#include "error.h"

typedef struct {
  int n;            // number of threads that were actually started
  pthread_t *ids;   // their ids, needed later by pthread_join
} *Rep;

extern Thr thr_start(int n, ThrF f, void *arg) {
  if (n < 0) ERROR("negative thread count: %d", n);
  Rep r = (Rep)malloc(sizeof(*r));
  if (!r) ERROR("malloc() failed");
  r->ids = (pthread_t *)malloc((n ? n : 1) * sizeof(pthread_t));
  if (!r->ids) ERROR("malloc() failed");
  r->n = 0;
  for (int i = 0; i < n; i++) {
    int e = pthread_create(&r->ids[i], 0, f, arg);
    if (e) ERROR("pthread_create() failed: %s", strerror(e));
    r->n++;
  }
  return r;
}

extern void thr_join(Thr t) {
  if (!t) ERROR("zero pointer");
  Rep r = (Rep)t;
  for (int i = 0; i < r->n; i++) {
    int e = pthread_join(r->ids[i], 0);
    if (e) ERROR("pthread_join() failed: %s", strerror(e));
  }
  free(r->ids);
  free(r);
}
