#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "deq.h"

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
  if (cond) { pass_count++; printf("PASS: %s\n", msg); } \
  else      { fail_count++; printf("FAIL: %s\n", msg); } \
} while (0)

// Runs `stmt` in a forked child. PASS if the child is killed by the
// library's ERROR() (exit(1)), meaning `stmt` correctly violated a
// documented precondition (empty deque, out-of-range index, etc).
// FAIL if the child instead returns normally (exit 0) -- that means
// the library silently tolerated input it was supposed to reject.
#define EXPECT_ERROR(stmt, msg) do {                                       \
  fflush(stdout);                                                          \
  pid_t _pid = fork();                                                     \
  if (_pid == 0) {                                                         \
    int devnull = open("/dev/null", O_WRONLY);                            \
    if (devnull >= 0) dup2(devnull, STDERR_FILENO); /* hush ERROR() text */ \
    (void)(stmt);                                                          \
    _exit(0); /* only reached if it did NOT error out */                   \
  }                                                                         \
  int _status;                                                             \
  waitpid(_pid, &_status, 0);                                              \
  int _died = WIFEXITED(_status) && WEXITSTATUS(_status) != 0;             \
  if (_died) { pass_count++; printf("PASS: %s\n", msg); }                  \
  else       { fail_count++; printf("FAIL: %s\n", msg); }                  \
} while (0)

// helper: stringify an integer stored as Data, for deq_str tests
static char *int_to_str(Data d) {
  char *s = malloc(32);
  snprintf(s, 32, "%ld", (long)d);
  return s;
}

int main() {
  Deq q;
  long got;

  // ---------------------------------------------------------------
  // 1. Empty deque behavior -- get/ith/rem on an empty deque are
  //    documented caller errors: must abort via ERROR(), not return 0.
  // ---------------------------------------------------------------
  q = deq_new();
  CHECK(deq_len(q) == 0, "new deque has len 0");
  EXPECT_ERROR(deq_head_get(q), "head_get on empty deque is fatal");
  EXPECT_ERROR(deq_tail_get(q), "tail_get on empty deque is fatal");
  EXPECT_ERROR(deq_head_ith(q, 0), "head_ith(0) on empty deque is fatal");
  CHECK(deq_head_rem(q, (Data)(long)1) == 0, "rem on empty deque returns 0");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 2. Basic single put/get, both ends
  // ---------------------------------------------------------------
  q = deq_new();
  deq_head_put(q, (Data)(long)42);
  CHECK(deq_len(q) == 1, "len==1 after one head_put");
  got = (long)deq_head_get(q);
  CHECK(got == 42, "head_put then head_get returns same value");
  CHECK(deq_len(q) == 0, "len==0 after removing only element");
  deq_del(q, 0);

  q = deq_new();
  deq_tail_put(q, (Data)(long)99);
  got = (long)deq_tail_get(q);
  CHECK(got == 99, "tail_put then tail_get returns same value");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 3. Ordering: FIFO via tail_put + head_get
  // ---------------------------------------------------------------
  q = deq_new();
  deq_tail_put(q, (Data)(long)1);
  deq_tail_put(q, (Data)(long)2);
  deq_tail_put(q, (Data)(long)3);
  CHECK((long)deq_head_get(q) == 1, "FIFO: first out is 1");
  CHECK((long)deq_head_get(q) == 2, "FIFO: second out is 2");
  CHECK((long)deq_head_get(q) == 3, "FIFO: third out is 3");
  CHECK(deq_len(q) == 0, "len==0 after draining FIFO");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 4. Ordering: LIFO via head_put + head_get (stack behavior)
  // ---------------------------------------------------------------
  q = deq_new();
  deq_head_put(q, (Data)(long)1);
  deq_head_put(q, (Data)(long)2);
  deq_head_put(q, (Data)(long)3);
  CHECK((long)deq_head_get(q) == 3, "LIFO: first out is 3");
  CHECK((long)deq_head_get(q) == 2, "LIFO: second out is 2");
  CHECK((long)deq_head_get(q) == 1, "LIFO: third out is 1");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 5. ith() traversal and bounds
  // ---------------------------------------------------------------
  q = deq_new();
  deq_tail_put(q, (Data)(long)10);
  deq_tail_put(q, (Data)(long)20);
  deq_tail_put(q, (Data)(long)30);
  CHECK((long)deq_head_ith(q, 0) == 10, "head_ith(0) == 10");
  CHECK((long)deq_head_ith(q, 1) == 20, "head_ith(1) == 20");
  CHECK((long)deq_head_ith(q, 2) == 30, "head_ith(2) == 30");
  CHECK((long)deq_tail_ith(q, 0) == 30, "tail_ith(0) == 30");
  CHECK((long)deq_tail_ith(q, 2) == 10, "tail_ith(2) == 10");
  EXPECT_ERROR(deq_head_ith(q, 3), "head_ith(len) out of range is fatal");
  EXPECT_ERROR(deq_head_ith(q, -1), "head_ith(-1) negative index is fatal");
  CHECK(deq_len(q) == 3, "ith() does not mutate the deque");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 6. rem() -- head, tail, middle, not-found, single-node
  // ---------------------------------------------------------------
  q = deq_new();
  deq_tail_put(q, (Data)(long)1);
  deq_tail_put(q, (Data)(long)2);
  deq_tail_put(q, (Data)(long)3);
  deq_tail_put(q, (Data)(long)4);
  // list is now 1 <-> 2 <-> 3 <-> 4 (head=1, tail=4)

  // NOT a precondition violation -- the value legitimately isn't there.
  // If this turns out to also abort against libdeq.so, swap it to
  // EXPECT_ERROR after confirming against the spec.
  CHECK(deq_head_rem(q, (Data)(long)99) == 0, "rem: value not present returns 0");
  CHECK(deq_len(q) == 4, "rem: failed removal does not change len");

  got = (long)deq_head_rem(q, (Data)(long)1);
  CHECK(got == 1, "rem: remove head value returns 1");
  CHECK((long)deq_head_ith(q, 0) == 2, "rem: new head is 2 after removing old head");

  got = (long)deq_head_rem(q, (Data)(long)4);
  CHECK(got == 4, "rem: remove tail value returns 4");
  CHECK((long)deq_tail_ith(q, 0) == 3, "rem: new tail is 3 after removing old tail");

  // list is now 2 <-> 3
  got = (long)deq_head_rem(q, (Data)(long)3);
  CHECK(got == 3, "rem: remove middle-ish value returns 3");
  CHECK(deq_len(q) == 1, "rem: len==1 after removing down to one node");

  got = (long)deq_head_rem(q, (Data)(long)2);
  CHECK(got == 2, "rem: remove last remaining node returns 2");
  CHECK(deq_len(q) == 0, "rem: len==0 after removing last node");
  EXPECT_ERROR(deq_head_get(q), "get after draining via rem is fatal (empty deque)");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 7. Mixed head/tail interleaving
  // ---------------------------------------------------------------
  q = deq_new();
  deq_head_put(q, (Data)(long)2);   // [2]
  deq_tail_put(q, (Data)(long)3);   // [2,3]
  deq_head_put(q, (Data)(long)1);   // [1,2,3]
  deq_tail_put(q, (Data)(long)4);   // [1,2,3,4]
  CHECK((long)deq_head_ith(q, 0) == 1, "interleave: index0==1");
  CHECK((long)deq_head_ith(q, 1) == 2, "interleave: index1==2");
  CHECK((long)deq_head_ith(q, 2) == 3, "interleave: index2==3");
  CHECK((long)deq_head_ith(q, 3) == 4, "interleave: index3==4");
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // 8. deq_str with a stringify function
  // ---------------------------------------------------------------
  q = deq_new();
  deq_tail_put(q, (Data)(long)7);
  deq_tail_put(q, (Data)(long)8);
  deq_tail_put(q, (Data)(long)9);
  char *s = deq_str(q, int_to_str);
  CHECK(strcmp(s, "7 8 9") == 0, "deq_str produces \"7 8 9\"");
  printf("deq_str result: \"%s\"\n", s);
  free(s);
  deq_del(q, 0);

  // ---------------------------------------------------------------
  // Summary
  // ---------------------------------------------------------------
  printf("\n===== %d PASSED, %d FAILED =====\n", pass_count, fail_count);
  return fail_count != 0;
}
