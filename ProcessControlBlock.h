// ProcessControlBlock.h
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Declaration of ProcessControlBlock struct which is used to store information about
// child processes of oss.  Declared so that it can be included in both user and oss
// easily
#ifndef PROCESS_CONTROL_BLOCK_H
#define PROCESS_CONTROL_BLOCK_H
#define NUM_DIFF_RESOURCES 20

#include<sys/types.h>

typedef struct {
	// Id of process - will actually be the hoare server's unix pid
	pid_t ProcessId;
	
	// Seconds process was created at
	int CreatedAtSeconds;

	// NanoSeconds process was created at
	int CreatedAtNanoSeconds;

	// Blocked at seconds
	int BlockedAtSeconds;

	// Blocked at nanoseconds
	int BlockedAtNanoSeconds;

	// Total blocked nanoseconds
	long BlockedNanoSeconds;

	// Page that needs to be loaded
	int BlockedOnPage;

	int NumberOfPages;
} ProcessControlBlock;

#endif
