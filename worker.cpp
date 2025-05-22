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
	
	// Info needed for message sending/receiving
	msgbuffer buf;
	buf.mtype = 1;
	int msqid = 0;
	key_t key;

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

	srand(getpid());

	// Loop that loop suntil determined end time is reached
	while(true)
	{
		// Receive message from parent
		if (msgrcv(msqid, &buf, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1)
		{
			perror("msgrcv failed");
			exit(1);
		}

		// Get time quantum given from parent in message. This is amount of time child runs
		int quantum = buf.intData;

		// Generate random number to determine child's outcome in this iteration
		int outcome = rand() % 100;
		// Bool to represent if child should terminate, set to false initially
		bool termNow = false;
		bool blockNow = false;
		// Stores what child's time quantum will be based on outcome generated. Initally set to full quantum
		int effQuantum = quantum;

		// If less than 20, early termination
		if (outcome < 20)
		{
			// If quantum is greater than 1, set effQuantum to random number less than full quantum
			if (quantum > 1)
				effQuantum = rand() % quantum;
			// Else set effQuantum to the full quantum
			else
				effQuantum = quantum;
			// Set to true, so process will temrinate after using effQuantum time
			termNow = true;
		}
		// Else outcome between 20-49, simulate I/O interrupt. Process will not terminate after this iteration
		else if (outcome < 50)
		{
			// If quantum is greater than 1, set effQuantum to random number less than full quantum
			if (quantum > 1)
				effQuantum = rand() % quantum;
			// Else set effQuantum to full quantum
			else effQuantum = quantum;
			blockNow = true;
		}
		

		// Get info to send message back to parent
		buf.mtype = getpid();
	 	buf.intData = effQuantum;	
		if (termNow)
			strcpy(buf.strData, "0");
		else if (blockNow)
			strcpy(buf.strData, "-1");
		else
			strcpy(buf.strData, "1");

		// Send message back to parent that process is still running
		if (msgsnd(msqid, &buf, sizeof(msgbuffer)-sizeof(long), 0) == -1)
		{
			perror("msgsnd to parent failed.\n");
			exit(1);
		}

		if (termNow)
			break;
	}
	
	// Detach from memory
	if (shmdt(shm_ptr) == -1)
	{
		perror("memory detach failed in worker\n");
		exit(1);
	}

	return 0;
}
