// user.c
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

// shared nanoseconds value
int* nanoSeconds = NULL;

// shared seconds value
int* seconds = NULL;

// process name
char* processName = NULL;

void getExistingIPC();
void detachIPC();

int main(int argc, char** argv)
{
	processName = argv[0];

	getExistingIPC();

	detachIPC();

	return 0;
}

void getExistingIPC()
{
	nanoSeconds = getExistingSharedMemory(ID_SHARED_NANO_SECONDS, processName);
	seconds = getExistingSharedMemory(ID_SHARED_SECONDS, processName);
}

void detachIPC()
{
	if (shmdt(nanoSeconds) == -1)
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if (shmdt(seconds) == -1)
		writeError("Failed to detach from seconds memory\n", processName);
}
