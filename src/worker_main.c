#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>

#include "debug.h"
#include "polya.h"

/*
 * "Polya" multiprocess problem solver: worker process.
 *
 * Command-line arguments are ignored.
 */
int main(int argc, char *argv[])
{
    // A worker does not generate any problems, but we should
    // have all known solvers enabled.
    init_problems(0, ~0x0);
    return worker();
}
