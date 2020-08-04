#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <stdio.h>

#include "event.h"
#include "driver.h"
#include "tracker.h"
#include "__helper.h"

#define QUOTE1(x) #x
#define QUOTE(x) QUOTE1(x)
#define SCRIPT1(x) x##_script
#define SCRIPT(x) SCRIPT1(x)

#define POLYA_EXECUTABLE "bin/polya"

/*
 * Finalization function to try to ensure no stray processes are left over
 * after a test finishes.
 */
static void killall() {
  system("killall -s KILL polya_worker");
  system("killall -s KILL bin/polya");
}

/*
 * Tests of master + worker using event tracker.
 */
#define SUITE tracker_suite

/*
 * One worker, no problems (default arguments).
 * Check for START_EVENT, followed by END_EVENT and EOF,
 * with arbitrary events intervening between START_EVENT and END_EVENT
 * (though it is checked that they define valid transitions).
 */
#define TEST_NAME start_end_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * One worker, one problem of trivial type.
 * Check that worker solves problem.
 */
#define TEST_NAME one_worker_one_trivial_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   0,                 TEN_MSEC,   NULL,      assert_num_workers, (void*)1 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-p1", "-t1", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * One worker, multiple problems of trivial type.
 * Check that worker solves all problems.
 */
#define TEST_NAME one_worker_multi_trivial_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   0,                 TEN_MSEC,   NULL,      assert_num_workers, (void*)1 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-p3", "-t1", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Two workers, one problem of trivial type.
 */
#define TEST_NAME two_workers_one_trivial_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, HND_MSEC,   NULL,      assert_num_workers, (void*)2 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w2", "-p1", "-t1", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Two workers, multiple problems of trivial type.
 */
#define TEST_NAME two_workers_multi_trivial_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, HND_MSEC,   NULL,      assert_num_workers, (void*)2 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w2", "-p3", "-t1", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Many workers, many problems of trivial type.
 */
#define TEST_NAME many_workers_many_trivial_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, HND_MSEC,   NULL,      assert_num_workers, (void*)10 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w10", "-p1000", "-t1", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * One worker, one problem of crypto type.
 * Check that worker solves problem.
 */
#define TEST_NAME one_worker_one_crypto_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 THTY_SEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   0,                 TEN_MSEC,   NULL,      assert_num_workers, (void*)1 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-p1", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * One worker, multiple problems of crypto type.
 * Check that worker solves all problems.
 */
#define TEST_NAME one_worker_multi_crypto_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 THTY_SEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 THTY_SEC,   NULL,      NULL },
    {  NULL,     SEND_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 THTY_SEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   0,                 TEN_MSEC,   NULL,      assert_num_workers, (void*)1 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-p3", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Two workers, one problem of crypto type.
 */
#define TEST_NAME two_workers_one_crypto_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, THTY_SEC,   NULL,      assert_num_workers, (void*)2 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w2", "-p1", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Two workers, multiple problems of crypto type.
 */
#define TEST_NAME two_workers_multi_crypto_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, THTY_SEC,   NULL,      assert_num_workers, (void*)2 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w2", "-p3", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Many workers, several problems of crypto type.
 * Use a longer timeout due to apparent scheduling latency.
 */
#define TEST_NAME many_workers_several_crypto_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, THTY_SEC,   NULL,      assert_num_workers, (void*)10 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w10", "-p10", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * Many workers, several problems of mixed type.
 * Use a longer timeout due to apparent scheduling latency.
 */
#define TEST_NAME many_workers_several_mixed_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, THTY_SEC,   NULL,      assert_num_workers, (void*)10 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {POLYA_EXECUTABLE, "-w10", "-p20", "-t1", "-t2", NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME

/*
 * This test checks whether cancellation is working properly.
 * It starts two workers on a problem of a special problem type for which all
 * variants except the last hang forever.  This ensures that the cancellation sequence
 * will be exercised, resulting in a cancellation event for one of the workers.
 * The timeouts check that the cancellation occurs promptly.
 *
 * This doesn't work reliably, because it is possible for one worker to solve
 * the problem before the other worker has even gotten started.  In that case,
 * the master will not issue any cancellation because no other worker is working
 * on the problem.  I've hacked this by introducing a sleep delay for the solvable
 * problem variant, to try to make it more likely that the other worker will have
 * gotten started.
 */
#define TEST_NAME cancellation_test
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,     expect,      modifiers,         timeout,    before,    after
    {  NULL,     START_EVENT, 0,                 HND_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  EXPECT_SKIP_OTHER, TEN_SEC,    NULL,      NULL },
    {  NULL,     CANCEL_EVENT,0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     RECV_EVENT,  0,                 TEN_MSEC,   NULL,      NULL },
    {  NULL,     END_EVENT,   EXPECT_SKIP_OTHER, HND_MSEC,   NULL,      assert_num_workers, (void*)2 },
    {  NULL,     EOF_EVENT,   0,                 TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .fini = killall, .timeout = 300)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = { POLYA_EXECUTABLE, "-w", "2", "-p", "1", "-t", "3", NULL };
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef TEST_NAME
