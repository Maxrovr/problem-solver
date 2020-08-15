## Introduction

This is a "problem solver" program, called `polya`, which manages a collection of "worker" processes that concurrently engage in solving computationally intensive problems.

## Getting Started

You will need to install the `libgcrypt20-dev` crypto library in order to compile
the code.  Do this with the following command:

```
$ sudo apt install libgcrypt20-dev
```

## Usage

The master program (exectuable `bin/polya`) has the following usage synopsis:

```
$ polya [-w num_workers] [-p num_probs] [-t prob_type]
```

where:
 * `num_workers` is the number of worker processes to use (min 1, max 32, default 1)
 * `num_probs` is the total number of problems to be solved (default 0)
 * `prob_type` is an integer specifying a problem type whose solver
    is to be enabled (min 0, max 31).  The -t flag may be repeated to enable
    multiple problem types.

The worker program, whose executable is `bin/polya_worker` and whose `main()`
function is in the file `worker_main.c()`, ignores its command-line arguments.
Instead, the worker will receive problems from the master process by reading
its standard input, and sends results back to the master process by
writing to its standard output.

### Code Structure :
<pre>
.
├── .gitlab-ci.yml
└── 
    ├── demo
    │   └── polya
    ├── include
    │   ├── debug.h
    │   └── polya.h
    ├── lib
    │   └── sf_event.o
    ├── Makefile
    ├── src
    │   ├── crypto_miner.c
    │   ├── main.c
    │   ├── master.c
    │   ├── problem.c
    │   ├── trivial.c
    │   ├── worker.c
    │   └── worker_main.c
    └── tests
        └── tests.c
</pre>

If you run `make`, the code should compile correctly, resulting in three executables `bin/polya`, `bin/polya_worker`, and `bin/polya_test`. The executable `bin/polya` is the main one, for the "master" process.

The executable `bin/polya_worker` is the executable run by "worker" processes. It is invoked by the master process -- it is not designed to be invoked from the command line.

The executable `bin/polya_tests` runs some tests using Criterion.

## `Polya`: Functional Specification

### Overview

The `polya` program implements a facility for concurrently solving computationally
intensive "problems".  It is not particularly important to the core logic of `polya`
what the problems are that are being solved.  All that the core logic knows about
is that a "problem" consists of a *header*, which gives some basic information about
the problem, and some *data*, which is an arbitrary sequence of bytes that the core
logic does not otherwise interpret.  The header of a problem contains a *size* field,
which specifies the total length of the header plus data, a *type* field,
which specifies which one of a set of available *solvers* is to be used to solve the
problem, an *id* field, which contains a serial number for the problem, and some fields
that specify in which of a number of possible *variant forms* the problem is expressed.

After initialization is completed, the `polya` program in execution consists of a
a *master process* and one or more *worker processes*.  The master process is
responsible for obtaining problems to be solved from a *problem source* and
distributing those problems to the worker processes to be solved.
When a worker process receives a problem from the master process, it begins trying
to solve that problem.  It continues trying to solve the problem until one of the
following things happens: (1) A solution is found; (2) The solution procedure fails;
or (3) The master process notifies the worker to cancel the solution procedure.
In each case, the worker process sends back to the master process a *result*,
which indicates what happened and possibly contains a solution to the problem.
The structure of a result is similar to that of a problem and, just as for problems,
the core `polya` logic does not care about the detailed content of a result,
which is dependent on the problem type.  As far as the core logic is concerned,
a result consists of a *header* and some uninterpreted *data*.
The header contains a *size* field, which specifies the total length of the header
plus data, an *id* field, which gives the serial number of the problem to which the
result pertains, and a *failed* field, which indicates whether the solution procedure
failed or whether the result should be considered as a potential solution to the
original problem.

### Coordination Protocol

The master process coordinates with the worker processes using a protocol that involves
*pipes* and *signals*.  During initialization, each time the master process creates a
worker process, it creates two pipes for communicating with that worker process:
one pipe for sending problems from the master to the worker, and one pipe for sending
results from the worker to the master.

When the master process wants to send a problem to a worker process, it first writes
the fixed-size problem header to the pipe, and then continues by writing the problem
data.  When a worker process wants to read a problem sent by the master, it first reads
the fixed-size problem header, and continues by reading the problem data, the number of
bytes of which can be calculated by subtracting the size of the header from the total
size given in the size field of the header.  The procedure by which a worker sends a result
to the master process is symmetric.

In order to know when to send a problem to a worker process and when to read a result
from a worker process, the master process uses *signals* to control the worker process
and to track its state.  From the point of view of the master process, at any given time,
a worker process is in one of the following states:

* `STARTED` - The worker process has just been started, and is performing initialization.

* `IDLE` - The worker process has stopped (as a result of the worker having received a
`SIGSTOP` signal), and the master process knows this (as a result of having received
a `SIGCHLD` signal and subsequently having used the `waitpid(2)` system call to query
the reason for the signal).  In this `IDLE` state, there is no result available for the
master to read on the pipe from that worker.

* `CONTINUED` - A stopped worker process has been signaled to continue (by having been sent
a `SIGCONT` signal), but the master has not yet observed (via `SIGCHLD`/`waitpid()`)
that the worker is once again executing.  When the worker process does continue to execute,
it will attempt to read a problem on the pipe from the master process.

* `RUNNING` - The master process has received a `SIGCHLD` from a worker process that was
previously in the `CONTINUED` state, and the master process has used the `waitpid(2)`
system call to confirm that the worker process is no longer stopped.  The worker process is
now working on solving a problem that the master has sent.

* `STOPPED` - The worker process has stopped working on trying to solve a problem that was
previously sent, and has written a result (which might or might not be marked "failed")
on the pipe to the master process.  The master has observed (via `SIGCHLD`/`waitpid()`)
that the worker has stopped and the master will read the result sent by the worker.
A worker process in the `RUNNING` state arranges to enter the `STOPPED` state by
using `kill(2)` to send itself a `SIGSTOP` signal.

* `EXITED` - The worker process has exited normally using the `exit(3)` function, and the
master process has observed this (via `SIGCHLD`/`waitpid()`).

* `ABORTED` - The worker process has terminated abnormally as a result of a signal,
or has terminated normally but with a nonzero exit status, and the master process has
observed this (via `SIGCHLD`/`waitpid()`).

Normally, a worker that is signaled to continue will read a problem sent by the master
process and will try to solve that problem until either the solution procedure succeeds
in finding a solution or it fails to find any solution.  However, it is also possible
for the master process to notify a worker process to *cancel* the solution procedure
that it is currently executing.  The master process does this by sending a `SIGHUP`
signal to the worker process.  When a worker process receives `SIGHUP`, if the current
solution attempt has not already succeeded or failed, then it is abandoned and a
result marked "failed" is sent to the master process before the worker stops
by sending itself a `SIGSTOP` signal.

Finally, when a worker receives a `SIGTERM` signal, it will abandon any current solution
attempt and use `exit(3)` to terminate normally with status `EXIT_SUCCESS`.

### Concurrent Solution

The underlying idea of `polya` is to exploit the possibility of concurrent execution
of worker processes (*e.g.* on multiple cores) to speed up the process of finding a
solution to a problem.  To support this, each problem to be solved with `polya` can be
created in one of several *variant forms*, where a solution to any one of the variant
forms constitutes a solution to the original problem.  Concurrency is achieved by
assigning a different variant form to each of a collection of concurrently executing
worker processes.  To the extent that the work involved in solving one variant form
does not overlap with that involved in solving another, the concurrency will speed up
the solution procedure.

As a concrete example, the main example problem type that has been included with the `polya`
basecode is the "crypto miner" problem type.  This problem type is modeled after the
computations performed by "miners" in a cryptocurrency system such as Bitcoin.
In such a system, the "miners" generate "blocks", which contain lists of transactions,
and for each block it creates a miner attempts to certify to the other miners in the
system that it has put significant computational effort into creating the block.
The other miners will only accept blocks with such a certification -- this is the concept
of *proof of work* that underlies the security of Bitcoin and other similar cryptocurrency
systems.  The proof of work for a block is achieved by finding a solution to a computationally
intensive problem associated with the block and including that solution with the block.

In Bitcoin, the type of problem that is solved is to find a sequence of bytes,
called a "nonce", with the property that when the nonce is appended to the block and the
result is hashed using a cryptographic hash function, then the resulting hash
(which is just a sequence of bits) has a specified number of leading zero bits.
The solution to the problem is the nonce, and although anyone can readily verify the
solution by concatenating the nonce with the block data and computing its hash,
it is in general not possible to efficiently find a nonce that solves a given block -- the best
that can be done is just to try one nonce after another until a solution is found.
This is what Bitcoin miners spend their time (and electrical power) doing.
The number of leading zero bits required for a solution allows the "difficulty" of the
problem to be solved to be adjusted: requiring fewer zero bits makes it easier to solve
the problem and requiring more zero bits makes it harder.
The nature of this kind of problem makes concurrency useful in solving it:
we can just partition the space of all possible nonce values into disjoint subsets,
and assign a separate concurrent process to work on each subset.

For the "crypto miner" solver included with the basecode, "blocks" are just filled with
random data (because we are not really trying to process transactions) and the SHA256
hash algorithm is used to hash the block contents, followed by a 8-byte nonce
(the Bitcoin system actually uses something like three rounds of the older SHA1 hash
algorithm rather than one round of SHA256).
The problem "difficulty" ranges from 20 to 25 bits of leading zeros, which should produce
a reasonable test range of solution times for our purposes on most computers.
The Bitcoin system dynamically adjusts the difficulty to adapt to the total number of
miners and the power of their computers so that, on average, about one block will
be solved every ten minutes in the entire system.

### Problem Source

In `polya`, problems to be solved are obtained from a "problem source" module,
which maintains a notion of the "current problem", creates variant forms of the
current problem, and keeps track of when the current problem has been solved
and it is time to create a new problem.  When a new problem is created, one of
the enabled problem types is chosen randomly and its "constructor" method is invoked
to produce a new problem.  When a worker process produces a result, the result is
posted to the problem source module, which checks the solution and clears the
current problem if a solution has indeed been found.

The problem module has been introduced basically to encapsulate the creation and
management of problems to be solved (which is not our primary interest here)
from the mechanism by which the problems are solved by concurrent worker processes
(which is our main interest).  The problems that we create are just built from
random data, though it would also be possible to have some of the "constructor"
methods read real problem data from a file.

For System Fundamentals II - Processess
