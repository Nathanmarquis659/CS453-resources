#ifndef COMMAND_H
#define COMMAND_H

// A Command is ONE program (or builtin) with its arguments and optional
// "< in" / "> out" redirections, e.g.  sort -r < names > sorted.
// A Pipeline (Pipeline.h) is a list of Commands joined by "|".

typedef void *Command;

#include "Tree.h"

// Build a Command from the parse tree. Copies everything it needs, so the
// tree can be freed independently.
extern Command newCommand(T_words words, struct T_redir redir);

// Is this one of the shell's builtins (exit, pwd, cd, history)?
extern int isBuiltin(Command command);

// Run a builtin inside the shell's own process, honoring its redirections.
// (Must only be called when isBuiltin() is true.) Sets *eof for "exit".
extern void runBuiltin(Command command, int *eof);

// Called in a freshly forked child: apply the redirections, then run the
// builtin or exec the program. Never returns.
extern void execChild(Command command);

extern void freeCommand(Command command);

#endif
