/* File log:
    20261004: Add file and basic 'trace' command
*/

#include "user/user.h"
#include "user/sh.c"


// argc[0] = trace
// argc[1] = bit_mask value
// argc[2:n] = other command
// example input: trace 32 ls
int
main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: trace command...");
        exit(1);
    }

    int trace_mask = atoi(argv[1]);   //
    exit(0);
}
