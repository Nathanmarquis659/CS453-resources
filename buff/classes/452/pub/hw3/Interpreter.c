// Interpreter.c -- walks a parse tree (Tree.h) and builds the runnable
// objects from it, then runs them:
//
//     tree node       ->   runnable object
//     T_sequence      ->   Sequence   (list of Pipelines)
//     T_pipeline      ->   Pipeline   (list of Commands)
//     T_command       ->   Command    (argv + redirections)
//
// The parse tree only *describes* the input; these objects know how to
// actually run it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Interpreter.h"
#include "Sequence.h"
#include "Pipeline.h"
#include "Command.h"

// Add every command of the tree pipeline `t` to the runnable `pipeline`.
static void i_pipeline(T_pipeline t, Pipeline pipeline) {
  for (; t; t = t->pipeline)		// the tree is a linked list of commands
    addPipeline(pipeline, newCommand(t->command->words, t->command->redir));
}

// Add every pipeline of the tree sequence `t` to the runnable `sequence`.
static void i_sequence(T_sequence t, Sequence sequence) {
  for (; t; t = t->sequence) {
    // A pipeline ended by "&" runs in the background; ";" or nothing means
    // the shell waits for it.
    int fg = !(t->op && !strcmp(t->op, "&"));
    Pipeline pipeline = newPipeline(fg);
    i_pipeline(t->pipeline, pipeline);
    addSequence(sequence, pipeline);
  }
}

extern void interpretSequence(T_sequence s, int *eof, Jobs jobs) {
  Sequence sequence = newSequence();
  i_sequence(s, sequence);
  execSequence(sequence, jobs, eof);	// runs it, then frees it
}
