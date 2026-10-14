#ifndef PIPELINE_H
#define PIPELINE_H

// A Pipeline is one or more Commands joined by "|", e.g.  ls | sort | head.
// A single plain command is just a pipeline of length 1.

typedef void *Pipeline;

#include "Command.h"
#include "Jobs.h"

// fg is 1 for a normal (foreground) pipeline, 0 if it ended with "&".
extern Pipeline newPipeline(int fg);
extern void addPipeline(Pipeline pipeline, Command command);
extern int sizePipeline(Pipeline pipeline);

// Run the pipeline. A foreground pipeline is finished (and freed) when this
// returns. A background pipeline is handed to `jobs` and keeps running.
extern void execPipeline(Pipeline pipeline, Jobs jobs, int *eof);

// Bookkeeping used by Jobs.c to find out when a background pipeline is done.
//   reapPid:          if pid is one of this pipeline's children, mark it
//                     finished and return 1; otherwise return 0.
//   pendingPipeline:  how many children are still running.
extern int reapPid(Pipeline pipeline, int pid);
extern int pendingPipeline(Pipeline pipeline);

extern void freePipeline(Pipeline pipeline);

#endif
