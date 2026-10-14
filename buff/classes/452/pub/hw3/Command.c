// Command.c -- one command: its words, its redirections, and how to run it.
//
// Two different ways a command can run:
//
//   1. A builtin typed on its own in the foreground runs INSIDE the shell
//      process (runBuiltin). It has to: "cd" must change the shell's own
//      directory, and "exit" must stop the shell itself. A child process
//      could do neither.
//
//   2. Everything else runs in a forked child (execChild): builtins that are
//      backgrounded or in a pipeline, and all ordinary programs like ls.
//      The child's changes die with it, which is why "cd /etc | pwd" does not
//      move the shell.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <readline/history.h>

#include "Command.h"
#include "error.h"

typedef struct {
  char **argv;		// NULL-terminated; argv[0] is the command's name
  char *in;		// file named by "< file", or 0
  char *out;		// file named by "> file", or 0
} *CommandRep;

// ---------------------------------------------------------------------------
// Builtins
// ---------------------------------------------------------------------------

typedef void (*BuiltinFn)(CommandRep r, int *eof);

typedef struct {
  char *name;
  BuiltinFn run;
} Builtin;

// Check that a builtin got exactly `want` arguments (not counting its own
// name). On a mismatch, complain and return 0 so the builtin does nothing.
static int checkArgs(CommandRep r, int want) {
  int have = 0;
  while (r->argv[have + 1])
    have++;
  if (have != want) {
    WARN("%s: expected %d argument(s), got %d", r->argv[0], want, have);
    return 0;
  }
  return 1;
}

static void bi_exit(CommandRep r, int *eof) {
  if (checkArgs(r, 0))
    *eof = 1;			// the main loop notices this and stops
}

static void bi_pwd(CommandRep r, int *eof) {
  char dir[PATH_MAX];
  if (!checkArgs(r, 0))
    return;
  if (!getcwd(dir, sizeof dir)) {
    WARN("pwd: %s", strerror(errno));
    return;
  }
  printf("%s\n", dir);
}

// "cd -" needs the directory we were in before the last successful cd.
static char previousDir[PATH_MAX];
static int havePreviousDir = 0;

static void bi_cd(CommandRep r, int *eof) {
  if (!checkArgs(r, 1))
    return;				// e.g. a bare "cd" with no directory

  // Remember where we are now, in case this cd succeeds.
  char here[PATH_MAX];
  if (!getcwd(here, sizeof here)) {
    WARN("cd: %s", strerror(errno));
    return;
  }

  char *dest = r->argv[1];
  if (!strcmp(dest, "-")) {
    if (!havePreviousDir) {
      WARN("cd: no previous directory");
      return;
    }
    dest = previousDir;
  }

  if (chdir(dest) < 0) {
    WARN("cd: %s: %s", dest, strerror(errno));
    return;
  }

  // Only now, after chdir succeeded, update the saved directory. (Doing it
  // earlier would be wrong when dest points at previousDir itself.)
  snprintf(previousDir, sizeof previousDir, "%s", here);
  havePreviousDir = 1;
}

static void bi_history(CommandRep r, int *eof) {
  if (!checkArgs(r, 0))
    return;
  // history_list() is the GNU History library's array of past lines, and
  // history_length says how many there are.
  HIST_ENTRY **list = history_list();
  for (int i = 0; list && i < history_length; i++)
    printf("%5d  %s\n", history_base + i, list[i]->line);
}

static const Builtin builtins[] = {
  { "exit",    bi_exit },
  { "pwd",     bi_pwd },
  { "cd",      bi_cd },
  { "history", bi_history },
  { 0, 0 }
};

// Look up the builtin named by this command (0 if it is not a builtin).
static const Builtin *findBuiltin(CommandRep r) {
  for (const Builtin *b = builtins; b->name; b++)
    if (!strcmp(b->name, r->argv[0]))
      return b;
  return 0;
}

extern int isBuiltin(Command command) {
  return findBuiltin(command) != 0;
}

// ---------------------------------------------------------------------------
// Redirection
// ---------------------------------------------------------------------------

// Point file descriptor 0 (stdin) at the "<" file and descriptor 1 (stdout)
// at the ">" file. Returns 0 on success, -1 (after a warning) on failure.
//
// dup2(fd, 0) makes descriptor 0 an alias of fd, closing whatever 0 used to
// be. Once 0 is an alias, the original fd is redundant, so we close it.
static int redirect(CommandRep r) {
  if (r->in) {
    int fd = open(r->in, O_RDONLY);
    if (fd < 0) {
      WARN("%s: %s", r->in, strerror(errno));
      return -1;
    }
    dup2(fd, 0);
    close(fd);
  }
  if (r->out) {
    int fd = open(r->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      WARN("%s: %s", r->out, strerror(errno));
      return -1;
    }
    dup2(fd, 1);
    close(fd);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Running a command
// ---------------------------------------------------------------------------

// Run a builtin in the shell's process. Its redirections must only last for
// this one command, so save copies of stdin/stdout first and restore them.
extern void runBuiltin(Command command, int *eof) {
  CommandRep r = command;
  const Builtin *b = findBuiltin(r);

  int savedIn = dup(0);
  int savedOut = dup(1);

  if (redirect(r) == 0)
    b->run(r, eof);
  fflush(stdout);			// push output into the file/pipe NOW,
					// before we point stdout back
  dup2(savedIn, 0);
  dup2(savedOut, 1);
  close(savedIn);
  close(savedOut);
}

// Runs in the child after fork(). Pipeline.c has already connected any pipes
// to stdin/stdout; a "<" or ">" on the command overrides those.
extern void execChild(Command command) {
  CommandRep r = command;

  if (redirect(r) < 0)
    _exit(1);

  const Builtin *b = findBuiltin(r);
  if (b) {				// a builtin in a pipeline or in the background
    int eof = 0;			// ("exit" here would only end this child)
    b->run(r, &eof);
    fflush(stdout);
    _exit(0);
  }

  execvp(r->argv[0], r->argv);		// replaces this process; only returns
					// if the program could not be started
  fprintf(stderr, "shell: %s: %s\n", r->argv[0], strerror(errno));
  _exit(127);				// 127 = "command not found" by convention
					// (_exit, not exit: don't flush the
					// stdio buffers we inherited from the
					// parent a second time)
}

// ---------------------------------------------------------------------------
// Creating and freeing
// ---------------------------------------------------------------------------

extern Command newCommand(T_words words, struct T_redir redir) {
  CommandRep r = malloc(sizeof(*r));
  if (!r)
    ERROR("malloc() failed");

  // Count the words, then copy them into a NULL-terminated argv array, the
  // shape execvp() wants.
  int n = 0;
  for (T_words w = words; w; w = w->words)
    n++;
  r->argv = malloc(sizeof(char *) * (n + 1));
  if (!r->argv)
    ERROR("malloc() failed");
  int i = 0;
  for (T_words w = words; w; w = w->words)
    r->argv[i++] = strdup(w->word->s);
  r->argv[i] = 0;

  r->in = redir.in ? strdup(redir.in) : 0;
  r->out = redir.out ? strdup(redir.out) : 0;
  return r;
}

extern void freeCommand(Command command) {
  CommandRep r = command;
  for (char **p = r->argv; *p; p++)
    free(*p);
  free(r->argv);
  free(r->in);
  free(r->out);
  free(r);
}
