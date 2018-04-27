// PageTableEntry.h
// CS 4760 Project 6
// Alex Kane 4/26/2018
// Definition of page table entry
#ifndef PAGE_TABLE_ENTRY_H
#define PAGE_TABLE_ENTRY_H

#include<sys/types.h>

typedef struct {
	pid_t ProcessId;

	int PageNumber;

	int InMemory;
} PageTableEntry;

#endif
