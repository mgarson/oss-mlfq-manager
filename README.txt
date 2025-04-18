Operating Systems Project 4
Author: Maija Garson
Date: 04/18/2025

Description
This project will compile two programs into two executables using the makefile provided. One of the executables, oss, is generated from oss.cpp. The other executable,
worker, is generated from worker.cpp. The oss program launches the worker program as its child with up to 100 total workers. The oss program simulates an OS scheduler
with a multi-level feedback queue containing 3 levels and a blocked queue. It will fork processes at randomly determined time intervals and will send them messages
in a message queue containing a time quantum that the child is able to run. Oss then waits for their reply, which indicates how much simulated time they used and 
their status (terminated, blocked, or used up time quantum). Oss also increments a system clock in shared memory and prints the state of the queues and each process's
PCB every half second of simulated time. At the end, it will calculate and print final statistics.
The child process will loop until it receives a message from oss containing its time quantum. It will then randomly generate a number 0-99 to determine if the child
will terminate, block from I/O, or use its full quantum. It will then send a message back to oss containing its status and time quantum used. 

Compilation
These programs will compile using the included makefile. In the command line it will compile if given:
make

Running the Program:
Once compiled, the oss program can be run by:
./oss
Each run will create a new log file named ossLog.txt, which will contain information about that run.

Problems Encountered:
I struggled with computing some of the final statsitics for this program. I had them ending up negative a lot, which I realized was due to overflow. Changing these
values from int to long long fixed this. I also had issues with the service time incrementing too high, I fixed this by finding an extra spot where I was incrementing this 
and removing it. 
I also had issues with the program getting stuck for a reason I could not initially figure out. It ending up being due to using 0 in the msgrcv vs the child's pid, so
oss would not grab a specific child's reply, but any in the queue. This caused some issues and oss would get stuck waiting until the sigkill. I fixed this
by having oss wait for a reply from a specific child pid instead of any reply in the queue.

Known Bugs:
I currently do not know of any bugs in this project.


