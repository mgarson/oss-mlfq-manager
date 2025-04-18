// Operating Systems Project 4
// Author: Maija Garson
// Date: 04/18/2025
// Description: A program that simulates an operating system scheduler using a multi-level feedback queue.
// This program will run in a loop until it forks up to 100 child processes, with 18 allowed simultaneously.
// This program creates a system clock in shared memory and uses it to determine time within the program. 
// Information about the child processes are stored in a Process Control Block table. This program schedules 
// children by sending messages to the child processes using a message queue, these messages sent the child
// the total allowed time quantum it can run before stopping. The quantum is based on which queue the child
// is in at that time. It will always schedule the children in the highest priority queue. It will then wait
// for a response from the child, which informs oss how much of its time it used and also its status (terminated,
// blocked, or used full quantum). This program then puts the processes that did not terminate back into a
// queue based on its status and the queue it was previously in. This program will also print the PCB table
// and queue states every half second of system time. At the end, it will calculate and print statistics based on the run.
// The program will send a kill signal to all processes and terminate if 3 real-life seconds are reached.

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string>
#include <queue>

#define PERMS 0644

using namespace std;

// Structure for Process Control Block
typedef struct 
{
	int occupied; // Either true or false
	pid_t pid; // Process ID of this child
	int startSeconds; // Time when it was forked
	int startNano; // Time when it was forked
	int messagesSent; // Total times oss sent a message to it
	int serviceTimeSeconds; // Total CPU service time in sec
	int serviceTimeNano; // Total CPU service time in ns
	int eventWaitSec; // Total wait time in sec
	int eventWaitNano; // Total wait time in ns
	int blocked; // Indicates if process is blocked
} PCB;

// Message buffer for communication between OSS and child processes
typedef struct msgbuffer 
{
	long mtype; // Message type used for message queue
	char strData[100]; // String data to be sent. Will either be "0"(finished working) or "1"(still working)
	int intData; // Integer data to be sent in the message
} msgbuffer;

// Global variables
PCB* processTable; // Process control block table to track child processes

int *shm_ptr; // Shared memory pointer to store system clock
int shm_id; // Shared memory ID

int msqid; // Queue ID for communication

FILE* logfile = NULL; // Pointer to logfile

// Priority queues for scheduling
queue<int> rQueue0;
queue<int> rQueue1;
queue<int> rQueue2;
queue<int> blockedQueue;

// Time quantums for each priority queue in ns
const int baseq = 10000000;
const int q0 = baseq;
const int q1 = 2 * baseq;
const int q2 = 4 * baseq;

// Variables to track statistics
long long totalTurnaroundTimeNs = 0; 
long long terminatedProcesses = 0;
long long totalIdleTimeNs = 0;
long long totalServiceTimeNs = 0;
long long totalBlockedTimeNs = 0;

// Variables to hold amount of additons to queue
int q0Count = 0;
int q1Count = 0;
int q2Count = 0;
int blockedCount = 0;

// Function to increment system clock in seconds and nanoseconds
void incrementClock()
{
	// Update nanoseconds and check for overflow
	shm_ptr[1] += 10000000;
	if (shm_ptr[1] >= 1000000000) // Determines if nanosec is gt 1 billion, meaning it should convert to 1 second
	{
		shm_ptr[1] -= 1000000000;
		shm_ptr[0]++;
	}

}

// Function to add a small overhead of 1000 ns to the clock (less amount than incrmenting clock)
void addOverhead()
{
	// Increment ns in shared memor
	shm_ptr[1] += 1000;
	// Check for overflow
	if (shm_ptr[1] >= 1000000000)
	{
		shm_ptr[1] -= 1000000000;
		shm_ptr[0]++;
	}
}

// Function to access and add to shared memory
void shareMem()
{
	// Generate key
	const int sh_key = ftok("main.c", 0);
	// Create shared memory
	shm_id = shmget(sh_key, sizeof(int) * 2, IPC_CREAT | 0666);
	if (shm_id <= 0) // Check if shared memory get failed
	{
		// If true, print error message and exit
		fprintf(stderr, "Shared memory get failed\n");
		exit(1);
	}
	
	// Attach shared memory
	shm_ptr = (int*)shmat(shm_id, 0, 0);
	if (shm_ptr <= 0)
	{
		fprintf(stderr, "Shared memory attach failed\n");
		exit(1);
	}
	// Initialize shared memory pointers to represent clock
	// Index 0 represents seconds, index 1 represents nanoseconds
	shm_ptr[0] = 0;
	shm_ptr[1] = 0;
}

// FUnction to print formatted process table and contents of the three different priority queues
void printInfo(int n)
{
	
	// Print to console
	printf("OSS PID: %d SysClockS: %u SysClockNano: %u\n Process Table:\n", getpid(), shm_ptr[0], shm_ptr[1]);
	printf("Entry\tOccupied\tPID\tStartS\tStartNs\n");
	
	// Print to log file as well
	fprintf(logfile, "OSS PID: %d SysClockS: %u SysClockNano: %u\n Process Table:\n", getpid(), shm_ptr[0], shm_ptr[1]);
	fprintf(logfile,"Entry\tOccupied\tPID\tStartS\tStartNs\n");

	for (int i = 0; i < n; i++)
	{
		// Print table only if occupied by process
		if (processTable[i].occupied == 1)
		{
			printf("%d\t%d\t\t%d\t%u\t%u\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
			fprintf(logfile, "%d\t%d\t\t%d\t%u\t%u\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
		}
	}
	printf("\n");
	fprintf(logfile, "\n");
	

	
	// Create temporary queues to print contents of priority queues
	queue<int> temp0 = rQueue0;
	queue<int> temp1 = rQueue1;
	queue<int> temp2 = rQueue2;

	printf("----MLFQ State----\n");
	fprintf (logfile, "-----MLFQ State----\n");

	// Incrementally print each queue's contents
	// Queue 0
	printf("Queue 0: ");
	fprintf(logfile, "Queue 0: ");
	while(!temp0.empty())
	{
		int i = temp0.front();
		temp0.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	fprintf(logfile, "\n");
	
	// Queue 1
	printf("Queue 1: ");
	fprintf(logfile, "Queue 1: ");

	while(!temp1.empty())
	{
		int i = temp1.front();
		temp1.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	fprintf(logfile, "\n");

	// Queue 2
	printf("Queue 2: ");
	fprintf(logfile, "Queue 2: ");
	while (!temp2.empty())
	{
		int i = temp2.front();
		temp2.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	fprintf(logfile, "\n");

	printf("------------------\n\n\n");
	fprintf(logfile, "------------------\n\n\n");

}

// Signal handler to terminate all processes after 3 seconds in real time
void signal_handler(int sig)
{
	printf("3 seconds have passed, process(es) will now terminate.\n");
	pid_t pid;

	// Loop through process table to find all processes still running and terminate
	for (int i = 0; i < 18; i++)
	{
		if(processTable[i].occupied)
		{
			pid = processTable[i].pid;
			if (pid > 0)
				kill(pid, SIGKILL);
		}
	}
	 // Detach from shared memory and remove it
        if(shmdt(shm_ptr) == -1)
        {
                perror("shmdt failed");
                exit(1);
        }
        if (shmctl(shm_id, IPC_RMID, NULL) == -1)
        {
                perror("shmctl failed");
                exit(1);
        }

        // Remove the message queue
        if (msgctl(msqid, IPC_RMID, NULL) == -1)
        {
                perror("msgctl failed");
                exit(1);
        }


	exit(1);
}

int main(int argc, char* argv[])
{
	// Signal that will terminate program after 3 sec (real time)
	signal(SIGALRM, signal_handler);
	alarm(3);

	msgbuffer buf; // Buffer for sending messages to child processes
	msgbuffer rcvbuf; // Buffer for receiving messages from child processes
	key_t key; // Key to access queue

	// Create file to track message queue
	system("touch msgq.txt");

	// Get key for message queue
	if ((key = ftok("msgq.txt", 1)) == -1)
	{
		perror("ftok");
		exit(1);
	}

	// Create message queue
	if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1)
	{
		perror("msgget in parent\n");
		exit(1);
	}	

	printf("Message queue set up\n");

	logfile = fopen("ossLog.txt", "w");
	if(logfile == NULL)
	{
		fprintf(stderr, "Failed to open log file.\n");
		return EXIT_FAILURE;
	}

	// Values to keep track of child iterations
	int total = 0; // Total amount of processes
	int running = 0;
	//int lastForkSec = 0; // Time in sec since last fork
	//int lastForkNs = 0; // Time in ns since last fork
	int msgsnt = 0;

	const char optstr[] = "hn:s:t:i:f"; // Options h, n, s, t, i, f
	char opt;
	
	// Set up shared memory for clock
	shareMem();

	// Allocate memory for process table based on total processes
	processTable = new PCB[20];

	// Variables to track last printed time
	long long int lastPrintSec = shm_ptr[0];
	long long int lastPrintNs = shm_ptr[1];

	// Initialize process table, all values set to 0
	for (int i = 0; i < 18; i++)
	{
		processTable[i].occupied = 0;
		processTable[i].serviceTimeSeconds = 0;
		processTable[i].serviceTimeNano = 0;
		processTable[i].eventWaitSec = 0;
		processTable[i].eventWaitNano = 0;
		processTable[i].blocked = 0;
	}

	// Max number of sec allowed between spawning processes
	const int maxBetProcSec = 1;
	// Max number of ns allowed between spawning processes
	const int maxBetProcNs = 1000;
	// Variable to hold current time in ns. Calculate using memory pointers representng system time.
	long long currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];
	// Generate random number of sec and ns between 0 through max allowed
	int randSec = rand() % (maxBetProcSec + 1);
	int randNs = rand() % (maxBetProcNs + 1);
	// Calculate total random delay in ns
	long long randDelay = ((long long)randSec * 1000000000) + randNs;
	// Calculate next spawn time in ns by adding random delay to current time
	long long nSpawnT = currTimeNs + randDelay;

	// Loop that will continue until amount of 100 total child processes is reached or until running processes is 0
	// Ensures only 100 total  processes are able to run, and that no processses are still running when the loop ends
	while (total < 100 ||  running > 0)
	{
		// Update system clock
		incrementClock();
		long long currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];

		// Calculate time since last print for sec and ns
		long long int printDiffSec = shm_ptr[0] - lastPrintSec;
		long long int printDiffNs = shm_ptr[1] - lastPrintNs;
		// Adjust ns value for subtraction resulting in negative value
		if (printDiffNs < 0)
		{
			printDiffSec--;
			printDiffNs += 1000000000;
		}
		// Calculate total time sincd last print in ns
		long long int printTotDiff = printDiffSec * 1000000000 + printDiffNs;

		if (printTotDiff >= 500000000) // Determine if time of last print surpasssed .5 sec system time
		{
			// If true, print table and MLFQ info and update time since last print in sec and ns
			printInfo(18);
			lastPrintSec = shm_ptr[0];
			lastPrintNs = shm_ptr[1];
		}

		// Temp queue to store processes still blocked
		queue<int> tempBlocked;
		// Iterate through blocked queue
		while(!blockedQueue.empty())
		{
			// Take first process index from front of blocked queue and remove from queue
			int bIndx = blockedQueue.front();
			blockedQueue.pop();

			// Determine what time process will finish being blocked in ns
			long long waitEndNs = ((long long)processTable[bIndx].eventWaitSec * 1000000000) + processTable[bIndx].eventWaitNano;
			if (currTimeNs >= waitEndNs) // Process passed blocked time
			{
				// Push blocked process to top priority queue
				rQueue0.push(bIndx);

				// Increment total time blocked by amount of time process waited while blocked
				totalBlockedTimeNs += ((long long)processTable[bIndx].eventWaitSec * 1000000000) + processTable[bIndx].eventWaitNano;
				
				// Reset process's values in PCB to reflect no longer being blocked
				processTable[bIndx].eventWaitSec = 0;
				processTable[bIndx].eventWaitNano = 0;
				processTable[bIndx].blocked = 0;

				// Add additional overhead for blocked scheduling
				addOverhead();
				addOverhead();
			}
			else // Process has not passed blocked time
				// Add process back to temp block queue
				tempBlocked.push(bIndx);
				
		}
		// Set blocked queue equal to temp blocked queue, adding back in all blocked process in original order
		blockedQueue = tempBlocked;

		// Update variable holding clock time in ns to system's current time in ns
		currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];

		// Determine if a new child process can be spawned
		// Must be greater than next spawn time, less than total process allowed (100), and less than simultanous processes allowed (18)
		if (currTimeNs >= nSpawnT && total < 100  && running < 18)
		{
			//Fork new child
			pid_t childPid = fork();
			if (childPid == 0) // Child process
			{
				// Create array of arguments to pass to exec. "./worker" is the program to execute, arg is the command line argument
				// to be passed to "./worker", and NULL shows it is the end of the argument list
				char* args[] = {"./worker",  NULL};
				// Replace current process with "./worker" process and pass iteration amount as parameter
				execvp(args[0], args);
				// If this prints, means exec failed
				// Prints error message and exits
				fprintf(stderr, "Exec failed, terminating!\n");
				exit(1);
			}
			else // Parent process
			{
				// Increment total created processes and running processes
				total++;
				running++;
					
				// Increment clock
				incrementClock();

				// Update table with new child info
				for (int i = 0; i < 18; i++)
				{
					if (processTable[i].occupied == 0)
					{
						processTable[i].occupied = 1;
						processTable[i].pid = childPid;
						processTable[i].startSeconds = shm_ptr[0];
						processTable[i].startNano = shm_ptr[1];
						processTable[i].serviceTimeSeconds = 0;
						processTable[i].serviceTimeNano = 0;
						processTable[i].eventWaitSec = 0;
						processTable[i].eventWaitNano = 0;
						processTable[i].blocked = 0;
						// Add process index to queue 0 since this process is ready to be scheduled
						rQueue0.push(i);
						// Increment insertions to queue 0
						q0Count++;
						break;
					}
				}

				// Calculate next randomly generated spawn time in ns
				randSec = rand() % (maxBetProcSec);
				randNs = rand() % (maxBetProcNs);
				randDelay = (randSec * 1000000000) + randNs;
				currTimeNs = (shm_ptr[0] * 1000000000) + shm_ptr[1];
				nSpawnT = currTimeNs + randDelay;

			}
		}


		// Pointer to an integer queue to track the current ready queue being used
		std::queue<int>* currQueue = nullptr;
		// Find the first nonempty queue and set currQueue to point to it
		if (!rQueue0.empty())
			currQueue = &rQueue0;
		else if (!rQueue1.empty())
			currQueue = &rQueue1;
		else if (!rQueue2.empty())
			currQueue = &rQueue2;

		if (currQueue != nullptr && !currQueue->empty())
		{
			// Retreive index of process that is at the front of the current queue
			int indx = currQueue->front();
			// Remove this process index from the current queue since it is being processed
			currQueue->pop();

			// Find pid of corresponding index in process table and set to childP
			pid_t childP = processTable[indx].pid;

			// Find time quantum for child process based on the queue it was in. 
			// Process will be given this amount of time to run
			int quantum = q0;
			const char* qLevStr = "0";
			if (currQueue == &rQueue1)
			{
				quantum = q1;
				qLevStr = "1";
			}
			else if (currQueue == &rQueue2)
			{
				quantum = q2;
				qLevStr = "2";
			}

			// Call function to add scheduling overhead to OS 
			addOverhead();

			// Prepare info for message to child
			buf.mtype = childP;
			buf.intData = quantum;
			strcpy(buf.strData, "1");

			// Send message to child process
			if (msgsnd(msqid, &buf, sizeof(msgbuffer) - sizeof(long), 0) != -1)
			{
				msgsnt++; // Increment amount of messages sent
				fprintf(logfile, "Dispatching process (index %d, queue level %s) with PID %d, time quantum %d at time %d:%d\n",
						indx, qLevStr, childP, quantum, shm_ptr[0], shm_ptr[1]);
			}

			// Wait for child's reply
			if (msgrcv(msqid, &rcvbuf, sizeof(msgbuffer) -sizeof(long), childP, 0) == -1)
			{
				perror("msgrcv failed");
				exit(1);
			}			

			childP = rcvbuf.mtype;

			// Update variable holding clock time in ns to system's current time in ns
			currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];
			
			fprintf(logfile, "Receiving message from worker (index %d PID %d) at time %d:%d\n",
					indx, rcvbuf.intData, shm_ptr[0], shm_ptr[1]);

			// Retrieve message from worker containing amount of simulated time they used in ns
			int quanUsed = rcvbuf.intData;
			// Increment system clock by that time in ns
			shm_ptr[1] += quanUsed;
			if (shm_ptr[1] >= 1000000000) 
			{
				// If ns overflowed into seconds, adjust system time for this
				shm_ptr[0] += shm_ptr[1] / 1000000000;
				shm_ptr[1] %= 1000000000;
			}

			// Split quantum into whole seconds and leftover ns
			long long usedSec = quanUsed / 1000000000;
			long long usedNs = quanUsed % 1000000000;

			// Add used seconds and ns to process's service time in PCB
			processTable[indx].serviceTimeSeconds += usedSec;
			processTable[indx].serviceTimeNano += usedNs;
			if (processTable[indx].serviceTimeNano >= 1000000000)
			{
				// If ns overflowed into seconds, adjust PCB values for this
				processTable[indx].serviceTimeSeconds += processTable[indx].serviceTimeNano / 1000000000;
				processTable[indx].serviceTimeNano %=  1000000000;
			}

			// Increment total service time in ns by the quantum used
			totalServiceTimeNs += quanUsed;

                        // Determine if child sent 0, meaning child will terminate
                        if (strcmp(rcvbuf.strData, "0") == 0 || rcvbuf.intData == 0)
                        {
				// Calculate when process was forked
				long long arrivalTimeNs = ((long long) processTable[indx].startSeconds * 1000000000) + processTable[indx].startNano;
				currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];
				// Calculate process's turnaround time by finding difference between current time and time process forked
				long long turnaroundNs = currTimeNs - arrivalTimeNs;
				// Add to total turnaround time
				totalTurnaroundTimeNs += turnaroundNs;

				// Increment terminated processes
				terminatedProcesses++;

				// Wait for child to terminate
                                waitpid(childP, NULL, 0);
				// Update process table to reflect terminated child
                                processTable[indx].occupied = 0;
                                // Decrement amount of processes currently running
				running--;      
					
			}
			else if (strcmp(rcvbuf.strData, "-1") == 0) // If worker sends -1, means it was blocked
			{
				// Randomly generate amount of time process will remain blocked 
				int blockSec = rand() % 6;
				int blockNs = (rand() % 1001) * 1000000;
				// Update process wait time in PCB
				processTable[indx].eventWaitSec += blockSec;
				processTable[indx].eventWaitNano += blockNs;
				if(processTable[indx].eventWaitNano >= 1000000000)
				{
					// Adjust values if ns overflows to seconds
					processTable[indx].eventWaitSec += processTable[indx].eventWaitNano / 1000000000;
					processTable[indx].eventWaitNano %= 1000000000;
				}
				// Mark process as blocked in PCB
				processTable[indx].blocked = 1;

				// Add process to blocked queue
				blockedQueue.push(indx);
				// Increment amount of total blocked processes
				blockedCount++;
				// Add process's blocked time to total time blocked in ns
				totalBlockedTimeNs += ((long long)blockSec * 1000000000) + blockNs;

				// Additional overhead added for handling blocked process
				addOverhead();
				addOverhead();
			}

			else // Process did not finish
                        {
				// Add process to one queue lower than where it was scheduled from
                                if (currQueue == &rQueue0)
				{	
					rQueue1.push(indx);
					q1Count++;
				}
                                else if (currQueue == &rQueue1) 
				{
					rQueue2.push(indx);
					q2Count++;
				}
                                else
				{	
					rQueue2.push(indx);
					q2Count++;
				}
                        }

		}
		else
		{
			// No process is ready, simulate cpu idle time and increment total idle time by this amount in ns
			incrementClock();
			totalIdleTimeNs += 10000000;
		}		
			

	}

	// Print total amounts
	printf("Total processes launched: %d\n", total);
	printf("Total messages sent by OSS: %d\n\n", msgsnt);
 	fprintf(logfile, "Total processes launched: %d\n", total);
	fprintf(logfile, "Total messages sent by OSS: %d\n\n", msgsnt);
	
	printf("Total insertions in Queue 0: %d\n", q0Count);
	printf("Total insertions in Queue 1: %d\n", q1Count);
	printf("Total insertions in Queue 2: %d\n", q2Count);
	printf("Total insertions in Blocked Queue: %d\n\n", blockedCount);
	
	fprintf(logfile, "Total insertions in Queue 0: %d\n", q0Count);
	fprintf(logfile, "Total insertions in Queue 1: %d\n", q1Count);
	fprintf(logfile, "Total insertions in Queue 2: %d\n", q2Count);
	fprintf(logfile, "Total insertions in Blocked Queue: %d\n\n", blockedCount);

	// Calculate the final simulated time
	currTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];

	// Varialbes to find overall statistics
	long long avgTurnaroundNs = 0;
	long long avgServiceNs = 0;
	long long avgWaitNs = 0;
	long long cpuUtil = 0;

	if (terminatedProcesses > 0) // Ensure there are terminated processess before calculated values, otherwise they remain 0
	{
		// Calculate average turnaround time by dividing total turnaoround by amount of terminated processes
		avgTurnaroundNs = totalTurnaroundTimeNs / terminatedProcesses;
		// Calculate average service time by dividing total service time by amount of terminated processes
		avgServiceNs = totalServiceTimeNs / terminatedProcesses;
		// Calculate average wait time by finding diffeerence between average turnaround time and average service time
		avgWaitNs = avgTurnaroundNs - avgServiceNs;
	}

	// If current time is greater than 0, calculate CPU utilization by dividing total service time (*100) by final time in ns
	if (currTimeNs > 0)
		cpuUtil = (totalServiceTimeNs * 100) / currTimeNs;

	// Print final statistics
	printf("----Final Statistics----\n");
	printf("Total simulated time: %lld ns\n", currTimeNs);
	printf("Terminated processes: %lld\n", terminatedProcesses);
	printf("Average turnaround time: %lld ns\n", avgTurnaroundNs);
	printf("Average CPU service time: %lld ns\n", avgServiceNs);
	printf("Average wait time: %lld ns\n", avgWaitNs);
	printf("CPU utilization: %lld\n", cpuUtil);
	printf("Total CPU idle time: %lld ns\n", totalIdleTimeNs);

	fprintf(logfile, "----Final Statistics----\n");
	fprintf(logfile, "Total simulated time: %lld ns\n", currTimeNs);
	fprintf(logfile, "Terminated processes: %lld\n", terminatedProcesses);
	fprintf(logfile, "Average turnaround time: %lld ns\n", avgTurnaroundNs);
	fprintf(logfile, "CPU utilization: %lld\n", cpuUtil);
	fprintf(logfile, "Total CPU idle time: %lld ns\n", totalIdleTimeNs);


	// Detach from shared memory and remove it
	if(shmdt(shm_ptr) == -1)
	{
		perror("shmdt failed");
		exit(1);
	}
	if (shmctl(shm_id, IPC_RMID, NULL) == -1)
	{
		perror("shmctl failed");
		exit(1);
	}

	// Remove the message queue
	if (msgctl(msqid, IPC_RMID, NULL) == -1)
	{
		perror("msgctl failed");
		exit(1);
	}

	return 0;

}




