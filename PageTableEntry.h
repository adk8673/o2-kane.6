#ifndef PAGE_TABLE_ENTRY_H
#define PAGE_TABLE_ENTRY_H

#include<sys/types.h>

typedef struct {
	pid_t ProcessId;

	int PageNumber;

	int InMemory;
} PageTableEntry;

#endif
