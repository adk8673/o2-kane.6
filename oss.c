// oss.c
// CS 4760 Project 6 
// Alex Kane 4/19/2018
// Master executable code - contains most of application logic
#include<unistd.h>
#include<math.h>
#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/sem.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<time.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"QueueUtilities.h"
#include"PeriodicTimer.h"
#include"ProcessControlBlock.h"
#include"ProcessUtilities.h"
#include"StringUtilities.h"

#define ID_SHARED_NANO_SECONDS 1
#define ID_SHARED_SECONDS 2
#define NANO_PER_SECOND 1000000000

// shmid NanoSeconds
int shmidNanoSeconds = 0;

// pointer to shared nanoseconds
int* nanoSeconds = NULL;

// shmid seconds
int shmidSeconds = 0;

// pointer to shared seconds
int* seconds = NULL;

// Shared process name
char* processName = NULL;

void allocateSharedIPC();
void deallocateSharedIPC();
void executeOSS();

int main(int argc, char** argv)
{
	processName = argv[0];

	allocateSharedIPC();

	executeOSS();

	deallocateSharedIPC();

	return 0;
}

void executeOSS()
{
	*seconds = 0;
	*nanoSeconds = 0;
}

void allocateSharedIPC()
{
	if ((shmidNanoSeconds = allocateSharedMemory(ID_SHARED_NANO_SECONDS, sizeof(int), processName)) > 0)
	{
		if ((nanoSeconds = shmat(shmidNanoSeconds, 0, 0)) <= 0)
			writeError("Failed to attach to shared nanoseconds\n", processName);
	}
	else
		writeError("Failed to allocated shared nanoseconds memory\n", processName);

	if ((shmidSeconds = allocateSharedMemory(ID_SHARED_SECONDS, sizeof(int), processName)) > 0)
	{
		if ((seconds = shmat(shmidSeconds, 0, 0)) <= 0)
			writeError("Failed to attach to shared seconds\n", processName);
	}
	else
		writeError("Failed to allocated shared seconds memory\n", processName);
}

void deallocateSharedIPC()
{
	if (nanoSeconds != NULL && shmdt(nanoSeconds) == -1)
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if (shmidNanoSeconds > 0)
		deallocateSharedMemory(shmidNanoSeconds, processName);

	if (seconds != NULL && shmdt(seconds) == -1)
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if (shmidSeconds > 0)
		deallocateSharedMemory(shmidSeconds, processName);
}
