
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int *bitmap;

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int fs_format()
{

	// create a new file system
	union fs_block block;

	// return failure on attempt to format an already-mounted disk
	if (bitmap != NULL) {
		printf("simplefs: Error! Cannot format an already-mounted disk.\n");
		return 0;
	}

	/* Write the superblock */
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();

	// set aside ten percent of the blocks for inodes
	if (block.super.nblocks % 10 == 0) {
		block.super.ninodeblocks = block.super.nblocks/10;
	}
	// round up
	else {
		block.super.ninodeblocks = block.super.nblocks/10 + 1;
	}
	
	block.super.ninodes = INODES_PER_BLOCK * block.super.ninodeblocks;

	disk_write(0, block.data);

	// TODO: destroy any data already present
	// TODO: clear the inode table
	// TODO: return 1 on success, 0 otherwise
	
	return 1;
}

void fs_debug()
{
	/* Scan a mounted filesystem */
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");

	if (block.super.magic == FS_MAGIC) {
		printf("magic number is valid\n");
	}
	else {
		printf("simplefs: Error! Magic number is invalid.\n");
		return;
	}

	printf("    %d blocks on disk\n",block.super.nblocks);
	printf("    %d blocks for inodes\n",block.super.ninodeblocks);
	printf("    %d inodes total\n",block.super.ninodes);

	/* Report on how the inodes are organized */

}

int fs_mount()
{
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
