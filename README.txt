Operating Systems Project 4
Author: Maija Garson
Date: 03/21/2025

Description
This project will compile two programs into two executables using the makefile provided. One of the executables, oss, is generated from oss.cpp. The other executable,
worker, is generated from worker.cpp. The oss program launches the worker program as its child up to a specified amount of times.
The child processes run simultaneously up to a specified amount of times. The child process will also run up until a random time with the upper value for this random
time specified. The parent, oss, will determine when any child process(es) have ended by sending and receiving messages to/from the child.

Compilation
These programs will compile using the included makefile. In the command line it will compile if given:
make

Running the Program:
Once compiled, the oss program can be run with 5 options that are optional:
oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren]
Where
        -h: Display help message
        -n proc: Proc represents the amount of total child processes to launch
        -s simul: Simul represents the amount of child processes that can run simultaneously
        -t timelimitForChildren: Represents the upper bound for the randomized time limit that child process will run
        -i intervalInMsToLaunchChildren: Represents the interval in ms to launch the next child process
        -f logfile: Will print output from oss to logfile, while still printing to console
Default values for options n, s, and t will be 1 and for i will be 0 if not specified in the command line

Problems Encountered:
I struggled with resolving a deadlock situation multiple times in this project. Both the parent and child would be blocked, stopping program execution. I fixed this by
adjusting the order that messages are seent and received in both my oss and worker programs. This eventually resolved the issue.

I also had issues with the parent becoming blocked and the worker becoming stuck in an infinite loop without the clock incrementing. This was also resolved by adjusting the
order of messages.

Known Bugs:
I currently do not know of any bugs in this project.


