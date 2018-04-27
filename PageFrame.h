// PageFrame.h
// CS 4760 Project 6
// Alex Kane 4/19/2018
// Definition of page frame object
#ifndef PAGE_FRAME_H
#define PAGE_FRAME_H

#include<sys/types.h>

typedef struct {
	pid_t ProcessId;

	int PageNumber;

	int Modified;

	int RecentlyAccessed;
} PageFrame;

#endif
