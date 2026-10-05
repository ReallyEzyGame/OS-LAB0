/* File log:
    20261004: Add file and basic 'trace' command
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"



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

    int mask = atoi(argv[1]);
    // char* path = argv[0];       // command path( command name)
    // copy n - 1 command left from here
    
    // simple TRACE version 1.0
    // only trace for one command at a time
    trace(mask);
    exec(argv[2], &argv[2]);
    printf("exec %s failed", argv[2]);
    exit(0);
}
