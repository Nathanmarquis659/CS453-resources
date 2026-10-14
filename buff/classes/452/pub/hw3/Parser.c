// Parser.c -- turns a line of tokens into a parse tree (see Tree.h).
//
// This is a "recursive-descent" parser: there is one function per grammar rule
// (see grammar.txt), and each calls the functions for the rules it contains.
//
//     words    ::= word | word words
//     redir    ::= (nothing) | < word | > word | < word > word
//     command  ::= words redir
//     pipeline ::= command  |  command "|" pipeline   (quoted | = pipe character)
//     sequence ::= pipeline [ & | ; ]
//
// The tree it builds is only a temporary description of one pipeline: the
// Interpreter turns it into runnable objects, then we free it.
//
// Syntax errors: the first problem on a line is reported, the rest of the
// line is thrown away, and the shell carries on with the next line. (The
// skeleton used to call exit() here, so one typo killed the whole shell.)

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "Interpreter.h"
#include "Parser.h"
#include "Tree.h"
#include "Scanner.h"
#include "error.h"

static Scanner scan;		// the line currently being parsed
static int syntaxError;		// 1 once something on this line was malformed

// Short names for the scanner functions, so the grammar code reads cleanly.
static char *next()       { return nextScanner(scan); }
static char *curr()       { return currScanner(scan); }
static int   eat(char *s) { return eatScanner(scan, s); }

// Report a syntax error (only the first one on a line) and remember it.
static void fail(const char *fmt, ...) {
  if (!syntaxError) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "shell: syntax error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, " (at position %d)\n", posScanner(scan));
    va_end(ap);
  }
  syntaxError = 1;
}

// Is s one of the operator tokens? Operators are never part of a word.
static int isOperator(char *s) {
  return !strcmp(s, "|") || !strcmp(s, "&") || !strcmp(s, ";") ||
         !strcmp(s, "<") || !strcmp(s, ">");
}

// The functions that free the tree are defined at the bottom of the file.
static void f_sequence(T_sequence t);

// ---------------------------------------------------------------------------
// One function per grammar rule. Each returns the tree it built, or 0 if the
// input does not start with that kind of thing.
// ---------------------------------------------------------------------------

// word: any token that is not an operator.
static T_word p_word() {
  char *s = curr();
  if (!s || isOperator(s))
    return 0;
  T_word word = new_word();
  word->s = strdup(s);
  next();
  return word;
}

// words ::= word words | word        (a linked list of words)
static T_words p_words() {
  T_word word = p_word();
  if (!word)
    return 0;
  T_words words = new_words();
  words->word = word;
  words->words = p_words();		// 0 when there are no more words
  return words;
}

// After "<" or ">" we need a file name, i.e. a word.
static char *p_filename(char *complaint) {
  char *s = curr();
  if (!s || isOperator(s)) {
    fail("%s", complaint);
    return 0;
  }
  char *name = strdup(s);
  next();
  return name;
}

// redir ::= nothing | < word | > word | < word > word
// ("> out < in" is NOT allowed by the grammar; p_command rejects it.)
static void p_redir(struct T_redir *redir) {
  redir->in = redir->out = 0;
  if (eat("<"))
    redir->in = p_filename("expected a file name after '<'");
  if (eat(">"))
    redir->out = p_filename("expected a file name after '>'");
}

// command ::= words redir
static T_command p_command() {
  T_words words = p_words();
  if (!words)
    return 0;
  T_command command = new_command();
  command->words = words;
  p_redir(&command->redir);

  // A finished command may only be followed by the end of the line or a
  // pipeline/sequence operator. Anything else (for example the second ">" in
  // "echo a > b > c") is an error, not something to silently ignore.
  char *s = curr();
  if (s && strcmp(s, "|") && strcmp(s, "&") && strcmp(s, ";"))
    fail("unexpected '%s' after command", s);
  return command;
}

// pipeline ::= command  |  command "|" pipeline
static T_pipeline p_pipeline() {
  T_command command = p_command();
  if (!command)
    return 0;
  T_pipeline pipeline = new_pipeline();
  pipeline->command = command;
  if (eat("|")) {
    pipeline->pipeline = p_pipeline();
    if (!pipeline->pipeline)
      fail("expected a command after '|'");
  }
  return pipeline;
}

// One step of a sequence: a pipeline plus the "&" or ";" that ends it.
// (We parse one pipeline at a time, not the whole sequence; see interpret().)
static T_sequence p_sequence() {
  T_pipeline pipeline = p_pipeline();
  if (!pipeline)
    return 0;
  T_sequence sequence = new_sequence();
  sequence->pipeline = pipeline;
  if (eat("&"))
    sequence->op = "&";
  else if (eat(";"))
    sequence->op = ";";
  return sequence;
}

// ---------------------------------------------------------------------------
// The entry point.
// ---------------------------------------------------------------------------

// Parse and run one line, one pipeline at a time:
//     parse a pipeline -> run it -> parse the next -> run it -> ...
// Interleaving the two matters. In "cd /etc ; pwd" the cd must have actually
// happened before pwd runs, and "exit ; echo hi" must not run the echo.
extern void interpret(char *line, int *eof, Jobs jobs) {
  scan = newScanner(line);
  syntaxError = 0;

  while (!*eof) {
    T_sequence sequence = p_sequence();

    if (syntaxError) {			// bad line: discard it, run nothing more
      f_sequence(sequence);
      break;
    }
    if (!sequence) {			// no pipeline here: end of line, or junk
      if (curr())
        fail("unexpected '%s'", curr());
      break;
    }
    interpretSequence(sequence, eof, jobs);
    f_sequence(sequence);
  }

  freeScanner(scan);
}

// ---------------------------------------------------------------------------
// Freeing the tree. The tree owns every string in it, including the
// redirection file names (the Command made its own copies).
// ---------------------------------------------------------------------------

static void f_words(T_words t) {
  if (!t)
    return;
  if (t->word) {
    free(t->word->s);
    free(t->word);
  }
  f_words(t->words);
  free(t);
}

static void f_command(T_command t) {
  if (!t)
    return;
  f_words(t->words);
  free(t->redir.in);
  free(t->redir.out);
  free(t);
}

static void f_pipeline(T_pipeline t) {
  if (!t)
    return;
  f_command(t->command);
  f_pipeline(t->pipeline);
  free(t);
}

static void f_sequence(T_sequence t) {
  if (!t)
    return;
  f_pipeline(t->pipeline);
  f_sequence(t->sequence);
  free(t);
}
