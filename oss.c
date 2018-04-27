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

// number of total page faults
int pageFaults = 0;

// blocked queue of processes waiting on pages
pid_t* blockedQueue = NULL;

// log file
FILE* ossLog = NULL;

// total lines written
int totalLinesWritten = 0;

// pointer to currently selected frame for fifo
int currentPageFrameIndex = 0;

// total memory accesses
int numMemoryAccesses = 0;

// total seg faults
int totalSegFaults = 0;

// Total blocked times
long totalBlockedTime = 0;

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
void writeMemoryAllocation();
void printOutPCB();
void printOutStatistics();

int main(int argc, char** argv)
{
	processName = argv[0];

	checkCommandArgs(argc, argv);

	ossLog = fopen("oss.log", "w");

	if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
	{
		fprintf(ossLog, "Begin execution of oss\n");
		++totalLinesWritten;
	}

	// set signal handlers
	signal(SIGALRM, handleInterruption);
	signal(SIGINT, handleInterruption);

	setPeriodic(2);
	allocateSharedIPC();
	initializeBitVectors();

	executeOSS();

	deallocateBitVectors();
	deallocateSharedIPC();

	if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
	{
		fprintf(ossLog, "End execution of oss\n");
		fclose(ossLog);
	}

	printOutStatistics();

	return 0;
}

// main execution logic of oss
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

	// main loop until process are completed or time limit is hit
	while ( totalCompletedProcesses < MAX_COMPLTETED_PROCESSES && hitMaxTime != 1 )
	{
		// check to see if we need to spawn a new process
		int spawnedProcess = spawnProcess(spawnSeconds, spawnNanoSeconds, semsignal, semwait);

		if ( spawnedProcess == 1 )
		{
			// if we spawned a new process we need to figure out when to next spawn a process
			int nextProcessSpawn = rand() % 700;

			spawnNanoSeconds = *nanoSeconds + nextProcessSpawn;
			spawnSeconds = *seconds;

			if ( spawnNanoSeconds >= NANO_PER_SECOND )
			{
				spawnSeconds -= NANO_PER_SECOND;
				spawnSeconds += 1;
			}
		}

		// see if any child requested memory
		while ( msgrcv( msgIdRequest, &requestMsg, sizeof(requestMsg), 0, IPC_NOWAIT) > 0 )
		{
			// get information about process and page requested
			int index = findProcessInPcb(requestMsg.mtype);
			int requestedAddress = atoi(requestMsg.mtext);
			int requestedPage = requestedAddress / BYTE_PER_KILO;

			grantMsg.mtype = requestMsg.mtype;

			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			++numMemoryAccesses;
			pcb[index].NumberOfMemoyAccesses++;

			// First check if the memory address requested is outside of accessible memory
			if ( (requestedPage + 1) > pcb[index].NumberOfPages )
			{
				++totalSegFaults;
				if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
				{
					fprintf(ossLog, "Child process %d requested address %d which resulted in a seg fault at time %d:%d\n", requestMsg.mtype, requestedAddress, *seconds, *nanoSeconds);
					++totalLinesWritten;
				}

				snprintf(grantMsg.mtext, 50, "%d", -1);
			}
			else
			{
				// first we need to get the index of the page we are looking at in the global page table for process
				int pageTableIndex = (index * MAX_PAGES) + requestedPage;

				// check to see if page is in memory
				if ( pageTable[pageTableIndex].InMemory == 1 )
				{
					if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
					{
						fprintf(ossLog, "Child process %d requested address %d on page %d which was in memory at time %d:%d\n", requestMsg.mtype, requestedAddress, requestedPage, *seconds, *nanoSeconds);
						++totalLinesWritten;
					}

					// send message back to child and increment clock slightly
					snprintf(grantMsg.mtext, 50, "%d", 1);

					if ( pcb[index].IsWrite == 1 )
					{
						int i;
						for ( i = 0; i < NUM_PAGE_FRAMES; ++i )
						{
							if ( pageFrames[i].ProcessId == pcb[index].ProcessId && pageFrames[i].PageNumber == pageTable[pageTableIndex].PageNumber )
								pageFrames[i].Modified = 1;
						}
					}

					incrementClock(10);
					pcb[index].BlockedNanoSeconds += 10;
				}
				else
				{
					// page wasn't in memory so block until it is
					++pageFaults;

					// not in memory, so queue process to wait for the page fault to end
					pcb[index].BlockedOnPage = requestedPage;

					int blockedSeconds = 0, blockedNanoSeconds = 0;
					blockedNanoSeconds = *nanoSeconds + 1500;
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

					if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
					{
						fprintf(ossLog, "Child process %d requested address %d on page %d which which is not in memory at time %d:%d resulting in a page fault\n", requestMsg.mtype, requestedAddress, requestedPage, *seconds, *nanoSeconds);
						++totalLinesWritten;
					}
				}
			}

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);

			if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
				writeError("Failed to send response to child\n", processName);
		}

		// Next we need to check the blocked queue
		checkQueue();

		if ( blockedNumProcesses >= currentNumProcesses)
		{
			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
			{
				fprintf(ossLog, "All processes waiting on memory swaps at %d:%d, increment clock\n", *seconds, *nanoSeconds);
				++totalLinesWritten;
			}

			incrementClock(1500);

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);
		}

		// See if any children ended and need to be waited on
		int endedPid;
		int status;
		while ( (endedPid = waitpid(0, &status, WNOHANG)) > 0 )
		{
			printf("OSS waiting on child %d because it has ended\n", endedPid);

			// access shared data structures
			if ( semop(semidCritical, semwait, 1) == -1 )
					writeError("Failed to wait for critical section entry\n", processName);

			int pcbIndex = findProcessInPcb(endedPid);

			if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
			{
				fprintf(ossLog, "Child process %d ended at %d:%d\n", endedPid, *seconds, *nanoSeconds);
				++totalLinesWritten;
				fprintf(ossLog, "Child process %d had an effective memory access time of %ld nano seconds\n", endedPid, (double)pcb[pcbIndex].BlockedNanoSeconds / (double)pcb[pcbIndex].NumberOfMemoyAccesses);
			}

			totalBlockedTime += pcb[pcbIndex].BlockedNanoSeconds;

			writeMemoryAllocation();

			// get rid of any pages in page table
			int i;
			for ( i = 0 ; i < MAX_PAGES; ++i )
			{
				pageTable[(pcbIndex * MAX_PAGES) + i].ProcessId = pageTable[(pcbIndex * MAX_PAGES) + i].PageNumber
						= pageTable[(pcbIndex * MAX_PAGES) + i].InMemory = 0;
			}

			// clear out values currently in memory
			for ( i = 0; i < NUM_PAGE_FRAMES; ++i)
			{
				if ( pageFrames[i].ProcessId == endedPid )
				{
					pageFrames[i].ProcessId = pageFrames[i].PageNumber = pageFrames[i].Modified = pageFrames[i].RecentlyAccessed = 0;
					clearBit(pageFrameOccupied, i);
				}
			}

			// Clear out the process control block
			pcb[pcbIndex].ProcessId = pcb[pcbIndex].CreatedAtSeconds = pcb[pcbIndex].CreatedAtNanoSeconds = pcb[pcbIndex].BlockedUntilSeconds
					= pcb[pcbIndex].BlockedUntilNanoSeconds = pcb[pcbIndex].BlockedAtSeconds = pcb[pcbIndex].BlockedAtNanoSeconds
					= pcb[pcbIndex].BlockedNanoSeconds = pcb[pcbIndex].IsWrite = pcb[pcbIndex].NumberOfPages = pcb[pcbIndex].NumberOfMemoyAccesses = 0;

			pcb[pcbIndex].BlockedOnPage = -1;

			clearBit(pcbOccupied, pcbIndex);

			if ( semop(semidCritical, semsignal, 1) == -1 )
				writeError("Failed to signal for critical section exit\n", processName);

			++totalCompletedProcesses;
		}

		if ( *seconds  >= MAX_SYSTEM_SECONDS )
			hitMaxTime = 1;
	}


	printf("Spawned: %d nanoseconds: %d\n", spawnSeconds, spawnNanoSeconds);
	printf("Seconds: %d nanoseconds: %d\n", *seconds, *nanoSeconds);
	if ( blockedQueue != NULL )
		free(blockedQueue);
}

void writeMemoryAllocation()
{
	// print out the current allocation
	if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
	{
		fprintf(ossLog, "Current memory allocation\n");
		++totalLinesWritten;

		int i;
		for ( i = 0; i < NUM_PAGE_FRAMES; ++i )
		{
			if ( pageFrames[i].ProcessId == 0 )
				fprintf(ossLog, ".");
			else if ( pageFrames[i].Modified == 1)
				fprintf(ossLog, "D");
			else
				fprintf(ossLog, "U");
		}

		fprintf(ossLog, "\n");
		++totalLinesWritten;

		for ( i = 0; i < NUM_PAGE_FRAMES; ++i )
		{
			if ( pageFrames[i].ProcessId == 0 )
				fprintf(ossLog, ".");
			else if ( pageFrames[i].RecentlyAccessed == 1)
				fprintf(ossLog, "1");
			else
				fprintf(ossLog, "0");
		}

		fprintf(ossLog, "\n");
		++totalLinesWritten;
	}
}

void checkQueue()
{
	pid_t* tempQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);

	pid_t blockedPid = dequeueValue(blockedQueue, MAX_PROCESSES);

	initializeQueue(tempQueue, MAX_PROCESSES);

	// check all our blocked processes and see if it's time to swap the page into memory
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
				if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
				{
					fprintf(ossLog, "Page %d was swapped in for child %d %d:%d\n", pcb[index].BlockedOnPage, pcb[index].ProcessId, *seconds, *nanoSeconds);
					++totalLinesWritten;
				}

				pageFrames[pageFrameIndex].ProcessId = pcb[index].ProcessId;
				pageFrames[pageFrameIndex].PageNumber = pcb[index].BlockedOnPage;
				pageFrames[pageFrameIndex].RecentlyAccessed = 1;

				int pageIndexResource = (index * MAX_PAGES) + pcb[index].BlockedOnPage;
				pageTable[pageIndexResource].InMemory = 1;

				--blockedNumProcesses;
				pcb[index].BlockedUntilSeconds = 0;
				pcb[index].BlockedUntilNanoSeconds = 0;
				pcb[index].BlockedOnPage = -1;

				setBit(pageFrameOccupied, pageFrameIndex);

				incrementClock(1500);

			}
			else
			{
				// no space, we need to see which process's frame to swap out
				int swapped = 0;
				while ( swapped != 1 )
				{
					if ( pageFrames[currentPageFrameIndex].RecentlyAccessed != 1 )
					{
						// This page hasn't been recently accessed, swap it out
						int targetProcessIndex = findProcessInPcb(pageFrames[currentPageFrameIndex].ProcessId);
						int targetPageNumber = pageFrames[currentPageFrameIndex].PageNumber;

						int targetPageIndex = (targetProcessIndex * MAX_PAGES) + targetPageNumber;
						pageTable[targetPageIndex].InMemory = 0;

						// We increment the clock a set amount of time based on whether or not the dirty bit has been set
						if ( pageFrames[currentPageFrameIndex].Modified == 1)
						{
							if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
							{
								fprintf(ossLog, "OSS swapped out frame %d for child %d from child %d which had dirty bit set at %d:%d\n", currentPageFrameIndex, pcb[index].ProcessId, pcb[targetProcessIndex].ProcessId, *seconds, *nanoSeconds);
								++totalLinesWritten;
							}

							incrementClock(40000);
						}
						else
						{
							if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
							{
								fprintf(ossLog, "OSS swapped out frame %d for child %d from child %d %d:%d\n", currentPageFrameIndex, pcb[index].ProcessId, pcb[index].ProcessId, pcb[targetProcessIndex], *seconds, *nanoSeconds);
								++totalLinesWritten;
							}

							incrementClock(15000);
						}

						pageFrames[currentPageFrameIndex].ProcessId = pcb[index].ProcessId;
						pageFrames[currentPageFrameIndex].PageNumber = pcb[index].BlockedOnPage;
						pageFrames[currentPageFrameIndex].RecentlyAccessed = 1;

						if ( pcb[index].IsWrite == 1 )
							pageFrames[currentPageFrameIndex].Modified = 1;
						else
							pageFrames[currentPageFrameIndex].Modified = 0;

						pcb[index].BlockedOnPage = -1;
						pcb[index].BlockedUntilSeconds = 0;
						pcb[index].BlockedUntilNanoSeconds = 0;
						pcb[index].BlockedAtSeconds = 0;
						pcb[index].BlockedAtNanoSeconds = 0;

						swapped = 1;
					}
					else
					{
						// clear so if we hit again this will be removed
						pageFrames[currentPageFrameIndex].RecentlyAccessed = 0;
					}

					++currentPageFrameIndex;
					if ( currentPageFrameIndex >= NUM_PAGE_FRAMES )
						currentPageFrameIndex = 0;
				}
			}

			pcb[index].BlockedNanoSeconds += ((pcb[index].BlockedUntilSeconds * NANO_PER_SECOND) + (pcb[index].BlockedUntilNanoSeconds)) - ((pcb[index].BlockedAtSeconds * NANO_PER_SECOND) + pcb[index].BlockedAtNanoSeconds);
		}
		else
		{
			enqueueValue(tempQueue, blockedPid, MAX_PROCESSES);
		}

		blockedPid = dequeueValue(blockedQueue, MAX_PROCESSES);
	}

	// copy the values still blocked into the current block
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

// Function to a process's location in the pcb
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

	// if we have too many processes already don't spawn any more
	if ( currentNumProcesses < maxNumProcesses )
	{
		if ( semop(semidCritical, semwait, 1) == -1 )
			writeError("Failed to wait for critical section entry\n", processName);

		// see if it is time to spawn a new process
		if ( checkIfTime(spawnSeconds, spawnNanoSeconds) == 1 )
		{
			int index = findUnoccupiedPCB();

			pcb[index].CreatedAtSeconds = *seconds;
			pcb[index].CreatedAtNanoSeconds = *nanoSeconds;
			pcb[index].BlockedAtSeconds = 0;
			pcb[index].BlockedAtNanoSeconds = 0;
			pcb[index].BlockedNanoSeconds = 0;
			pcb[index].BlockedOnPage = -1;
			pcb[index].NumberOfMemoyAccesses = 0;
			pcb[index].NumberOfPages = (rand() % MAX_PAGES) + 1; // need at least one page, at most MAX_PAGES
			pcb[index].ProcessId = createChildProcess("./user", processName);
			setBit(pcbOccupied, index);
			spawnedProcess = 1;
			++currentNumProcesses;

			if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
			{
				fprintf(ossLog, "Created child %d at %d:%d with %d pages\n", pcb[index].ProcessId, *seconds, *nanoSeconds, pcb[index].NumberOfPages);
				++totalLinesWritten;
			}
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
		writeMemoryAllocation();
		printOutPCB();

		int i;
		for ( i = 0; i < MAX_PROCESSES; ++i)
		{
			if (testBit(pcbOccupied, i) == 1)
				totalBlockedTime += pcb[i].BlockedNanoSeconds;
		}

		printOutStatistics();

		if ( ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE )
		{
			fprintf(ossLog, "Ended oss due to signal at %d:%d\n", *seconds, *nanoSeconds);
			fclose(ossLog);
			++totalLinesWritten;
		}

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
				printf("oss (second iteration):\nWhen ran (using the option ./oss), simulates the management of resource allocation of several children by a master process \nThe following options may be used:\n-h\tDisplay help\n-n\tNumber of processes to spawn at max (cannot be greater than 18)\n");
				exit(0);
				break;
			case 'n':
				if (optarg != NULL && checkNumber(optarg))
				{
					maxNumProcesses = atoi(optarg);
					if (maxNumProcesses > MAX_PROCESSES)
					{
						printf("Argument exceeded max number of child processes, using default of %d\n", MAX_PROCESSES);
						maxNumProcesses = MAX_PROCESSES;
					}
				}
				else
					printf("Invalid number of max children, will use default of %d\n", MAX_PROCESSES);
				break;
			default:
				break;
		}
	}
}

void printOutStatistics()
{
	double totalNanoTime = ((double)(*seconds) * (double)NANO_PER_SECOND) + (double)(*nanoSeconds);

	double memoryAccessesPerSecond = (double)numMemoryAccesses / totalNanoTime;
	double pageFaultsPerAccess = (double)pageFaults / (double)numMemoryAccesses;
	double memoryAccessesPerSpeed = (double)numMemoryAccesses / (double)totalBlockedTime;
	double throughput = (double)totalCompletedProcesses / totalNanoTime;

	printf("Ending statistics\nMemory accesses per nanosecond: %lf\nNumber of page faults per memory access: %lf\n"
			"Average memory access speed: %lf\nNumber of seg faults: %lf\nThroughput (processes completed per nano seconds): %lf\n",
			memoryAccessesPerSecond,
			pageFaultsPerAccess,
			memoryAccessesPerSpeed,
			totalSegFaults,
			throughput);
}

// print ouf the contents of the pcb for debugging purposes
void printOutPCB()
{
	printf("PCB Contents: \n");
	int i;
	for ( i = 0; i < MAX_PROCESSES; ++i )
	{
		printf("Process Id: %d  Created at seconds: %d Created at NanoSeconds: %d Blocked until nano: %d blocked until seconds: %d blocked at seconds: %d blocked at nano: %d  blocked on page: %d is write: %d Page number: %d\n",
				pcb[i].ProcessId,
				pcb[i].CreatedAtSeconds,
				pcb[i].CreatedAtNanoSeconds,
				pcb[i].BlockedUntilNanoSeconds,
				pcb[i].BlockedUntilSeconds,
				pcb[i].BlockedAtSeconds,
				pcb[i].BlockedAtNanoSeconds,
				pcb[i].BlockedOnPage,
				pcb[i].IsWrite,
				pcb[i].NumberOfPages);
	}
}
