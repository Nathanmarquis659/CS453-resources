// Sequence.c -- a Sequence is a list of Pipelines that run one after another
// (separated by ";" or "&" on the command line).

#include <stdio.h>
#include "Sequence.h"
#include "deq.h"
#include "error.h"

extern Sequence newSequence() {
  return deq_new();
}

extern void addSequence(Sequence sequence, Pipeline pipeline) {
  deq_tail_put(sequence, pipeline);
}

extern void freeSequence(Sequence sequence) {
  deq_del(sequence, freePipeline);	// frees any pipelines that never ran
}

// Run the pipelines in order. An "exit" stops the loop; whatever has not run
// yet is simply freed.
extern void execSequence(Sequence sequence, Jobs jobs, int *eof) {
  while (deq_len(sequence) > 0 && !*eof)
    execPipeline(deq_head_get(sequence), jobs, eof);
  freeSequence(sequence);
}
