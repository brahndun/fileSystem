#include <stdio.h>
#include "filesystem.h"
#include "softwaredisk.h"

#define MAX_NAME_SIZE = 128;
#define NUM_DIRECTLY_MAPPED_INODE_BLOCKS = 9; //How many data blocks each inode maps to
#define NUM_INDIRECT_BLOCK_MAPPINGS = 206 //512 divided by 2

#define INODE_BITMAP_INDEX = 0;
#define DATA_BITMAP_INDEX = 1;

#define FIRST_INODE_BLOCK_INDEX= 2;
#define LAST_INODE_BLOCK_INDEX = 43;//An Inode takes up 24 bytes of space, so each Inode block holds 24 Inodes.
                                     //And since we want to support up to 1008 files, we need 1008/24=42 Inode blocks
#define FIRST_DIRECTORY_ITEM_BLOCK_INDEX = 44; //We want to support up to 1008 files, so we need 1008
                                                //directory item blocks
#define LAST_DIR_ENTRY_BLOCK_INDEX = 1051;

#define FIRST_DATA_BLOCK_INDEX = 1052;
#define LAST_DATA_BLOCK_INDEX = 4999;

typedef struct FileInternals {
    DirectoryItem directory;
    unsigned short int directoryItemBlockIndex;
    Inode inode;
    unsigned long int pos = 0;
    FileMode fileMode;
} FileInternals;

typedef struct DirectoryItem {
    unsigned short int inodeIndex
    char name[MAX_NAME_SIZE]
} DirectoryItem;

typedef struct Inode {
    unsigned long int fileSize; //Size of the file this Inode maps to in bytes
    unsigned short int blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS + 1] //The Inode has a number of
                                        //directly mapped data blocks defined by the constant + 1 for the
                                        //indirect block.
} Inode;

typedef struct InodeBlock {
    Inode inodes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)];
} InodeBlock;

typedef struct IndirectBlock {
    unsigned short int blocks[NUM_INDIRECT_BLOCK_MAPPINGS]
} IndirectBlock



File create_file(char *name, FileMode mode) {
    File f = malloc(sizeof(FileInternals));
    fserror=FS_NONE;
    bzero(f, sizeof(FileInternals));

    //Should probably check if name is null at some point

    //directoryBlockIndex = findDirectory()


}

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