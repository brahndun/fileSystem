#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include "filesystem.h"
#include "softwaredisk.h"

#define MAX_NAME_SIZE 128
#define NUM_DIRECTLY_MAPPED_INODE_BLOCKS 9 //How many data blocks each inode maps to
#define NUM_INDIRECT_BLOCK_MAPPINGS 256 //512 divided by 2
#define INODES_PER_INODE_BLOCK 24

#define INODE_BITMAP_INDEX 0
#define DATA_BITMAP_INDEX 1

#define FIRST_INODE_BLOCK_INDEX 2
#define LAST_INODE_BLOCK_INDEX 57//An Inode takes up 28 bytes of space, so each Inode block holds 18 Inodes.
                                     //And since we want to support up to 1008 files, we need 1008/18=56 Inode blocks
#define FIRST_DIRECTORY_ITEM_BLOCK_INDEX 58 //We want to support up to 1008 files, so we need 1008
                                                //directory item blocks
#define LAST_DIRECTORY_ITEM_BLOCK_INDEX 1108


#define MAX_FILE_BYTES SOFTWARE_DISK_BLOCK_SIZE * NUM_DIRECTLY_MAPPED_INODE_BLOCKS + NUM_INDIRECT_BLOCK_MAPPINGS * SOFTWARE_DISK_BLOCK_SIZE

#define FIRST_DATA_BLOCK_INDEX 1109
#define LAST_DATA_BLOCK_INDEX 4999

FSError fserror;

typedef struct DirectoryItem {
    unsigned short int inodeIndex;
    char name[MAX_NAME_SIZE];
    int allocated;
    int open;
} DirectoryItem;

typedef struct Inode {
    unsigned long int fileSize; //Size of the file this Inode maps to in bytes
    unsigned short int blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS + 1]; //The Inode has a number of
                                        //directly mapped data blocks defined by the constant + 1 for the
                                        //indirect block.
} Inode;

typedef struct FileInternals {
    DirectoryItem directory;
    unsigned short int directoryItemBlockIndex;
    Inode inode;
    unsigned long int position;
    FileMode fileMode;
} FileInternals;

typedef struct InodeBlock {
    Inode inodes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)];
} InodeBlock;

typedef struct IndirectBlock {
    unsigned short int blocks[NUM_INDIRECT_BLOCK_MAPPINGS];
} IndirectBlock;

typedef struct Bitmap {
    unsigned char bytes[SOFTWARE_DISK_BLOCK_SIZE];
} Bitmap;

//Sets an inode's status to either not-in-use or in-use. This is done by associating each bit
//within the bitmap with the index of each inode.
int setInodeStatus(unsigned short int inodeIndex, int status) {
    Bitmap bitmap;
    if (!read_sd_block(&bitmap, INODE_BITMAP_INDEX)) {
        return 0;
    }
    else {
        if (status)
            bitmap.bytes[inodeIndex / 8] |= (1 << (7 - (inodeIndex % 8)));
        else 
            bitmap.bytes[inodeIndex / 8] &= -(1 << (7 - (inodeIndex % 8)));
        if (!write_sd_block(&bitmap, INODE_BITMAP_INDEX)) {
            return 0;
        }
    }

    return 1;
}

//Sets a file data block to either not-in-use or in-use. This is done by associating each
//bit within the bitmap with the index of each data block.
int setDataBlockStatus(unsigned short int blockIndex, int status) {
    Bitmap bitmap;
    if (!read_sd_block(&bitmap, DATA_BITMAP_INDEX)) {
        return 0;
    }
    else {
        if (status)
            bitmap.bytes[blockIndex / 8] |= (1 << (7 - (blockIndex % 8)));
        else 
            bitmap.bytes[blockIndex / 8] &= -(1 << (7 - (blockIndex % 8)));
        if (!write_sd_block(&bitmap, DATA_BITMAP_INDEX)) {
            return 0;
        }
    }

    return 1;
}

//Writes an inode to the specified inodeIndex.
int writeInode(unsigned short int inodeIndex, Inode inode) {
    InodeBlock inodeBlock;

    unsigned short int inodeBlockIndex = inodeIndex / INODES_PER_INODE_BLOCK + FIRST_INODE_BLOCK_INDEX;
    if (inodeBlockIndex <= LAST_INODE_BLOCK_INDEX) {
        if (!read_sd_block(&inodeBlock, inodeBlockIndex)) {
            return 0;
        }
        else {
            inodeBlock.inodes[inodeIndex % INODES_PER_INODE_BLOCK] = inode;
            if (!write_sd_block(&inodeBlock, inodeBlockIndex)) {
                fserror = FS_IO_ERROR;
                return 0;
            }
        }
    }
    return 1;
}

//Finds the index of the irst available data block by checking each bit of the data block bitmap, looking
//for the first 0.
int findFreeDataBlockIndex(void) {
    Bitmap bitmap;

    if (!read_sd_block(&bitmap, DATA_BITMAP_INDEX)) {
        fserror=FS_IO_ERROR;
        return -1;
    }
    else {
        int byte, bit;
        for (byte = 0; byte < SOFTWARE_DISK_BLOCK_SIZE; byte++) {
            for (bit = 0; bit < 8; bit++) {
                if ((bitmap.bytes[byte] & (1 << (7 - bit))) == 0) {
                    return byte*8 + bit;
                }
            }
        }
    }

    return -1;
}


//Sets one of the inode's blocks to the given block index.
int setInodeDataBlock(unsigned short int blockIndex, unsigned short int overwriteBlockIndex, File file) {
    int newIndirectBlockIndex;
    IndirectBlock * indirectBlock;

    if (blockIndex < NUM_DIRECTLY_MAPPED_INODE_BLOCKS)
        file->inode.blocks[blockIndex] = overwriteBlockIndex;
    else {
        //Creates an indirect block if one doesn't exist within the iNode yet
        if (!file->inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS]) {
            newIndirectBlockIndex = findFreeDataBlockIndex();
            indirectBlock = (IndirectBlock*)malloc(sizeof(IndirectBlock));
            bzero(indirectBlock, sizeof(IndirectBlock));

            if (!setDataBlockStatus(newIndirectBlockIndex, 1)) {
                return 0;
            }

            file->inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS] = newIndirectBlockIndex;
        }

        else {
            if (!read_sd_block(indirectBlock, file->inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS]))
                return 0;
        }
        if (!setDataBlockStatus(overwriteBlockIndex, 1))
            return 0;

        indirectBlock->blocks[blockIndex - NUM_DIRECTLY_MAPPED_INODE_BLOCKS] = overwriteBlockIndex;
        if (!write_sd_block(indirectBlock, file->inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS]))
            return 0;

        if (!writeInode(file->directory.inodeIndex, file->inode))
            return 0;
    }

    return 1;
}



//Finds the index of the first available inode by checking each bit of the inode bitmap, looking for the first 0.
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
                if ((bitmap.bytes[byte] & (1 << (7 - bit))) == 0) {
                    return byte*8 + bit;
                }
            }
        }
    }

    return -1;
}

//Simply writes the given directory to the given directory block index.
int writeDirectoryItem(DirectoryItem directory, unsigned short int blockIndex) {
    if (write_sd_block(&directory, blockIndex))
        return 1;
    else
        return 0;
}

//Creates a new directory item by searching for the first empty directory block and then writing to it.
int createDirectoryItem(DirectoryItem directory) {
    DirectoryItem * currentDirectory = (DirectoryItem*) malloc(sizeof(DirectoryItem));
    for (unsigned short int i = FIRST_DIRECTORY_ITEM_BLOCK_INDEX; i <= LAST_DIRECTORY_ITEM_BLOCK_INDEX; i++) {

        if (!read_sd_block(currentDirectory, i)) {
            fserror = FS_IO_ERROR;
            break;
        }
        
        else if (currentDirectory->allocated == 0) {
            writeDirectoryItem(directory, i);
            return i;
        }

        
    }

    return -1;
}

//Finds a directory given a directory name, and then reads the directory into the given directory pointer
int findDirectoryItem(DirectoryItem * directory, char* name) {
    for (int i=FIRST_DIRECTORY_ITEM_BLOCK_INDEX; i <= LAST_DIRECTORY_ITEM_BLOCK_INDEX; i++) {
        if (!read_sd_block(directory,i))
            fserror=FS_IO_ERROR;
        else {
            if (!strncmp(name, directory->name,MAX_NAME_SIZE - 1  ))
                return i;
        }
    }

    return -1;
}


int findInodeDataBlockIndex(unsigned short int blockIndex, Inode inode) {
    IndirectBlock indirectBlock;
    if (blockIndex < NUM_DIRECTLY_MAPPED_INODE_BLOCKS) {
        return inode.blocks[blockIndex];
    }
    else if (!inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS <= NUM_INDIRECT_BLOCK_MAPPINGS]) {
        return 0;
    }
    else if (blockIndex - NUM_DIRECTLY_MAPPED_INODE_BLOCKS <= NUM_INDIRECT_BLOCK_MAPPINGS) {
        if (!read_sd_block(&indirectBlock, inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS]))
            return -1;
        else
            return indirectBlock.blocks[blockIndex - NUM_DIRECTLY_MAPPED_INODE_BLOCKS];
    }
    else {
        return -1;
    }
}

//Writes data from buffer to the correct data block index this file's inode points to
int writeDataBlockFromFile(File file, void * data, unsigned short int blockIndex) {
   int dataBlockIndex = findInodeDataBlockIndex(blockIndex, file->inode);

   if (dataBlockIndex == 0) {
       dataBlockIndex = findFreeDataBlockIndex();
       if (dataBlockIndex < 0) {
           fserror = FS_OUT_OF_SPACE;
           return 0;
       }
       else {
           if (!setDataBlockStatus(dataBlockIndex,1)) {
               fserror = FS_IO_ERROR;
               return 0;
           }
            if (!setInodeDataBlock(blockIndex, dataBlockIndex, file))
                return 0;
       }
   }

   if (!write_sd_block(data, dataBlockIndex)) {
       fserror = FS_IO_ERROR;
       return 0;
   }
   fserror = FS_NONE;
   return 1;
}

//Reads a data block into the data buffer with an index corresponding to this file's inode data blocks
int readDataBlockFromFile(File file, void *data, unsigned short int blockIndex) {
    //NOTE: The blockIndex refers to the index of the data block for the file, not the software disk
    int dataBlockIndex;
    IndirectBlock indirectBlock;
    if (blockIndex < NUM_DIRECTLY_MAPPED_INODE_BLOCKS)
        dataBlockIndex = file->inode.blocks[blockIndex];
    else {
        if (!read_sd_block(&indirectBlock, file->inode.blocks[NUM_DIRECTLY_MAPPED_INODE_BLOCKS]))
            dataBlockIndex = -1;
        else
            dataBlockIndex = indirectBlock.blocks[blockIndex - NUM_DIRECTLY_MAPPED_INODE_BLOCKS];
    }
    if (dataBlockIndex != -1) {
        if (!read_sd_block(data, SOFTWARE_DISK_BLOCK_SIZE)) {
            fserror = FS_IO_ERROR;
            return 0;
        }
        return 1;
    }
    return 0;

}

File create_file(char *name) {
    File file = (File) malloc(sizeof(FileInternals));
    fserror=FS_NONE;
    bzero(file, sizeof(FileInternals));
    file->fileMode = READ_WRITE;

    file->position=0;

    file->directory.inodeIndex = findFreeInodeIndex();
    file->directory.allocated = 1;
    if (file->directory.inodeIndex < 0)
        fserror=FS_OUT_OF_SPACE;
    else {
        bzero(&file->inode, sizeof(Inode));
        if (!writeInode(file->directory.inodeIndex, file->inode)) {
            fserror=FS_IO_ERROR;
        }
        
        else {
            file->directory.open = 1;
            strncpy(file->directory.name, name, MAX_NAME_SIZE);
            int index = createDirectoryItem(file->directory);
            if (index < 0) {
                fserror=FS_OUT_OF_SPACE;
            }
            else {
                file->directoryItemBlockIndex = index;
                if (!setInodeStatus(file->directory.inodeIndex, 1)) {
                    fserror=FS_IO_ERROR;
                }
                else
                    return file;
            }
        }
    }

    return 0;
}

unsigned long write_file(File file, void *buf, unsigned long numbytes) {
    fserror=FS_NONE;
    unsigned char bytes[SOFTWARE_DISK_BLOCK_SIZE];
    int bytesWritten = 0;
    if (!file) {
        fserror = FS_FILE_NOT_OPEN;
    }
    else if (!file->directory.open) {
        fserror=FS_FILE_NOT_OPEN;
    }
    else if (file->position + numbytes > MAX_FILE_BYTES) {
        fserror = FS_OUT_OF_SPACE;
    }
    else if (file->fileMode == READ_ONLY) {
        fserror = FS_FILE_READ_ONLY;
    }
    else {
        while (numbytes > 0) {
            if (!readDataBlockFromFile(file, bytes, file->position / SOFTWARE_DISK_BLOCK_SIZE)) {
                fserror =  FS_IO_ERROR;
                break;
            }

            int offset = file->position % SOFTWARE_DISK_BLOCK_SIZE;
            int bytesToCopy;
            if (SOFTWARE_DISK_BLOCK_SIZE - offset > numbytes)
                bytesToCopy = numbytes;
            else
                bytesToCopy = SOFTWARE_DISK_BLOCK_SIZE - offset;
            memcpy(bytes + offset, buf + bytesWritten, bytesToCopy);

            if (!writeDataBlockFromFile(file, bytes, file->position / SOFTWARE_DISK_BLOCK_SIZE)) {
                fserror = FS_OUT_OF_SPACE;
                break;
            }

            numbytes -= bytesToCopy;
            bytesWritten += bytesToCopy;
            file->position += bytesToCopy;
            file->inode.fileSize += bytesToCopy;
        }

    }

    return bytesWritten;
}

unsigned long read_file(File file, void *buf, unsigned long numbytes) {
    unsigned char bytes[SOFTWARE_DISK_BLOCK_SIZE];
    unsigned long bytesRead = 0;

    if (file->position + numbytes > file->inode.fileSize)
        numbytes = file->inode.fileSize - file->position;

    if (file->directory.open == 0)
        fserror=FS_FILE_NOT_OPEN;
    else {
        while (numbytes > 0) {
            if (!readDataBlockFromFile(file, bytes, file->position / SOFTWARE_DISK_BLOCK_SIZE))
                fserror=FS_OUT_OF_SPACE;
            else {
                int offset = file->position % SOFTWARE_DISK_BLOCK_SIZE;
                int bytesToCopy;
                if (SOFTWARE_DISK_BLOCK_SIZE - offset > numbytes)
                    bytesToCopy = numbytes;
                else
                    bytesToCopy = SOFTWARE_DISK_BLOCK_SIZE - offset;
                
                memcpy(buf+bytesRead, bytes+offset, bytesToCopy);
                numbytes -= bytesToCopy;
                file->position += bytesToCopy;
                bytesRead += bytesToCopy;
            }
        }
    }

    return 0;
}

int seek_file(File file, unsigned long bytepos) {
    if (!file) {
        fserror=FS_FILE_NOT_OPEN;
        return 0;
    }
    else {
        if (bytepos >= MAX_FILE_BYTES) {
            fserror=FS_EXCEEDS_MAX_FILE_SIZE;
            return 0;
        }
        else {
            file->position = bytepos;
            if (file->position > file->inode.fileSize) {
                file->inode.fileSize = bytepos;
                return writeInode(file->directory.inodeIndex, file->inode);
            }
        }
    }

    return 1;
}

unsigned long file_length(File file) {
    return file->inode.fileSize;
}

int delete_file(char *name) {

    DirectoryItem * directory = (DirectoryItem*) malloc(sizeof(DirectoryItem));
    int blockIndex;
    blockIndex = findDirectoryItem(directory, name);
    if (blockIndex < 0) {
        fserror = FS_FILE_NOT_FOUND;
        return 0;
    }
    else if (directory->open) {
        fserror = FS_FILE_OPEN;
        return 0;
    }
    else {
        bzero(directory,sizeof(DirectoryItem));
        if (!write_sd_block(directory, blockIndex))
        {
            fserror = FS_IO_ERROR;
            return 0;
        }
    }

    return 1;
    
}

File open_file(char *name, FileMode mode) {
    File file = (File) malloc(sizeof(FileInternals));

    bzero(file, sizeof(FileInternals));
    file->position = 0;
    fserror = FS_NONE;
    file->fileMode = mode;

    int directory = findDirectoryItem(&file->directory, name);
    if (directory < 0) {
        fserror=FS_FILE_NOT_FOUND;
        return 0;
    }

    else {
        file->directoryItemBlockIndex = directory;
        if (file->directory.open) {
            fserror = FS_FILE_OPEN;
            return 0;
        }
        else {
            file->directory.open = 1;
            if (!writeDirectoryItem(file->directory, file->directoryItemBlockIndex)) {
                fserror=FS_IO_ERROR;
                return 0;
            }
            else {
                InodeBlock inodeBlock;
                unsigned short int inodeBlockIndex = file->directory.inodeIndex / INODES_PER_INODE_BLOCK + FIRST_INODE_BLOCK_INDEX;
                bzero(&file->inode, sizeof(Inode));

                if (inodeBlockIndex > LAST_INODE_BLOCK_INDEX) {
                    fserror = FS_OUT_OF_SPACE;
                    return 0;
                }
                else {
                    if (read_sd_block(&inodeBlock, inodeBlockIndex)) {
                        file->inode=inodeBlock.inodes[file->directory.inodeIndex % INODES_PER_INODE_BLOCK];
                    }
                    else {
                        return 0;
                        fserror=FS_IO_ERROR;
                    }
                }
            }
        }
    }

    fserror = FS_NONE;
    return file;
}

void close_file(File file) {
    file->directory.open = 0;
    if (!write_sd_block(&file->directory, file->directoryItemBlockIndex))
        fserror=FS_IO_ERROR;
    
}

int file_exists(char * name) {
    DirectoryItem directory;
    return findDirectoryItem(&directory, name);
}

void fs_print_error(void) {
    switch (fserror) {
        case FS_NONE:
            printf("No error.\n");
            break;
        case FS_OUT_OF_SPACE:
            printf("ERROR: Out of space\n");
            break;
        case FS_FILE_NOT_OPEN:
            printf("ERROR: File not open\n");
            break;
        case FS_FILE_OPEN:
            printf("ERROR: File is already open\n");
            break;
        case FS_FILE_NOT_FOUND:
            printf("ERROR: File not found\n");
            break;
        case FS_FILE_READ_ONLY:
            printf("ERROR: File is set to read only\n");
            break;
        case FS_FILE_ALREADY_EXISTS:
            printf("ERROR: File already exists\n");
            break;
        case FS_EXCEEDS_MAX_FILE_SIZE:
            printf("ERROR: File exceeds max size\n");
            break;
        case FS_ILLEGAL_FILENAME:
            printf("ERROR: Illegal filename\n");
            break;
        case FS_IO_ERROR:
            printf("ERROR: Error doing IO\n");
            break;
        default:
            printf("ERROR: There was an error");
            break;
    }
}