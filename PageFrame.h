#ifndef PAGE_FRAME_H
#define PAGE_FRAME_H

#include<sys/types.h>

typedef struct {
	pid_t ProcessId;

	int Modified;

	int RecentlyAccessed;
} PageFrame;

#endif
