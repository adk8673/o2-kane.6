// ErrorLogging.c
// CS 4760 Project 6
// Alex Kane 4/26/2018
// Error logging function(s)

#include"ErrorLogging.h"

// Using perror, write information about errors to stderr
void writeError(const char* errorMessage, const char* processName)
{
	char message[1024];
	
	snprintf(message, sizeof(message), "%s: Error: %s", processName, errorMessage);
	
	perror(message);
}
