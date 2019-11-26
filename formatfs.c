//initializes the filesystem for the assignment.
//requires a completely zeroed out software disk

#include <stdio.h>
#include "softwaredisk.h"

int main(int argc, char *argv[]){
    printf("Initializing filesystem...");
    init_software_disk();
    printf("done.\n");

    return 0;

}
