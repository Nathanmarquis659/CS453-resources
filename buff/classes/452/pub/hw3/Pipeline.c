// Pipeline.c -- running a pipeline: fork one child per command, connect them
// with pipes, and (for foreground pipelines) wait for them all to finish.
//
// For   a | b | c   the shell makes two pipes and three children:
//
//     a ---pipe 0---> b ---pipe 1---> c
//
// A pipe is a pair of file descriptors, [0] to read from and [1] to write to.
// Bytes written into [1] come out of [0]. Command i's stdout is the write end
// of pipe i, and command i+1's stdin is the read end of pipe i.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "Pipeline.h"
#include "deq.h"
#include "error.h"

typedef struct {
  Deq commands;		// the Commands, left to right
  int fg;		// 1 = foreground (shell waits), 0 = background ("&")
  int *pids;		// pids[i] = child running command i; set to 0 once reaped
  int pending;		// children forked but not yet reaped
} *PipelineRep;

extern Pipeline newPipeline(int fg) {
  PipelineRep r = malloc(sizeof(*r));
  if (!r)
    ERROR("malloc() failed");
  r->commands = deq_new();
  r->fg = fg;
  r->pids = 0;
  r->pending = 0;
  return r;
}

extern void addPipeline(Pipeline pipeline, Command command) {
  PipelineRep r = pipeline;
  deq_tail_put(r->commands, command);
}

extern int sizePipeline(Pipeline pipeline) {
  PipelineRep r = pipeline;
  return deq_len(r->commands);
}

extern int pendingPipeline(Pipeline pipeline) {
  PipelineRep r = pipeline;
  return r->pending;
}

extern int reapPid(Pipeline pipeline, int pid) {
  PipelineRep r = pipeline;
  for (int i = 0; i < sizePipeline(r); i++) {
    if (r->pids && r->pids[i] == pid) {
      r->pids[i] = 0;
      r->pending--;
      return 1;
    }
  }
  return 0;
}

// Close both ends of every pipe.
static void closeAll(int (*pipes)[2], int npipes) {
  for (int i = 0; i < npipes; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
}

// Runs in the child for command number i (of n). Never returns.
static void runChild(PipelineRep r, int i, int (*pipes)[2]) {
  int n = sizePipeline(r);

  if (i > 0)
    dup2(pipes[i - 1][0], 0);		// stdin  <- read end of the previous pipe
  if (i < n - 1)
    dup2(pipes[i][1], 1);		// stdout -> write end of the next pipe

  // Now stdin/stdout are aliases of the pipe ends we need, so the original
  // pipe descriptors are redundant. We MUST close them: a reader (like cat)
  // only sees end-of-file once EVERY copy of the write end is closed, so a
  // stray copy in any process would make the pipeline hang forever.
  closeAll(pipes, n - 1);

  execChild(deq_head_ith(r->commands, i));
}

// Fork one child per command.
static void startChildren(PipelineRep r) {
  int n = sizePipeline(r);
  int (*pipes)[2] = calloc(n, sizeof(int[2]));	// n-1 are used
  r->pids = calloc(n, sizeof(int));
  if (!pipes || !r->pids)
    ERROR("calloc() failed");

  for (int i = 0; i < n - 1; i++)
    if (pipe(pipes[i]) < 0)
      ERROR("pipe() failed");

  // A child inherits a copy of our unflushed stdio buffers; flush first so
  // nothing gets printed twice.
  fflush(NULL);

  for (int i = 0; i < n; i++) {
    int pid = fork();
    if (pid < 0)
      ERROR("fork() failed");
    if (pid == 0)
      runChild(r, i, pipes);		// the child never comes back
    r->pids[i] = pid;			// the parent remembers the child
    r->pending++;
  }

  // The parent uses none of the pipes. Close them now, for the same
  // end-of-file reason as above.
  closeAll(pipes, n - 1);
  free(pipes);
}

// Wait until every child of this pipeline has exited.
//
// waitpid(-1, ...) means "any child", so it may hand back a background job's
// child instead (one that happened to finish while we wait). That is fine; we
// just credit it to the job it belongs to.
static void waitForeground(PipelineRep r, Jobs jobs) {
  while (r->pending > 0) {
    int pid = waitpid(-1, NULL, 0);
    if (pid < 0)
      break;				// no children left at all
    if (!reapPid(r, pid))
      creditJobs(jobs, pid);
  }
}

extern void execPipeline(Pipeline pipeline, Jobs jobs, int *eof) {
  PipelineRep r = pipeline;

  // A lone foreground builtin runs inside the shell itself (see Command.c).
  if (r->fg && sizePipeline(r) == 1) {
    Command c = deq_head_ith(r->commands, 0);
    if (isBuiltin(c)) {
      runBuiltin(c, eof);
      freePipeline(r);
      return;
    }
  }

  startChildren(r);
  if (r->fg) {
    waitForeground(r, jobs);
    freePipeline(r);
  } else {
    addJobs(jobs, r);			// keep it; reaped and freed later
  }
}

extern void freePipeline(Pipeline pipeline) {
  PipelineRep r = pipeline;
  deq_del(r->commands, freeCommand);
  free(r->pids);
  free(r);
}
