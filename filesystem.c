#include <stdio.h>
#include "filesystem.h"
#include "softwaredisk.h"

#define MAX_NAME_SIZE = 128;
#define NUM_DIRECTLY_MAPPED_INODE_BLOCKS = 9; //How many data blocks each inode maps to
#define NUM_INDIRECT_BLOCK_MAPPINGS = 206 //512 divided by 2
#define INODES_PER_INODE_BLOCK = 24

#define INODE_BITMAP_INDEX = 0;
#define DATA_BITMAP_INDEX = 1;

#define FIRST_INODE_BLOCK_INDEX= 2;
#define LAST_INODE_BLOCK_INDEX = 43;//An Inode takes up 24 bytes of space, so each Inode block holds 24 Inodes.
                                     //And since we want to support up to 1008 files, we need 1008/24=42 Inode blocks
#define FIRST_DIRECTORY_ITEM_BLOCK_INDEX = 44; //We want to support up to 1008 files, so we need 1008
                                                //directory item blocks
#define LAST_DIRECTORY_ITEM_BLOCK_INDEX = 1051;

#define FIRST_DATA_BLOCK_INDEX = 1052;
#define LAST_DATA_BLOCK_INDEX = 4999;

typedef struct FileInternals {
    DirectoryItem directory;
    unsigned short int directoryItemBlockIndex;
    Inode inode;
    unsigned long int position;
    FileMode fileMode;
} FileInternals;

typedef struct DirectoryItem {
    unsigned short int inodeIndex;
    char name[MAX_NAME_SIZE];
    int allocated;
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

typedef struct Bitmap {
    unsigned char bytes[SOFTWARE_DISK_BLOCK_SIZE];
}

int setInodeStatus(unsigned short int inodeIndex, int status) {
    Bitmap bitmap;
    if (!read_sd_block(&bitmap, INODE_BITMAP_INDEX)) {
        return 0;
    }
    else {
        if (status)
            bitmap.bytes[inodeIndex / 8] |= (1 << (7 - (inodeIndex % 8)));
        else 
            f.bytes[inodeIndex / 8] &= -(1 << (7 - (blk % 8)));
        if (!write_sd_block(&bitmap, INODE_BITMAP_INDEX)) {
            return 0;
        }
    }
}

int setDataBlockStatus(unsigned short int blockIndex, int status) {
    Bitmap bitmap;
    if (!read_sd_block(&bitmap, DATA_BITMAP_INDEX)) {
        return 0;
    }
    else {
        if (status)
            bitmap.bytes[blockIndex / 8] |= (1 << (7 - (blockIndex % 8)));
        else 
            f.bytes[blockIndex / 8] &= -(1 << (7 - (blk % 8)));
        if (!write_sd_block(&bitmap, DATA_BITMAP_INDEX)) {
            return 0;
        }
    }
}

int writeInode(unsigned short int inodeIndex, Inode inode) {
    InodeBlock inodeBlock;

    unsigned short int inodeBlockIndex = inodeIndex / INODES_PER_INODE_BLOCK + FIRST_INODE_BLOCK_INDEX;
    if (inodeBlockIndex <= LAST_INODE_BLOCK_INDEX) {
        if (!read_sd_block(&inodeBlock, inodeBlockIndex)) {
            return 0;
        }
        else {
            ib.inodes[inodeIndex % INODES_PER_INODE_BLOCK] = inode;
            if (!write_sd_block(&inodeBlock, inodeBlockIndex)) {
                fserror = FS_IO_ERROR;
                return 0;
            }
        }
    }
    return 1;
}

int readInode(unsigned short int inodeIndex, Inode* inode) {
    InodeBlock inodeBlock;
    unsigned short int inodeBlockIndex = inodeIndex / INODES_PER_INODE_BLOCK + FIRST_INODE_BLOCK_INDEX;
    bzero(inode, sizeof(Inode));

    if (inodeBlockIndex > LAST_INODE_BLOCK)
        return 0;
    else {
        if (read_sd_block(&inodeBlock, inodeBlockIndex)) {
            *inode=inodeBlock.inodes[inodeIndex % INODES_PER_INODE_BLOCK];
            return 1;
        }
            return 0;
    }
}

unsigned short int findFreeInodeIndex(void) {
    Bitmap bitmap;

    if (!read_sd_block(&bitmap, INODE_BITMAP_INDEX)) {
        fserror=FS_IO_ERROR;
        return -1;
    }
    else {
        int byte, bit;
        for (byte = 0; byte < SOFTWARE_DISK_BLOCK_SIZE; byte++) {
            for (bit = 0; bit < 8; bit++) {
                if ((bitmap[byte] & (1 << (7 - bit))) == 0) {
                    return byte*8 + bit;
                }
            }
        }
    }
}

unsigned short int findFreeDataBlockIndex(void) {
    Bitmap bitmap;

    if (!read_sd_block(&f, DATA_BITMAP_INDEX)) {
        fserror=FS_IO_ERROR;
        return -1;
    }
    else {
        int byte, bit;
        for (byte = 0; byte < SOFTWARE_DISK_BLOCK_SIZE; byte++) {
            for (bit = 0; bit < 8; bit++) {
                if ((bitmap[byte] & (1 << (7 - bit))) == 0) {
                    return byte*8 + bit;
                }
            }
        }
    }
}

int createDirectoryItem(DirectoryItem directory) {
    DirectoryItem currentDirectory;
    for (int i = FIRST_DIRECTORY_ITEM_BLOCK_INDEX; i <= LAST_DIRECTORY_ITEM_BLOCK_INDEX; i++) {
        if (!read_sd_block(&currentDirectory, i)) {
            fserror = FS_IO_ERROR;
            break;
        }
        if (currentDirectory.allocated == 0) {
            writeDirectoryItem(directory, i);
            return i;
        }

        
    }

    return -1;

}

int writeDirectoryItem(DirectoryItem directory, unsigned short int blockIndex) {
    if (write_sd_block(&directory, blockIndex))
        return 1;
    else
        return 0;
}

File create_file(char *name, FileMode mode) {
    File f = malloc(sizeof(FileInternals));
    fserror=FS_NONE;
    bzero(f, sizeof(FileInternals));

    f->mode = mode;
    f->position=0;

    f->d.inode_index = findFreeInodeIndex();
    f->d.alocated = 1;
    if (f->d.inode_index < 0)
        fserror=FS_OUT_OF_SPACE;
    else {
        bzero(f->d.inode, sizeof(Inode));
        if (!writeInode(f->d.inodeIndex, f->inode)) {
            fserror=FS_IO_ERROR
        }
        else {
            strncpy(f->d.name, name, MAX_NAME_SIZE);
            f->d.open = 1;
            createDirectoryEntry(f->directory);
        }
    }


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