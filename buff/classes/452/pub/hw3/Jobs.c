// Jobs.c -- keeping track of background pipelines until they finish.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "Jobs.h"
#include "deq.h"
#include "error.h"

// A Jobs object is just a deque of Pipelines.

extern Jobs newJobs() {
  return deq_new();
}

extern int sizeJobs(Jobs jobs) {
  return deq_len(jobs);
}

extern void addJobs(Jobs jobs, Pipeline pipeline) {
  deq_tail_put(jobs, pipeline);
}

// Tell whichever job owns `pid` that its child is gone.
extern void creditJobs(Jobs jobs, int pid) {
  for (int i = 0; i < sizeJobs(jobs); i++)
    if (reapPid(deq_head_ith(jobs, i), pid))
      return;
}

// Free every job whose children have all been reaped.
static void removeFinished(Jobs jobs) {
  int i = 0;
  while (i < sizeJobs(jobs)) {
    Pipeline job = deq_head_ith(jobs, i);
    if (pendingPipeline(job) == 0) {
      deq_head_rem(jobs, job);		// the next job slides into slot i
      freePipeline(job);
    } else {
      i++;
    }
  }
}

extern void reapJobs(Jobs jobs) {
  // WNOHANG = "don't block": return 0 right away if no child has finished.
  int pid;
  while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    creditJobs(jobs, pid);
  removeFinished(jobs);
}

extern void drainJobs(Jobs jobs) {
  removeFinished(jobs);
  while (sizeJobs(jobs) > 0) {
    int pid = waitpid(-1, NULL, 0);	// block until some child exits
    if (pid < 0)
      break;				// no children left (shouldn't happen)
    creditJobs(jobs, pid);
    removeFinished(jobs);
  }
  deq_del(jobs, freePipeline);
}
