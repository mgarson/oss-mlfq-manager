# OSS Multi-Level Feedback Queue (MLFQ) Manager

*C++, POSIX shared memory, message queues & signals*

---

## Overview
A command-line program that forks up to *N* child `worker` processes, enforces a concurrency limit of *M*, simulates a multilevel-feedback-queue scheduler with three priority levels, and maintains a shared-memory clock — printing PCB and queue status every 0.5 s of simulated time.

---

## Features

- **Multi-level Feedback Queue**  
  Implements three ready queues (`Q0`, `Q1`, `Q2`) with time quanta of 10 ms, 20 ms, and 40 ms respectively. Processes that exhaust their quantum are demoted; blocked or terminated processes are removed or re-enqueued at `Q0` after blocking.

- **Shared-memory clock**  
  Uses `shmget()`/`shmat()` to store a two-word clock (seconds + nanoseconds), incremented by `10 ms` (`shm_ptr[1] += 10000000`) each scheduler tick.

- **Process forking**  
  Spawns each `worker` child via `fork()` + `execvp()`, up to *N* total processes (set by `-n N`, default: 1).

- **Concurrency control**  
  Ensures no more than *M* workers run simultaneously (set by `-s M`, default: 1); parent reaps exited children with `waitpid(..., WNOHANG`.

- **IPC messaging**  
  Uses `msget`/`msgsnd`/`msgrcv()` to dispatch time quanta and receive status codes (`0` = terminated, `1` = full quantum, `-1` = blocked).

- **Blocking & unblocking**  
Processes that rsend a `-1` status (block request) are moved into the blocked queue with a randomized wait (0-5 s + 0-1 ms). Once their wait elapses, they re-enter `Q0`.

- **Configurable parameters**  
  - `-t T` sets upper bound (s) for children run time (default: 1).
  - `-1 I` sets minimum interval (ns) between forks (default: 0).
  - `-f` enables real-time logging to `ossLog.txt`.
 
- **Real-time safety**  
  Installs `SIGALRM` via `alarm(3)` to kill any remaining children after 3 s and clean up shared memory and message queue.

---

## Build & Run

```bash
# 1. Clone
git clone git@github:mgarson/oss-mlfq-manager.git
cd oss-mlfq-manager

# 2. Build
make

# 3. Usage
./oss [-h] [-n N] [-s M] [-t T] [-i I] [-f]

# Options
#   -h         Show help
#   -n N       Total child processes (default: 1)
#   -s M       Max simultaneous workers (default: 1)
#   -t T       Upper bound (s) for child run time (default: 1)
#   -i I       Min interval (ns) between forks (default: 0)
#   -f         Log output to ossLog.txt

# Examples
./oss -n 50 -s 10 -t 2 -i 10000000  # Launch 50 workers, 10 at a time; 2 s max runtime; 10 ms fork interval
./oss -n 100 -f                     # Launch 100 workers (default 1 at a time, 1 s max runtime, 0 ms fork interval) with logging enabled
```

---

## Technical Highlights

- **Queue-based scheduling**  
  Maintains three `std::queue<int>` structures (`Q0-Q2`) and a `blockedQueue`, dynamically promoting process indeces bases on dispatch and blocking events.

- **Time quantum dispatch**  
  Selects the highest-priority non-empty queue, dequeues an index, and sends its PID a quantum via `msgsnd()`, then waits for the reply with `msgrcv()`.

- **Accurate metrics tracking**  
  Records per-process service time, wait time, turnaround time, and blocked time in each PCB entry; computes averages and and CPU utilization at termination.

- **Graceful cleanup**  
  On `SIGALRM`, iterates PCB entries to kill orphans, then detaches/removes shared memory (`shmdt()`, `shmctl()`) and the message queue (`msgctl()`).

- **Adaptive clock ticks**  
  Adds both scheduling overhead (`addOverhead()`) and fixed 10 ms increments (`incrementClock`), ensuring the simulated clock reflects both dispatch and context-switch costs.

