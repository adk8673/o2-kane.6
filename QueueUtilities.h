// QueueUtilities.h
// CS 4760 Project 6
// Alex Kane 4/26/2018
// Declaration of queue utility functions
#ifndef QUEUE_UTILITIES_H
#define QUEUE_UTILITIES_H

void initializeQueue( pid_t *, int );
pid_t dequeueValue( pid_t *, int );
void enqueueValue( pid_t *, pid_t , int );

#endif
