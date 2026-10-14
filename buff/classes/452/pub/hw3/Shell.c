// Shell.c -- main(): the read-eval loop.
//
//     read a line  ->  parse and run it  ->  repeat until "exit" or Ctrl-D

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "Jobs.h"
#include "Parser.h"

int main() {
  int eof = 0;				// set to 1 by the "exit" builtin
  Jobs jobs = newJobs();		// background pipelines still running
  char *prompt = 0;
  char historyFile[PATH_MAX];

  // Where to save history between sessions. It must be an absolute path: the
  // shell can "cd" away, and a relative name would then land in whatever
  // directory we happen to be in when we exit.
  char *home = getenv("HOME");
  snprintf(historyFile, sizeof historyFile, "%s/.shell_history",
           home ? home : ".");

  // Are we talking to a person at a terminal, or reading input from a file
  // or pipe (as the regression tests do)?
  int interactive = isatty(fileno(stdin));

  using_history();			// start the GNU History library
  if (interactive) {
    prompt = "$ ";
    read_history(historyFile);	// remember commands between sessions
  } else {
    // readline normally echoes the line and offers tab-completion. When the
    // input is a file, send that echo to /dev/null so it doesn't pollute the
    // output the tests compare, and make Tab insert a tab character.
    rl_bind_key('\t', rl_insert);
    rl_outstream = fopen("/dev/null", "w");
  }

  while (!eof) {
    reapJobs(jobs);			// clean up finished background jobs
    char *line = readline(prompt);	// returns 0 at end of input (Ctrl-D)
    if (!line)
      break;
    if (*line)
      add_history(line);		// readline's history keeps its own copy
    interpret(line, &eof, jobs);
    free(line);
  }

  // "exit" waits for background jobs before leaving.
  drainJobs(jobs);

  if (interactive) {
    write_history(historyFile);
    rl_clear_history();
  } else {
    fclose(rl_outstream);
  }
  return 0;
}
