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
#include"BitVectorUtilities.h"
#include"PageTableEntry.h"
#include"PageFrame.h"
#include"Constants.h"

// shared nanoseconds value
int* nanoSeconds = NULL;

// shared seconds value
int* seconds = NULL;

// Semaphore id that guards critical sections
int semidCritical = 0;

// PCB
ProcessControlBlock* pcb = NULL;

// Page table
PageTableEntry* pageTable = NULL;

// page frame list
PageFrame* pageFrames = NULL;

// Id to message queue to receive requests from
int msgIdRequest = 0;

// Id to message queue to send grants to
int msgIdGrant = 0;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

// process name
char* processName = NULL;

void getExistingIPC();
void detachIPC();
int findProcessInPcb(pid_t);
int setMemoryAccesses();
int shouldEnd();
int shouldSegment();

int main(int argc, char** argv)
{
	processName = argv[0];

	// I've added this as a failsafe - oss should have ended and deallocated at this point, killing any
	// children, but in case they haven't, end so we don't have orphaned processes
	setPeriodic(5);

	getExistingIPC();

	executeUser();

	detachIPC();

	return 0;
}

void executeUser()
{
	struct sembuf semsignal[1];
	semsignal[0].sem_num = 0;
	semsignal[0].sem_op = 1;
	semsignal[0].sem_flg = 0;

	struct sembuf semwait[1];
	semwait[0].sem_num = 0;
	semwait[0].sem_op = -1;
	semwait[0].sem_flg = 0;

	mymsg_t requestMsg, grantMsg;
	requestMsg.mtype = getpid();

	if ( semop(semidCritical, semwait, 1) == -1 )
		writeError("Failed to wait for critical section entry\n", processName);

	int pcbIndex = findProcessInPcb(getpid());
	int maxMemoryAddress = pcb[pcbIndex].NumberOfPages * BYTE_PER_KILO;

	if ( semop(semidCritical, semsignal, 1) == -1 )
		writeError("Failed to signal for critical section exit\n", processName);

	int numAccesses = setMemoryAccesses();
	int endUser = 0;
	while ( endUser != 1 )
	{
		int requestedMemoryAddress;
		if ( shouldSegment() == 1 )
			requestedMemoryAddress = maxMemoryAddress + (rand() % ADDRESS_OVERAGE);
		else
			requestedMemoryAddress = (rand() % maxMemoryAddress) + 1;

		snprintf(requestMsg.mtext, 50, "%d", requestedMemoryAddress);

		int isWrite = 0;
		if ( rand() % MAX_PERCENT < PERCENT_WRITE )
			isWrite = 1;

		if ( semop(semidCritical, semwait, 1) == -1 )
			writeError("Failed to wait for critical section entry\n", processName);

		pcb[pcbIndex].IsWrite = isWrite;

		if ( semop(semidCritical, semsignal, 1) == -1 )
			writeError("Failed to signal for critical section exit\n", processName);

		if ( msgsnd(msgIdRequest, &requestMsg, sizeof(requestMsg), 0) == -1 )
			writeError("Failed to send request to oss\n", processName);

		if ( msgrcv(msgIdGrant, &grantMsg, sizeof(grantMsg), getpid(), 0) == -1 )
			writeError("Failed to receive message from oss\n", processName);

		int response = atoi(grantMsg.mtext);

		if ( response == 1 )
		{
			--numAccesses;

			if ( numAccesses <= 0 )
			{
				if ( shouldEnd() == 1 )
					endUser = 1;

				numAccesses = setMemoryAccesses();
			}
		}
		else if ( response == 0 )
		{
			int valueInMemory = 0;
			while ( valueInMemory != 1 )
			{
				if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

				if ( pcb[pcbIndex].BlockedOnPage == 0 )
					valueInMemory = 1;

				if ( semop(semidCritical, semsignal, 1) == -1 )
					writeError("Failed to signal for critical section exit\n", processName);
			}

			--numAccesses;
			if ( numAccesses <= 0 )
			{
				if ( shouldEnd() == 1 )
					endUser = 1;

				numAccesses = setMemoryAccesses();
			}
		}
		else if ( response == -1 )
		{
			printf("user over\n");
		}
	}

	printf("Ending child %d\n", getpid());
}

int shouldSegment()
{
	int shouldThrowSegmentationError = 0;

	if ( (rand() % MAX_PERCENT) + 100000 < PERCENT_SEGMENT )
		shouldThrowSegmentationError = 1;

	return shouldThrowSegmentationError;
}

int setMemoryAccesses()
{
	return BASE_MEMORY_ACCESSES + ( (rand() % ACCESSES_VARIATION) * (rand() % 1 == 1) ? -1 : 1);
}

int shouldEnd()
{
	int shouldEnd;
	if ( rand() % MAX_PERCENT < PERCENT_END )
		shouldEnd = 1;

	return shouldEnd;
}

int findProcessInPcb(pid_t pid)
{
	int index, found;
	for ( index = 0, found = 0; index < MAX_PROCESSES && !found; ++index)
	{
		if ( pcb[index].ProcessId == pid )
		{
			found = 1;
			break;
		}
	}

	if ( !found )
		index = -1;

	return index;
}

void getExistingIPC()
{
	nanoSeconds = getExistingSharedMemory(ID_SHARED_NANO_SECONDS, processName);
	seconds = getExistingSharedMemory(ID_SHARED_SECONDS, processName);
	semidCritical = getExistingSemaphore(ID_SEM_CRITICAL, processName);
	pageFrames = getExistingSharedMemory(ID_SHARED_FRAME, processName);
	pageTable = getExistingSharedMemory(ID_SHARED_PAGE, processName);
	pcb = getExistingSharedMemory(ID_SHARED_PCB, processName);
	msgIdRequest = getExistingMessageQueue(ID_MSG_REQUEST, processName);
	msgIdGrant = getExistingMessageQueue(ID_MSG_GRANT, processName);
}

void detachIPC()
{
	if ( shmdt(nanoSeconds) == -1 )
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if ( shmdt(seconds) == -1 )
		writeError("Failed to detach from seconds memory\n", processName);

	if ( shmdt(pageFrames) == -1 )
		writeError("Failed to detach from page frames memory\n", processName);

	if ( shmdt(pcb) == -1 )
		writeError("Failed to detach from pcb memory\n", processName);

	if ( shmdt(pageTable) == -1 )
		writeError("Failed to detach from page table\n", processName);
}
