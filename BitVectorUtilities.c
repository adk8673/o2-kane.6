#include"BitVectorUtilities.h"

// These functions will help to interact with an array of integers which is accessed as a bit vector
// implemented using the logic described here: http://www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html

void setBit(int bits[], int bitToSet)
{
	int bytesInInt = sizeof(int);
	bits[bitToSet / bytesInInt] |= 1 << (bitToSet % bytesInInt);
}

void clearBit(int bits[], int bitToSet)
{
	int bytesInInt = sizeof(int);
    bits[bitToSet / bytesInInt] &= ~(1 << (bitToSet % bytesInInt));
}

int testBit(int bits[], int bitToSet)
{
	int bytesInInt = sizeof(int);
	return ( (bits[bitToSet / bytesInInt] & (1 << (bitToSet % bytesInInt) )) != 0 );
}
