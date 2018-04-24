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
#include"BitVectorUtilities.h"
#include"PageTableEntry.h"
#include"PageFrame.h"
#include"Constants.h"

// shmid NanoSeconds
int shmidNanoSeconds = 0;

// pointer to shared nanoseconds
int* nanoSeconds = NULL;

// shmid seconds
int shmidSeconds = 0;

// pointer to shared seconds
int* seconds = NULL;

// shmid of shared process control block
int shmidPCB = 0;

// PCB
ProcessControlBlock* pcb = NULL;

// shmid of page table
int shmidPageTable = 0;

// Page table
PageTableEntry* pageTable = NULL;

// shmid of page frame list
int shmidPageFrame = 0;

// page frame list
PageFrame* pageFrames = NULL;

// id of shared semaphore
int semidCritical = 0;

// Shared process name
char* processName = NULL;

// Bit vector to store values
int* pageFrameOccupied = NULL;

// Bit vector to see occupied PCB
int* pcbOccupied = NULL;

// Id to message queue to receive requests from
int msgIdRequest = 0;

// Id to message queue to send grants to
int msgIdGrant = 0;

// Maximum number of processes
int maxNumProcesses = 0;

// Current number of children spawned
int currentNumProcesses = 0;

// Current number of blocked processes
int blockedNumProcesses = 0;

// Total children to end
int totalCompletedProcesses = 0;

// Current Page Frame pointer
int currentPageFramePointer = -1;

// blocked queue of processes waiting on pages
pid_t* blockedQueue = NULL;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

void allocateSharedIPC();
void deallocateSharedIPC();
void executeOSS();
void handleInterruption(int);
void initializeBitVectors();
void deallocateBitVectors();
void checkCommandArgs(int, char * *);
int spawnProcess(int, int, struct sembuf*, struct sembuf*);
int checkIfTime(int, int);
int findProcessInPcb(pid_t);
void incrementClock(int);

int main(int argc, char** argv)
{
	processName = argv[0];

	checkCommandArgs(argc, argv);

	signal(SIGALRM, handleInterruption);
	signal(SIGINT, handleInterruption);

	setPeriodic(2);
	allocateSharedIPC();
	initializeBitVectors();

	executeOSS();

	deallocateBitVectors();
	deallocateSharedIPC();

	return 0;
}

void executeOSS()
{
	*seconds = 0;
	*nanoSeconds = 0;

	// sem buff objects used to control critical sections
	struct sembuf semsignal[1];
	semsignal[0].sem_num = 0;
	semsignal[0].sem_op = 1;
	semsignal[0].sem_flg = 0;

	struct sembuf semwait[1];
	semwait[0].sem_num = 0;
	semwait[0].sem_op = -1;
	semwait[0].sem_flg = 0;

	int spawnSeconds = 0, spawnNanoSeconds = 0;
	int hitMaxTime = 0;

	mymsg_t requestMsg, grantMsg;
	blockedQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);

	while ( totalCompletedProcesses <= MAX_COMPLTETED_PROCESSES && hitMaxTime != 1 )
	{
		int spawnedProcess = spawnProcess(spawnSeconds, spawnNanoSeconds, semsignal, semwait);

		if ( spawnedProcess == 1 )
		{
			printf("test spawned: %d\n", spawnedProcess);
			// for now, don't spawn another process
			spawnSeconds = 100;
			spawnNanoSeconds = 100;
		}


		while ( msgrcv( msgIdRequest, &requestMsg, sizeof(requestMsg), 0, IPC_NOWAIT) > 0 )
		{
			int index = findProcessInPcb(requestMsg.mtype);
			int requestedAddress = atoi(requestMsg.mtext);
			int requestedPage = requestedAddress / BYTE_PER_KILO;

			printf("index: %d requestedAddress: %d requestedPage: %d\n", index, requestedAddress, requestedPage);

			grantMsg.mtype = requestMsg.mtype;

			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			// First check if the memory address requested is outside of accessible memory
			if ( (requestedPage + 1) > pcb[index].NumberOfPages )
				snprintf(grantMsg.mtext, 50, "%d", -1);
			else
			{
				// first we need to get the index of the page we are looking at in the global page table for process
				int pageTableIndex = (index * MAX_PAGES) + requestedPage;

				// check to see if page is in memory
				if ( pageTable[pageTableIndex].InMemory == 1 )
				{
					// send message back to child and increment clock slightly
					snprintf(grantMsg.mtext, 50, "%d", 1);
					incrementClock(10);
				}
				else
				{
					// not in memory, so queue process to wait for the page fault to end
					pcb[index].BlockedOnPage = requestedPage;

					int blockedSeconds = 0, blockedNanoSeconds = 0;
					blockedNanoSeconds = *nanoSeconds + 15000000;
					if ( blockedNanoSeconds >= NANO_PER_SECOND )
					{
						blockedSeconds = *seconds + 1;
						blockedNanoSeconds -= NANO_PER_SECOND;
					}

					pcb[index].BlockedUntilSeconds = blockedSeconds;
					pcb[index].BlockedUntilNanoSeconds = blockedNanoSeconds;

					enqueueValue(blockedQueue, requestMsg.mtype, MAX_PROCESSES);
					++blockedNumProcesses;
					snprintf(grantMsg.mtext, 50, "%d", 0);

					incrementClock(15000000);
				}
			}

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);

			if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
				writeError("Failed to send response to child\n", processName);
		}

		// Next we need to check the blocked queue
		checkQueue();

		if ( blockedNumProcesses >= maxNumProcesses)
		{
			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			incrementClock(15000000);

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);
		}

		int endedPid;
		int status;
		while ( (endedPid = waitpid(0, &status, WNOHANG)) > 0 )
		{
			printf("OSS sees child %d\n", endedPid);

			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			int pcbIndex = findProcessInPcb(endedPid);

			int i;
			for ( i = 0 ; i < MAX_PAGES; ++i )
			{
				pageTable[(pcbIndex * MAX_PAGES) + i].ProcessId = pageTable[(pcbIndex * MAX_PAGES) + i].PageNumber
						= pageTable[(pcbIndex * MAX_PAGES) + i].InMemory = 0;
			}

			for ( i = 0; i < NUM_PAGE_FRAMES; ++i)
			{
				if ( pageFrames[i].ProcessId == endedPid )
				{
					pageFrames[i].ProcessId = pageFrames[i].PageNumber = pageFrames[i].Modified = pageFrames[i].RecentlyAccessed = 0;
					clearBit(pageFrameOccupied, i);
				}
			}

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);

			++totalCompletedProcesses;
		}

		if ( *seconds  >= MAX_SYSTEM_SECONDS )
			hitMaxTime = 1;
	}

	if ( blockedQueue != NULL )
		free(blockedQueue);
}

void checkQueue()
{
	pid_t* tempQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);
	pid_t blockedPid = dequeueValue(blockedQueue, MAX_PROCESSES);

	initializeQueue(tempQueue, MAX_PROCESSES);

	while ( blockedPid != 0 )
	{
		int index = findProcessInPcb(blockedPid);
		if ( checkIfTime(pcb[index].BlockedUntilSeconds, pcb[index].BlockedUntilNanoSeconds) == 1 )
		{
			int pageFrameIndex = -1;

			int i;
			for ( i = 0; i < NUM_PAGE_FRAMES && pageFrameIndex == -1; ++i )
			{
				if (testBit(pageFrameOccupied, i ) == 0)
					pageFrameIndex = i;
			}


			// there was an unoccupied page frame, don't need to swap
			if ( pageFrameIndex != -1 )
			{
				pageFrames[pageFrameIndex].ProcessId = pcb[index].ProcessId;
				pageFrames[pageFrameIndex].PageNumber = pcb[index].BlockedOnPage;
				pageFrames[pageFrameIndex].RecentlyAccessed = 1;

				int pageIndexResource = (index * MAX_PAGES) + pcb[index].BlockedOnPage;
				pageTable[pageIndexResource].InMemory = 1;

				--blockedNumProcesses;
				pcb[index].BlockedUntilSeconds = 0;
				pcb[index].BlockedUntilNanoSeconds = 0;
				pcb[index].BlockedOnPage = 0;
			}
			else
			{
				// swap logic
			}
		}
		else
		{
			enqueueValue(tempQueue, blockedPid, MAX_PROCESSES);
		}

		blockedPid = dequeueValue(blockedQueue, MAX_PROCESSES);
	}

	blockedPid = dequeueValue(tempQueue, MAX_PROCESSES);
	while ( blockedPid != 0 )
	{
		enqueueValue(blockedQueue, blockedPid, MAX_PROCESSES);
		blockedPid = dequeueValue(tempQueue, MAX_PROCESSES);
	}

	free(tempQueue);
}

void incrementClock(int incrementNanoSeconds)
{
	*nanoSeconds += incrementNanoSeconds;

	if (*nanoSeconds >= NANO_PER_SECOND)
	{
		*seconds = *seconds + 1;
		*nanoSeconds -= NANO_PER_SECOND;
	}
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

int spawnProcess(int spawnSeconds, int spawnNanoSeconds, struct sembuf* semsignal, struct sembuf* semwait)
{
	int spawnedProcess = 0;

	if ( currentNumProcesses < maxNumProcesses )
	{
		if ( semop(semidCritical, semwait, 1) == -1 )
			writeError("Failed to wait for critical section entry\n", processName);

		if ( checkIfTime(spawnSeconds, spawnNanoSeconds) == 1 )
		{
			int index = findUnoccupiedPCB();

			pcb[index].CreatedAtSeconds = *seconds;
			pcb[index].CreatedAtNanoSeconds = *nanoSeconds;
			pcb[index].BlockedAtSeconds = 0;
			pcb[index].BlockedAtNanoSeconds = 0;
			pcb[index].BlockedNanoSeconds = 0;
			pcb[index].BlockedOnPage = 0;
			pcb[index].NumberOfPages = (rand() % MAX_PAGES) + 1; // need at least one page, at most MAX_PAGES
			pcb[index].ProcessId = createChildProcess("./user", processName);
			setBit(pcbOccupied, index);
			spawnedProcess = 1;
			++currentNumProcesses;
			printf("test index: %d process: %d\n", index, pcb[index].ProcessId);
		}

		if (semop(semidCritical, semsignal, 1) == -1)
			writeError("Failed to signal for critical section exit\n", processName);

	}

	return spawnedProcess;
}

int findUnoccupiedPCB()
{
	int i, index = -1;
	for ( i = 0; i < MAX_PROCESSES && index == -1; ++i )
	{
		if (testBit(pcbOccupied, i) == 0)
			index = i;
	}

	return index;
}

int checkIfTime(int spawnSeconds, int spawnNanoSeconds)
{
	int isTime = 0;

	if ( spawnSeconds < *seconds || (spawnSeconds <= *seconds && spawnNanoSeconds <= *nanoSeconds))
		isTime = 1;

	return isTime;
}

void handleInterruption(int signo)
{
	if ( signo == SIGINT || signo == SIGALRM )
	{
		deallocateSharedIPC();
		deallocateBitVectors();

		if ( blockedQueue != NULL)
			free(blockedQueue);

		kill(0, SIGKILL);
	}
}

void initializeBitVectors()
{
	int neededInts = (NUM_PAGE_FRAMES / sizeof(int));
	if (NUM_PAGE_FRAMES % sizeof(int) != 0)
		++neededInts;

	pageFrameOccupied = malloc(sizeof(int) * neededInts);
	int i;
	for ( i = 0; i < NUM_PAGE_FRAMES; ++i )
		clearBit(pageFrameOccupied, i);

	neededInts = (MAX_PROCESSES / sizeof(int));
	if ( MAX_PROCESSES % sizeof(int) != 0 )
		++neededInts;

	pcbOccupied = malloc(sizeof(int) * neededInts);
	for ( i = 0; i < MAX_PROCESSES; ++i )
		clearBit(pcbOccupied, i);
}

void deallocateBitVectors()
{
	if ( pageFrameOccupied != NULL )
		free(pageFrameOccupied);

	if ( pcbOccupied != NULL )
		free(pcbOccupied);
}

void allocateSharedIPC()
{
	if ( (shmidNanoSeconds = allocateSharedMemory(ID_SHARED_NANO_SECONDS, sizeof(int), processName)) > 0 )
	{
		if ((nanoSeconds = shmat(shmidNanoSeconds, 0, 0)) <= 0)
			writeError("Failed to attach to shared nanoseconds\n", processName);
	}
	else
		writeError("Failed to allocated shared nanoseconds memory\n", processName);

	if ( (shmidSeconds = allocateSharedMemory(ID_SHARED_SECONDS, sizeof(int), processName)) > 0 )
	{
		if ((seconds = shmat(shmidSeconds, 0, 0)) <= 0)
			writeError("Failed to attach to shared seconds\n", processName);
	}
	else
		writeError("Failed to allocated shared seconds memory\n", processName);

	if ( (shmidPCB = allocateSharedMemory(ID_SHARED_PCB, sizeof(ProcessControlBlock) * MAX_PROCESSES, processName)) > 0 )
	{
		if ( (pcb = shmat(shmidPCB, 0, 0)) <= 0 )
			writeError("Failed to attach to shared pcb\n", processName);
	}
	else
		writeError("Failed to allocated shared seconds memory\n", processName);

	if ( (shmidPageTable = allocateSharedMemory(ID_SHARED_PAGE, sizeof(PageTableEntry) * MAX_PAGES * MAX_PROCESSES, processName)) > 0 )
	{
		if ((pageTable = shmat(shmidPageTable, 0, 0)) <= 0)
			writeError("Failed to attach to shared page table\n", processName);
	}

	if ( (shmidPageFrame = allocateSharedMemory(ID_SHARED_FRAME, sizeof(PageFrame) * NUM_PAGE_FRAMES, processName)) > 0 )
	{
		if ( (pageFrames = shmat(shmidPageFrame, 0, 0)) <= 0 )
			writeError("Failed to attach to shared page frames\n", processName);
	}

	msgIdRequest = allocateMessageQueue(ID_MSG_REQUEST, processName);
	msgIdGrant = allocateMessageQueue(ID_MSG_GRANT, processName);
	semidCritical = allocateSemaphore(ID_SEM_CRITICAL, 1, processName);
	initializeSemaphoreToValue(semidCritical, 0, 1, processName);
}

void deallocateSharedIPC()
{
	if ( nanoSeconds != NULL && shmdt(nanoSeconds) == -1 )
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if ( shmidNanoSeconds > 0 )
		deallocateSharedMemory(shmidNanoSeconds, processName);

	if ( seconds != NULL && shmdt(seconds) == -1 )
		writeError("Failed to detach from nanoseconds memory\n", processName);

	if ( shmidSeconds > 0 )
		deallocateSharedMemory(shmidSeconds, processName);

	if ( pcb != NULL && shmdt(pcb) == -1 )
		writeError("Failed to detach from pcb", processName);

	if ( shmidPCB > 0 )
		deallocateSharedMemory(shmidPCB, processName);

	if ( pageTable != NULL && shmdt(pageTable) == -1 )
		writeError("Failed to detach from page table", processName);

	if ( shmidPageTable > 0 )
		deallocateSharedMemory(shmidPageTable, processName);

	if ( pageFrames != NULL && shmdt(pageFrames) == -1 )
		writeError("Failed to detach from page frames", processName);

	if ( shmidPageFrame > 0 )
		deallocateSharedMemory(shmidPageFrame, processName);

	if ( semidCritical > 0 )
		deallocateSemaphore(semidCritical, processName);

	if ( msgIdRequest > 0 )
		deallocateMessageQueue(msgIdRequest, processName);

	if ( msgIdGrant > 0 )
		deallocateMessageQueue(msgIdGrant, processName);
}

void initializeSharedMemory()
{
	int i;
	for ( i = 0; i < MAX_PROCESSES; ++i )
		pcb[i].ProcessId = pcb[i].CreatedAtSeconds = pcb[i].CreatedAtNanoSeconds = pcb[i].BlockedAtSeconds =
				pcb[i].BlockedAtNanoSeconds = pcb[i].BlockedNanoSeconds = pcb[i].NumberOfPages = 0;

	for ( i = 0; i < MAX_PROCESSES * MAX_PAGES; ++i )
		pageTable[i].ProcessId = pageTable[i].PageNumber = pageTable[i].InMemory = 0;

	for ( i = 0; i < NUM_PAGE_FRAMES; ++i )
		pageFrames[i].ProcessId = pageFrames[i].Modified = pageFrames[i].RecentlyAccessed = 0;
}

// Check our arguments passed from the command line.  In this case, since we are only accepting the
// -h option from the command line, we only need to return 1 int which indicates if a the help
// argument was passed.
void checkCommandArgs(int argc, char** argv)
{
	maxNumProcesses = MAX_PROCESSES;

	int c;
	while ( (c = getopt(argc, argv, "hn:")) != -1 )
	{
		switch (c)
		{
			case 'h':
				printf("oss (second iteration):\nWhen ran (using the option ./oss), simulates the management of resource allocation of several children by a master process \nThe following options may be used:\n-h\tDisplay help\n-n\tNumber of processes to spawn at max (cannot be greater than 18)\n-v\tVerbose log setting\n");
				exit(0);
				break;
			case 'n':
				if (optarg != NULL && checkNumber(optarg))
				{
					maxNumProcesses = atoi(optarg);
					if (maxNumProcesses > MAX_PROCESSES)
						printf("Argument exceeded max number of child processes, using default of %d\n", MAX_PROCESSES);
				}
				else
					printf("Invalid number of max children, will use default of %d\n", MAX_PROCESSES);
				break;
			default:
				break;
		}
	}
}
