// ProcessUtilities.h
// CS 4760 Project 6
// Alex Kane 4/26/2018
// Declarations of process utility functions
#ifndef PROCESS_UTILITIES_H
#define PROCESS_UTILITIES_H

pid_t createChildProcess( const char* , const char* );
int makeargv( const char * , const char *delimiters, char ***);

#endif
