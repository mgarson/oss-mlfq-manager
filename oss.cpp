// Operating Systems Project 4
// Author: Maija Garson
// Date: 04/15/2025
// Description: A program that simulates an operating system scheduler using a multi-level feedback queue.
// This program takes in command line options to determine total number of child processes to create, how
// many processes can run simultaneously, the time limit for each child process, and the interval between
// spawning children. This program creates a system clock in shared memory and uses it to determine time
// within the program. Information about the child processes are stored in a Process Control Block table.
// This program schedules children by sending messages to the child processes with a message queue and 
// receives responses from the children to determine if the child terminated or not.


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

//Structure to hold values for command line arguments
typedef struct
{
	int proc; // Number of processes
	int simul; // Number of simultaneous processes
	int timelim; 
	int interval;
	std::string logfile;
} options_t;


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

int running = 0; // Number of processes currently running
int sec = 0; // System clock seconds
int nanoSec = 0; // System clock nanoseconds
int increm; // Clock increment (based on number of children currently running)

FILE* logfile = NULL; // Pointer to logfile
bool logging = false; // Bool to determine if output should also print to logfile

// Priority queues for scheduling
queue<int> rQueue0;
queue<int> rQueue1;
queue<int> rQueue2;

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

void print_usage(const char * app)
{
       	fprintf (stdout, "usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren\n",\
                        app);
	fprintf (stdout, "      proc is the number of total children to launch\n");
	fprintf (stdout, "      simul indicates how many children are to be allowed to run simultaneously\n");
	fprintf (stdout, "      iter is the number to pass to the user process\n");

}

// Function to increment system clock in seconds and nanoseconds
// Increments based on how many child processes are currently running
void incrementClock()
{
	// Calculate increment based on number of currently running chldren
	if (running > 0) increm = 10000000 / running;
	else increm = 10000000;

	// Update nanoseconds and check for overflow
	nanoSec += increm;
	if (nanoSec >= 1000000000) // Determines if nanosec is gt 1 billion, meaning it should convert to 1 second
	{
		nanoSec -= 1000000000;
		sec++;
	}

	// Update shared memory pointers with calculated time
	shm_ptr[0] = sec;
	shm_ptr[1] = nanoSec;
}

// Function to add a small overhead of 1000 ns to the clock
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
	if (logging) // Determine if output should also print to logfile
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
	}
	else // Print only to console
	{
		printf("OSS PID: %d SysClockS: %u SysClockNano: %u\n Process Table:\n", getpid(), shm_ptr[0], shm_ptr[1]);
		printf("Entry\tOccupied\tPID\tStartS\tStartNs\n");
		for (int i = 0; i < n; i++)
		{
			// Print table only if occupied by process
			if(processTable[i].occupied == 1)
				printf("%d\t%d\t\t%d\t%u\t%u\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);

		}
	}
	
	// Create temporary queues to print contents of priority queues
	queue<int> temp0 = rQueue0;
	queue<int> temp1 = rQueue1;
	queue<int> temp2 = rQueue2;

	printf("\n");
	if (logging) fprintf(logfile, "\n");
	printf("----MLFQ State----\n");
	if (logging) fprintf (logfile, "-----MLFQ State----\n");

	// Incrementally print each queue's contents
	// Queue 0
	printf("Queue 0: ");
	if (logging) fprintf(logfile, "Queue 0: ");
	while(!temp0.empty())
	{
		int i = temp0.front();
		temp0.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		if (logging) fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	if (logging) fprintf(logfile, "\n");
	
	// Queue 1
	printf("Queue 1: ");
	if (logging) fprintf(logfile, "Queue 1: ");

	while(!temp1.empty())
	{
		int i = temp1.front();
		temp1.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		if (logging) fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	if (logging) fprintf(logfile, "\n");

	// Queue 2
	printf("Queue 2: ");
	if (logging) fprintf(logfile, "Queue 2: ");
	while (!temp2.empty())
	{
		int i = temp2.front();
		temp2.pop();
		printf("[Table Index: %d; PID: %d] ", i, processTable[i].pid);
		if (logging) fprintf(logfile, "[Table Index: %d; PID: %d] ", i, processTable[i].pid);
	}
	printf("\n");
	if (logging) fprintf(logfile, "\n");

	printf("------------------\n\n");
	if (logging) fprintf(logfile, "------------------\n\n");

}

// Signal handler to terminate all processes after 60 seconds in real time
void signal_handler(int sig)
{
	printf("60 seconds have passed, process(es) will now terminate.\n");
	pid_t pid;

	// Loop through process table to find all processes still running and terminate
	for (int i = 0; i < sizeof(processTable); i++)
	{
		if(processTable[i].occupied == 1)
			pid = processTable[i].pid;

		if (pid > 0)
			kill(pid, SIGKILL);
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
	int msqid; // Queue ID for communication
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

	// Structure to hold values for options in command line argument
	options_t options;

	// Set default values
	options.proc = 1;
	options.simul = 1;
	options.timelim = 1;
	options.interval = 0;

	// Values to keep track of child iterations
	int total = 0; // Total amount of processes
	int lastForkSec = 0; // Time in sec since last fork
	int lastForkNs = 0; // Time in ns since last fork
	int msgsnt = 0;

	const char optstr[] = "hn:s:t:i:f"; // Options h, n, s, t, i, f
	char opt;

	//Parse command line arguments with getopt
	while ( ( opt = getopt(argc, argv, optstr) ) != -1)
	{
		switch (opt)
		{
			case 'h': // Help
				// Prints usage
				print_usage(argv[0]);
				return (EXIT_SUCCESS);
			case 'n': // Total amount of processes
				// Check if n's argument starts with '-'
				if (optarg[0] == '-')
				{
					// Check if next character starts with other option, meaning no argument given for n and another option given
					if (optarg[1] == 's' || optarg[1] == 't' || optarg[1] == 'i' || optarg[1] == 'f' ||  optarg[1] == 'h')
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Option n requires an argument.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					// Means argument is not another option, but is invalid input
					else
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Invalid input.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}
				// Loop to ensure all characters in n's argument are digits
				for (int i = 0; optarg[i] != '\0'; i++)
				{
					if (!isdigit(optarg[i]))
					{
						// If non digit is found, print error statement, print usage, and exit program
						fprintf(stderr, "Error! %s is not a valid number.\n", optarg);
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}

				// Sets proc to optarg and breaks
				options.proc = atoi(optarg);
				break;

			case 's': // Total amount of processes that can run simultaneously
				// Checks if s's argument starts with '-'
				if (optarg[0] == '-')
				{
					// Checks if next character is character of other option, meaning no argument given for s and another option given
					if (optarg[1] == 'n' || optarg[1] == 't' || optarg[1] == 'i' || optarg[1] == 'f' ||  optarg[1] == 'h')
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Option s requires an argument.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					// Means argument is not another option, but is invalid input
					else
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Invalid input.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}
				// Loop to ensure all characters in s's argument are digits
				for (int i = 0; optarg[i] != '\0'; i++)
				{
					if (!isdigit(optarg[i]))
					{
						// If non digit is found, print error statement, print usage, and exit program
						fprintf(stderr, "Error! %s is not a valid number.\n", optarg);
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}

				// Sets simul to optarg and breaks
				options.simul = atoi(optarg);
				break;

			case 't': // Time limit for child processes to run
				// Checks if t's argument starts with '-'
				if (optarg[0] == '-')
				{
					// Checks if next character is characterof other option, meaning no argument given for t and another option given
					if (optarg[1] == 'n' || optarg[1] == 's' || optarg[1] == 'i' || optarg[1] == 'f' ||  optarg[1] == 'h')
					{ 
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Option t requires an argument.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
					// Means argument is not another option, but is invalid input
					else
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Invalid input.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}
				// Loop to ensure all characters in t's argument are digits
				for (int i = 0; optarg[i] != '\0'; i++)
				{
					if (!isdigit(optarg[i]))
					{
						// If non digit is found, print error statement, print usage, and exit program
						fprintf(stderr, "Error! %s is not a valid number.\n", optarg);
						return EXIT_FAILURE;
					}
				}

				// Sets timelim to optarg and breaks
				options.timelim = atoi(optarg);
				break;

			case 'i': // Interval in ns to launch children
				// Checks if i's argument starts with '-'
				if (optarg[0] == '-')
				{
					// Checks if next character is character of other option, meaning no argument given for i and another option given
					if (optarg[1] == 'n' || optarg[1] == 's' || optarg[1] == 't' || optarg[1] == 'f' ||  optarg[1] == 'h')
					{
						// Print error statement, print usage, and exit program
						fprintf(stderr, "Error! Option t requires an argument.\n");
						print_usage(argv[0]);
						return EXIT_FAILURE;
					}
				}
				// Loop to ensure all characters in i's argument are digits
				for (int i = 0; optarg[i] != '\0'; i++)
				{
					if (!isdigit(optarg[i]))
					{
						// If non digit is found, print error statement, print usage, and exit program
						fprintf(stderr, "Error! %s is not a valid number.\n", optarg);
						print_usage(argv[0]);
						return EXIT_FAILURE;
					} 
				}

				// Sets interval to optarg and breaks
				options.interval = atoi(optarg);
				break;

			 case 'f': // Print output also to logfile if option is passed
				logging = true;
				// Open logfile
				logfile = fopen("ossLog.txt", "w"); // Open logfile
				if (logfile == NULL)
				{
					fprintf(stderr, "Failed to open log file.\n");
					return EXIT_FAILURE;
				}
				break;

			default: 
				// Prints message that option given is invalid, prints usage, and exits program
				printf("Invalid option %c\n", optopt);
				print_usage(argv[0]);
				return EXIT_FAILURE;
		}
	}
	
	// Set up shared memory for clock
	shareMem();

	// Allocate memory for process table based on total processes
	processTable = new PCB[options.proc];

	// Variables to track last printed time
	long long int lastPrintSec = shm_ptr[0];
	long long int lastPrintNs = shm_ptr[1];

	// Initialize process table, all values set to 0
	for (int i = 0; i < options.proc; i++)
	{
		processTable[i].occupied = 0;
		processTable[i].serviceTimeSeconds = 0;
		processTable[i].serviceTimeNano = 0;
		processTable[i].eventWaitSec = 0;
		processTable[i].eventWaitNano = 0;
		processTable[i].blocked = 0;
	}

	// Convert timelim to string to be passed to child process
	string str = to_string(options.timelim);
	// Creates new char array to hold value to be passed into child program
	char* arg = new char[str.length()+1];
	// Copies str to arg so it is able to be passed into the child program
	strcpy(arg, str.c_str());

	// Max number of sec allowed between spawning processes
	const int maxBetProcSec = 1;
	// Max number of ns allowed between spawning processes
	const int maxBetProcNs = 1000000000;
	// Calculate current amount of time simulated in ns using shared memory pointers
	long long currSimT = (shm_ptr[0] * 1000000000) + shm_ptr[1];
	// Generate random number of sec and ns between 0 through max allowed
	int randSec = rand() % (maxBetProcSec + 1);
	int randNs = rand() % (maxBetProcNs + 1);
	// Calculate total random delay in ns
	long long randDelay = (randSec * 1000000000) + randNs;
	// Calculate next spawn time
	long long nSpawnT = currSimT + randDelay;

	// Loop that will continue until specified amount of child processes is reached or until running processes is 0
	// Ensures only the specified amount of processes are able to run, and that no processses are still running when the loop ends
	while (total < options.proc || running > 0)
	{
		// Update system clock
		incrementClock();
		long long currentTimeNs = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];

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
			printInfo(options.proc);
			lastPrintSec = shm_ptr[0];
			lastPrintNs = shm_ptr[1];
		}


		// Calculate current amount of time simulated in ns
		currSimT = (shm_ptr[0] * 1000000000) + shm_ptr[1];

		// Determine if a new child process can be spawned
		// Must be greater than next spawn time, less than total process allowed, and less than simultanous processes allowed
		// Also ensures no more than 100 processes are spawned
		if (currSimT >= nSpawnT && total < options.proc && total < 100  && running < options.simul)
		{	
			//Fork new child
			pid_t childPid = fork();
			if (childPid == 0) // Child process
			{
				// Create array of arguments to pass to exec. "./worker" is the program to execute, arg is the command line argument
				// to be passed to "./worker", and NULL shows it is the end of the argument list
				char* args[] = {"./worker", arg, NULL};
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
				for (int i = 0; i < options.proc; i++)
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
						break;
					}
				}

				// Update time since last fork to current system time
				lastForkSec = shm_ptr[0];
				lastForkNs = shm_ptr[1];

				// Calculate next randomly generated spawn time
				randSec = rand() % (maxBetProcSec + 1);
				randNs = rand() % (maxBetProcNs + 1);
				randDelay = (randSec * 1000000000) + randNs;
				nSpawnT = currSimT + randDelay;

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
				printf("Dispatching process (index %d, queue level %s) with PID %d, time quantum %d ns at time %d:%d\n",
					       indx, qLevStr, childP, quantum, shm_ptr[0], shm_ptr[1]);
				if (logging)
					fprintf(logfile, "Dispatching process (index %d, queue level %s) with PID %d, time quantum %d at time %d:%d\n",
							indx, qLevStr, childP, quantum, shm_ptr[0], shm_ptr[1]);
			}


			// Variable to hold msgrcv value
			int ret;
			// Loop until a message from the child process is received while using IPC_NOWAIT to ensure parent can continue
			while ((ret = msgrcv(msqid, &rcvbuf, sizeof(msgbuffer) - sizeof(long), childP, IPC_NOWAIT)) == -1)
			{
				// Means msgrcv failed. Print error and exit program
				if (errno != ENOMSG)
				{
					perror("msgrcv failed");
					exit(1);
				}
				// Continue incrementing the clock even if no message has been received yet
				// This ensure that clock continues incrementing while child process is running
				incrementClock();
			}
			

			// Update the current clock time in ns
			currentTimeNs = ((long long)shm_ptr[0] * 100000000) + shm_ptr[1];
			
			printf("Receiving message from worker (index  %d PID %d) at time %d:%d\n",
					indx, rcvbuf.intData, shm_ptr[0], shm_ptr[1]);
			if (logging)
				fprintf(logfile, "Receiving message from worker (index %d PID %d) at time %d:%d\n",
						indx, rcvbuf.intData, shm_ptr[0], shm_ptr[1]);


                        // Determine if child sent 0, meaning child will terminate
                        if (strcmp(rcvbuf.strData, "0") == 0 || rcvbuf.intData < 0)
                        {
				// Calculate when process was forked
				long long arrivalTimeNs = ((long long) processTable[indx].startSeconds * 1000000000) + processTable[indx].startNano;
				// Calculate process's turnaround time by finding difference between current time and time process forked
				long long turnaroundNs = currentTimeNs - arrivalTimeNs;
				// Add to total turnaround time
				totalTurnaroundTimeNs += turnaroundNs;

				// Calculate process's service time and add to total service time
				long long procService = ((long long) processTable[indx].serviceTimeSeconds * 1000000000) + processTable[indx].serviceTimeNano;
				totalServiceTimeNs += procService;

				// Increment terminated processes
				terminatedProcesses++;

				// Wait for child to terminate
                                waitpid(childP, NULL, 0);
				// Update process table to reflect terminated child
                                processTable[indx].occupied = 0;
                                // Decrement amount of processes currently running
				running--;      
					
			}

			else // Process did not finish
                        {
				// Add process to one queue lower than where it was scheduled from
				if (processTable[indx].occupied != 0)
				{
                                	if (currQueue == &rQueue0)
                                                rQueue1.push(indx);
                                	else if (currQueue == &rQueue1)
                                                rQueue2.push(indx);
                                        else
                                                rQueue2.push(indx);
				}
                        }

			
	
			incrementClock();
		}
		else
		{
			// No process is ready, simulate cpu idle time
			incrementClock();
			totalIdleTimeNs += 10000000;
		}		
			

	}
	printf("Total processes launched: %d\n", total);
	printf("Total messages sent by OSS: %d\n", msgsnt);
	if (logging)
	{
 		fprintf(logfile, "Total processes launched: %d\n", total);
		fprintf(logfile, "Total messages sent by OSS: %d\n", msgsnt);
	}

	// Calculate the final simulated time
	long long finalSimT = ((long long)shm_ptr[0] * 1000000000) + shm_ptr[1];

	// Varialbes to find overall statistics
	long long avgTurnaroundNs = 0;
	long long avgServiceNs = 0;
	long long avgWaitNs = 0;
	long long cpuUtil = 0;

	if (terminatedProcesses > 0) // Ensure there are terminated processess before calculated values, otherwise they remain 0
	{
		// Calculate average turnaround time by divinding total turnaoround by amount of terminated processes
		avgTurnaroundNs = totalTurnaroundTimeNs / terminatedProcesses;
		// Calculate average service time by dividing total service time by amount of terminated processes
		avgServiceNs = totalServiceTimeNs / terminatedProcesses;
		// Calculate average wait time by finding diffeerence between average turnaround time and average service time
		avgWaitNs = avgTurnaroundNs - avgServiceNs;
	}

	if (finalSimT > 0)
		cpuUtil = (totalServiceTimeNs * 100) / finalSimT;

	printf("----Final Statistics----\n");
	printf("Total simulated time: %lld ns\n", finalSimT);
	printf("Terminated processes: %lld\n", terminatedProcesses);
	printf("Average turnaround time: %lld ns\n", avgTurnaroundNs);
	printf("Average CPU service time: %lld ns\n", avgServiceNs);
	printf("Average wait time: %lld ns\n", avgWaitNs);
	printf("CPU utilization: %lld\n", cpuUtil);
	printf("Total CPU idle time: %lld ns\n", totalIdleTimeNs);

	
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




