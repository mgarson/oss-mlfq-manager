// Operating Systems Project 3
// Author: Maija Garson
// Data: 03/21/2025
// Description: Program that launches from parent process. This process iterate through a loop until the time specified to end. This time is passed in by the parent and is then randomized with 
// //the passed value as upper bound. In the loop, it prints it's PID, parent's PID, its term time,  and the system timeIt sends a message of 1 to parent if it is still running, or 0 if it will end.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <cstdio>
#include <cstdlib>

#define PERMS 0644
typedef struct msgbuffer
{
	long mtype;
	char strData[100];
	int intData;
} msgbuffer;

int *shm_ptr;
int shm_id;

// Function to attach to shared memory
void shareMem()
{
	// Generate key
	const int sh_key = ftok("main.c", 0);
	// Access shared memory
	shm_id = shmget(sh_key, sizeof(int) * 2, 0666);

	// Determine if shared memory access not successful
	if (shm_id == -1)
	{
		// If true, print error message and exit 
		fprintf(stderr, "Child: Shared memory get failed.\n");
		exit(1);
	}

	// Attach shared memory
	shm_ptr = (int *)shmat(shm_id, 0, 0);
	//Determine if insuccessful
	if (shm_ptr == (int *)-1)
	{
		// If true, print error message and exit
		fprintf(stderr, "Child: Shared memory attach failed.\n");
		exit(1);
	}
}


int main(int argc, char* argv[])
{
	shareMem();
	
	// Tracks time worker started
	int startSec = shm_ptr[0];
	int startNs = shm_ptr[1];
	int lastKSec = startSec;

	// Represents time to end
	int endSec;
	int endNs;

	// Info needed for message sending/receiving
	msgbuffer buf;
	buf.mtype = 1;
	int msqid = 0;
	key_t key;

	if (argv[2] != nullptr) // Determines if second argument was passed, meaning value was given for termination for both sec (argv1) and nanosec(argv2)
	{
		endSec = shm_ptr[0] + atoi(argv[1]);
		endNs = shm_ptr[1] + atoi(argv[2]);
	}

	else if (argv[1] != nullptr) // Means no second argument given. Only upper value for seconds
	{
		// Convert first argument passed to int that represents upper value for random number to be found
		int upper = atoi(argv[1]);
		//Generate random run time using pid as seed
		srand(getpid());
		int runTimeSec = (rand() % upper) + 1;
		long long int runTimeNs = (rand() % 999999999);
		// Calculate time to end program based on random number found
		endSec = shm_ptr[0] + runTimeSec;
		endNs = shm_ptr[1] + runTimeNs;
	}
	else if (argv[1] == nullptr) // Invalid amount of arguments passed
	{
		fprintf(stderr, "Error! No value given for termination time. Program will now end.\n");
		return EXIT_FAILURE;
	}

	// Print starting message
	printf("Worker PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n", getpid(), getppid(), shm_ptr[0], shm_ptr[1], endSec, endNs);
	printf("--Just starting\n");

	

	// Get key for message queue
	if ((key = ftok("msgq.txt", 1)) == -1)
	{
		perror("ftok");
		exit(1);
	}

	// Create message queue
	if ((msqid = msgget(key, PERMS)) == -1)
	{
		perror("msgget in child\n");
		exit(1);
	}

	int i = 0; // Amount of iterations in while loop
	// Loop that loop suntil determined end time is reached
	while (shm_ptr[0] < endSec || (shm_ptr[0] == endSec && shm_ptr[1] < endNs))
	{
		// Receive message from parent
		if (msgrcv(msqid, &buf, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1)
		{
			perror("msgrcv failed");
			exit(1);
		}

		i++; // Increment iterations in loop

		printf("Worker PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n", getpid(), getppid(), shm_ptr[0], shm_ptr[1], endSec, endNs);
		printf("--%d iterations have passed since starting.\n", i);

		// Get info to send message back to parent
		buf.mtype = getppid(); 
		buf.intData = getpid();
		strcpy(buf.strData, "1");
		// Send message back to parent that process is still running
		if (msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1)
		{
			perror("msgsnd to parent failed.\n");
			exit(1);
		}
	}

	// Get info to send message back to parent 
	buf.mtype = getppid();
	buf.intData = getpid();
	strcpy(buf.strData, "0");
	// Send message back to parent that process is terminating
	if (msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1)
	{
		perror("msgsnd to parent failed.\n");
		exit(1);
	}

	printf("WORKER PID:%d PPID:%d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n", getpid(), getppid(), shm_ptr[0], shm_ptr[1], endSec, endNs);
	printf("--Terminating after sending message back to oss after %d iterations\n", i);
	
	// Detach from memory
	if (shmdt(shm_ptr) == -1)
	{
		perror("memory detach failed in worker\n");
		exit(1);
	}

	return 0;
}
