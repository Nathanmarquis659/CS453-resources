#ifndef JOBS_H
#define JOBS_H

// Jobs is the set of background pipelines ("cmd &") that are still running.
// Every child process must eventually be wait()ed for; otherwise it stays
// behind as a "zombie" (finished, but still taking up a process-table slot).

typedef void *Jobs;

#include "Pipeline.h"

extern Jobs newJobs();
extern int sizeJobs(Jobs jobs);

// Add a just-started background pipeline.
extern void addJobs(Jobs jobs, Pipeline pipeline);

// Some child with this pid has been reaped; if it belongs to a background
// job, record that. (Used by foreground waits that reap a job's child.)
extern void creditJobs(Jobs jobs, int pid);

// Without blocking, reap every background child that has finished and free
// the jobs that are completely done.
extern void reapJobs(Jobs jobs);

// Block until all background jobs are done, then free everything.
extern void drainJobs(Jobs jobs);

#endif
