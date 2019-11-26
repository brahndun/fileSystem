#include <stdin.h>
#include "filesystem.h"
#include "softwaredisk.h"

void fs_print_error(void) {
    switch (fserror): {
        case FS_NONE:
            printf("ERROR: No file");
            break;
        case FS_OUT_OF_SPACE:
            printf("ERROR: Out of space");
            break;
        case FS_FILE_NOT_OPEN:
            printf("ERROR: File not open");
            break;
        case FS_FILE_OPEN:
            printf("ERROR: File is already open");
            break;
        case FS_FILE_NOT_FOUND:
            printf("ERROR: File not found");
            break;
        case FS_FILE_READ_ONLY:
            printf("ERROR: File is set to read only");
            break;
        case FS_FILE_ALREADY_EXISTS:
            printf("ERROR: File already exists");
            break;
        case FS_EXCEEDS_MAX_FILE_SIZE:
            printf("ERROR: File exceeds max size");
            break;
        case FS_ILLEGAL_FILENAME:
            printf("ERROR: Illegal filename");
            break;
        case FS_IO_ERROR:
            printf("ERROR: Error doing IO");
            break;
    }
}