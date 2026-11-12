#ifndef THR_H
#define THR_H

// Thread helpers: run one function in n threads, then wait for them.

typedef void *Thr;                // opaque handle for a group of threads
typedef void *(*ThrF)(void *arg); // thread body, as pthread_create expects

// Start n new threads, each running f(arg). All threads receive the
// same arg pointer, so arg (and what it points to) must stay valid
// until thr_join() returns, and must be safe to share between threads.
// Exits with an error message if memory or a thread cannot be created.
extern Thr thr_start(int n, ThrF f, void *arg);

// Wait for every thread in the group to finish, then free the handle.
// The handle must not be used afterward.
extern void thr_join(Thr t);

#endif
