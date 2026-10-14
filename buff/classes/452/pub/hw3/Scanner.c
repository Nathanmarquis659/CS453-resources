// Scanner.c -- the scanner (a.k.a. lexer) splits a line of text into tokens.
//
// A token is either
//   * an operator:  |  &  ;  <  >      (always exactly one character), or
//   * a word:       any run of other characters up to whitespace or an operator.
//
// So  "echo a;ls>out"  scans as:  echo  a  ;  ls  >  out
//
// The parser asks for tokens through five small functions (see Scanner.h):
//   next = advance to the next token       curr = look at the current token
//   cmp  = is the current token == s ?     eat  = if so, advance past it
//   pos  = how far into the line are we (used only in error messages)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Scanner.h"
#include "error.h"

typedef struct {
  int eos;		// 1 once we have run out of tokens ("end of string")
  char *str;		// our private copy of the whole line
  char *pos;		// where scanning will resume
  char *curr;		// the current token (malloc'd), or 0 if none scanned yet
} *ScannerRep;

extern Scanner newScanner(char *s) {
  ScannerRep r = malloc(sizeof(*r));
  if (!r)
    ERROR("malloc() failed");
  r->eos = 0;
  r->str = strdup(s);
  r->pos = r->str;
  r->curr = 0;
  return r;
}

extern void freeScanner(Scanner scan) {
  ScannerRep r = scan;
  free(r->str);
  free(r->curr);		// free(0) is allowed and does nothing
  free(r);
}

static int isOperator(char c) {
  return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static int isSpace(char c) {
  return c == ' ' || c == '\t';
}

// Scan the next token and make it the current one. Returns it, or 0 at the
// end of the line.
extern char *nextScanner(Scanner scan) {
  ScannerRep r = scan;
  if (r->eos)
    return 0;

  char *start = r->pos;
  while (isSpace(*start))
    start++;				// skip whitespace between tokens

  char *end = start;
  if (isOperator(*start))
    end++;				// an operator is a token all by itself
  else
    while (*end && !isSpace(*end) && !isOperator(*end))
      end++;				// a word runs until a delimiter

  if (end == start) {			// nothing left on the line
    r->eos = 1;
    return 0;
  }

  free(r->curr);			// the previous token's text is now stale
  r->curr = strndup(start, end - start);
  if (!r->curr)
    ERROR("malloc() failed");
  r->pos = end;
  return r->curr;
}

// The current token, scanning the first one on demand.
extern char *currScanner(Scanner scan) {
  ScannerRep r = scan;
  if (r->eos)
    return 0;
  if (r->curr)
    return r->curr;
  return nextScanner(scan);
}

// Is the current token exactly s?
extern int cmpScanner(Scanner scan, char *s) {
  char *tok = currScanner(scan);
  return tok && !strcmp(tok, s);
}

// If the current token is s, consume it and return 1; otherwise return 0.
extern int eatScanner(Scanner scan, char *s) {
  int match = cmpScanner(scan, s);
  if (match)
    nextScanner(scan);
  return match;
}

// Character offset of the scanner within the line (for error messages).
extern int posScanner(Scanner scan) {
  ScannerRep r = scan;
  return r->pos - r->str;
}
