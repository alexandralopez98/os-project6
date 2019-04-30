
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int *bitmap;
int sizeBitmap;

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024
#define BLOCK_SIZE 4096

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
	union fs_block superblock;

	// return failure on attempt to format an already-mounted disk
	if (bitmap != NULL) {
		printf("simplefs: Error! Cannot format an already-mounted disk.\n");
		return 0;
	}

	/* Write the superblock */
	superblock.super.magic = FS_MAGIC;
	superblock.super.nblocks = disk_size();

	// set aside ten percent of the blocks for inodes
	if (superblock.super.nblocks % 10 == 0) {
		superblock.super.ninodeblocks = superblock.super.nblocks/10;
	}
	// round up
	else {
		superblock.super.ninodeblocks = superblock.super.nblocks/10 + 1;
	}
	
	superblock.super.ninodes = INODES_PER_BLOCK * superblock.super.ninodeblocks;

	// write the superblock to disk
	disk_write(0, superblock.data);

	// destroy any data already present (skipping the superblock)
	union fs_block reset;
	memset(reset.data, 0, BLOCK_SIZE);
	
	int i;
	for (i = 1; i < superblock.super.ninodeblocks; i++) {
		disk_write(i, reset.data);
	}
	
	return 1;
}

void fs_debug()
{
	/* Scan a mounted filesystem */
	union fs_block block;

	disk_read(0, block.data);

	printf("superblock:\n");

	if (block.super.magic == FS_MAGIC) {
		printf("magic number is valid\n");
	}
	else {
		printf("simplefs: Error! Magic number is invalid.\n");
		return;
	}

	printf("\t%d blocks on disk\n", block.super.nblocks);
	printf("\t%d blocks for inodes\n", block.super.ninodeblocks);
	printf("\t%d inodes total\n", block.super.ninodes);

	/* Report on how the inodes are organized */

	union fs_block inodeblock;
	struct fs_inode inode;

	// starting at the second block
	// loop through every inode block
	int i;
	for (i = 1; i < block.super.ninodeblocks; i++) {
		disk_read(i, inodeblock.data);

		// loop through every inode in the block
		int j;
		for (j = 0; j < INODES_PER_BLOCK; j++) {
			inode = inodeblock.inode[j];

			// check if inode is valid
			if (inode.isvalid) {
				printf("inode %d:\n", j);
				printf("\tsize: %d bytes\n", inode.size);
		
				// go through all 5 direct pointers to data blocks
				printf("\tdirect blocks:");
				int k;
				for (k = 0; (k * BLOCK_SIZE < inode.size) && (k < 5); k++) {
					printf(" %d", inode.direct[k]);
				}
				printf("\n");

				// check for indirect block (inode size will be greater than the total size of 5 direct blocks)
				if (inode.size > 5 * BLOCK_SIZE) {
					printf("\tindirect block: %d\n", inode.indirect);

					// find the indirect data blocks
					union fs_block blockforindirects;
					disk_read(inode.indirect, blockforindirects.data);

					int indirectblocks;
					if (inode.size % BLOCK_SIZE == 0) {
						indirectblocks = inode.size/BLOCK_SIZE - 5; 
					}
					else {
						indirectblocks = inode.size/BLOCK_SIZE - 5 + 1;
					}

					printf("\tindirect data blocks:");

					int l;
					for (l = 0; l < indirectblocks; l++) {
						printf(" %d", blockforindirects.pointers[l]);
					}
					printf("\n");
				}
			}
		}
	}	
}

int fs_mount() 
{

	// read the superblock
	union fs_block block;
	disk_read(0, block.data); 

	// create array of integers in memory for our bitmap
	bitmap = calloc(block.super.nblocks, sizeof(int)); 
	// set bitmap size
	sizeBitmap = block.super.nblocks; 

	union fs_block inode_block;
	struct fs_inode inode;
	int i;

	// loop through the inode blocks
	for (i = 1; i < block.super.ninodeblocks; i++) { 
		disk_read(i, inode_block.data);
		int j;
		for (j = 0; j < INODES_PER_BLOCK; j++) { //loops through inodes in each inode block.
			inode = inode_block.inode[j];
			if (inode.isvalid) {
				bitmap[i] = 1;
				int k;
				for (k = 0; k * BLOCK_SIZE < inode.size && k < 5; k++) { //loops through all direct pointers in inode.
					bitmap[inode.direct[k]] = 1;
				}
				if (inode.size > 5 * BLOCK_SIZE) {
					bitmap[inode.indirect] = 1;

					union fs_block temp;
					disk_read(inode.indirect, temp.data);
					int q;
					int indirectblocks; //determines number of indirect blocks.
					if (inode.size % BLOCK_SIZE == 0) {
						indirectblocks = inode.size / BLOCK_SIZE - 5;
					}
					else {
						indirectblocks = inode.size / BLOCK_SIZE - 5 + 1;
					}
					for (q = 0; q < indirectblocks; q++) { //loops through indirect block
						bitmap[temp.pointers[q]] = 1;
					}
				}
			}

		}
	}


	return 1;
}

int getInodeNumber(int blockindex, int inodeindex) {
	int temp = ((blockindex - 1) * INODES_PER_BLOCK) + inodeindex;
	return temp;
}

int fs_create()
{
	// no mounted disk
	if (bitmap == NULL) {
		printf("simplefs: Error! No mounted disk.\n");
		return 0;
	}

	union fs_block block;
	disk_read(0, block.data);

	int i;
	// loop through all inode blocks
	for (i = 1; i < block.super.ninodeblocks + 1; i++) {
		// read in every inode block
		disk_read(i, block.data);

		struct fs_inode inode;
		int j;
		// loop through all inodes in our inode block
		for (j = 0; j < INODES_PER_BLOCK; j++) {
			if (j == 0 && i == 1) {
				j = 1; //deals with invalid inode 0
			}

			// read in inode at that index
			inode = block.inode[j];

			// check if inode is valid
			if (!inode.isvalid) {
				// inode is invalid; we will store our new inode here
				// set our new inode (reset direct and indirect pointers and set size to 0)
				inode.size = 0;
				memset(inode.direct, 0, sizeof(inode.direct));
				inode.indirect = 0;
				inode.isvalid = 1;
				
				// update bitmap
				bitmap[i] = 1;
				// set the inode at the index in the block to our new inode
				block.inode[j] = inode;
				// write updated inode block to disk
				disk_write(i, block.data);

				// on success, return the inode number
				int inodeNumber = getInodeNumber(i, j);
				return inodeNumber;
			}
		}
	}

	// return 0 on failure to create inode (all inode blocks are full)
	printf("simplefs: Error! Unable to create new inode.\n");
	return 0;
}

int getBlockNumber(int inumber) {
	int temp = (inumber + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK; 
	return temp;
}

int fs_delete(int inumber)
{
	// find the block index that we need
	int blockNumber = getBlockNumber(inumber);

	union fs_block block;
	disk_read(0, block.data);

	// ensure that the index is not beyond the bounds
	if (blockNumber > block.super.ninodeblocks)
	{
		printf("simplefs: Error! Block number is out of bounds.\n");
		return 0;
	}
	//read in the data from our inode block
	disk_read(blockNumber, block.data);

	struct fs_inode inode = block.inode[inumber % 128];
	if (inode.isvalid) {
		//zero out everything in the inode struct.
		inode.size = 0;
		memset(inode.direct, 0, sizeof(inode.direct));
		inode.indirect = 0;
		inode.isvalid = 0;
		block.inode[inumber % 128] = inode; //update block's inode.
		disk_write(blockNumber, block.data);
		return 1;
	}

	// return 0 if it's an invalid inode
	printf("simplefs: Error! Unable to delete inode.\n");
	return 0;
}

int fs_getsize(int inumber)
{
	// get inode block
	int blockNumber = getBlockNumber(inumber);

	// read in the super block
	union fs_block block;
	disk_read(0, block.data);

	// check if the block number is valid; return -1 on error
	if (blockNumber > block.super.ninodeblocks)
	{
		printf("simplefs: Error! Block number is out of bounds.\n");
		return -1;
	}

	// read in the inode block
	disk_read(blockNumber, block.data);

	// read in the inode
	struct fs_inode inode = block.inode[inumber % 128];

	// check if inode is valid; return size on success
	if (inode.isvalid)
	{
		return inode.size;
	}

	// inode is invalid; return -1 on error
	printf("simplefs: Error! Invalid inode number.\n");
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	// get inode block
	int blockNumber = getBlockNumber(inumber);

	// read in the super block
	union fs_block block;
	disk_read(0, block.data);

	// check if the block number is valid; return -1 on error
	if (blockNumber > block.super.ninodeblocks || blockNumber == 0)
	{
		printf("simplefs: Error! Block number is out of bounds.\n");
		return -1;
	}

	// read in the data from the inode block
	disk_read(blockNumber, block.data);

	// read in the inode we want
	struct fs_inode inode;
	inode = block.inode[inumber % 128];

	// return error if inode is invalid
	if (!inode.isvalid)
	{
		printf("simplefs: Error! Invalid inode.\n");
		return 0;
	}
	// return error if inode's size is 0
	else if (inode.size == 0)
	{
		printf("simplefs: Error! Inode's size is 0.\n");
		return 0;
	}
	// return error if offset is out of bounds
	else if (inode.size < offset)
	{
		printf("simplefs: Error! Offset is out of bounds.\n");
		return 0;
	}

	// find the direct block we want to read from
	int directindex = offset / BLOCK_SIZE;
	if (directindex > 5)
	{
		printf("simplefs: Error! Direct block index is out of bounds.\n");
		return 0;
	}

	// adjust length if the end of the inode is reached before that amount of bytes are read
	if (inode.size < length + offset)
	{
		length = inode.size - offset;
	}

	// iterate through the direct blocks and read in the data
	union fs_block directblock;
	int totalbytesread = 0;
	memset(data, 0, length);
	int tempbytesread = BLOCK_SIZE;
	while (directindex < 5 && totalbytesread < length)
	{
		disk_read(inode.direct[directindex], directblock.data);

		// adjust tempbytesread variable if we have reached the end of the inode
		if (tempbytesread + totalbytesread > length)
		{
			tempbytesread = length - totalbytesread;
		}

		// append read data to our data variable
		strncat(data, directblock.data, tempbytesread);
		directindex++;
		totalbytesread += tempbytesread;
	}

	// read from indirect block if we still have bytes left to be read
	if (totalbytesread < length)
	{

		// read in the indirect block
		union fs_block indirectblock;
		union fs_block tempblock;
		disk_read(inode.indirect, indirectblock.data);

		// iterate through the indirect data blocks
		int indirectblocks;
		if (inode.size % BLOCK_SIZE == 0)
		{
			indirectblocks = inode.size / BLOCK_SIZE - 5;
		}
		else
		{
			indirectblocks = inode.size / BLOCK_SIZE - 5 + 1;
		}
		int i;
		tempbytesread = BLOCK_SIZE;
		for (i = 0; (i < indirectblocks) && (totalbytesread < length); i++)
		{
			disk_read(indirectblock.pointers[i], tempblock.data);

			// adjust tempbytesread variable if we have reached the end of the inode
			if (tempbytesread + totalbytesread > length)
			{
				tempbytesread = length - totalbytesread;
			}

			// append read data to our data variable
			strncat(data, tempblock.data, tempbytesread);
			totalbytesread += tempbytesread;
		}
	}

	// return the total number of bytes read (could be smaller than the number requested)
	return totalbytesread;
}

int getNextBlock() {
	union fs_block block;
	disk_read(0, block.data); //read superblock

	int i;
	for (i = block.super.ninodeblocks + 1; i < sizeBitmap; i++) {
		if (bitmap[i] == 0) {
			memset(&bitmap[i], 0, sizeof(bitmap[0]));
			return i;
		}
	}

	printf("simplefs: Error! There is no more room for blocks.\n");
	return -1;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	union fs_block block;
	disk_read(0, block.data);
	if (inumber == 0 || inumber > block.super.ninodes) {
		printf("simplefs: Error! Invalid inumber.\n");
		return 0;
	}

	int bytes_written = 0;
	int blockNumber = getBlockNumber(inumber);

	// we need to read the block to be written to
	disk_read(blockNumber, block.data);

	// fetch the inode data
	struct fs_inode inode = block.inode[inumber % 128];
	if (!inode.isvalid) {
		printf("Error: invalid inode!\n");
	}
	else {
		// find block to begin writing to
		union fs_block temp;
		int chunkSize = BLOCK_SIZE;

		int overallIndex = offset / BLOCK_SIZE; // ensure we get a valid int index
		while (overallIndex < 5 && bytes_written < length) {
			if (inode.direct[overallIndex] == 0) {
				int index = getNextBlock();

				if (index == -1)
				{
					printf("simplefs: Error! Not enough space left to write to.\n");
					return -1;
				}
				inode.direct[overallIndex] = index;
				bitmap[overallIndex] = 1;
			}

			if (chunkSize + bytes_written > length) {
				chunkSize = length - bytes_written;
			}

			strncpy(temp.data, data, chunkSize);
			data += chunkSize;

			disk_write(inode.direct[overallIndex], temp.data);
			inode.size += chunkSize;
			bytes_written += chunkSize;
			overallIndex++;
		}

		// still have some data left to write, need indirect blocks
		if (bytes_written < length) {
			union fs_block indirect_block;
			if (inode.indirect == 0) {
				int index = getNextBlock();
				if (index == -1) {
					printf("simplefs: Error! There is no space left to write to.\n");
					return -1;
				}

				inode.indirect = index;
				bitmap[index] = 1;
			}

			disk_read(inode.indirect, indirect_block.data);

			int blockIndex;
			for (blockIndex = 0; bytes_written < length; blockIndex++) {
				if (indirect_block.pointers[blockIndex] == 0) {
					int index = getNextBlock();
					if (index == -1) {
						printf("simplefs: Error! Not enough space left to write to.\n");
						return -1;
					}
					inode.direct[overallIndex] = index;
				}
				
				if (chunkSize + bytes_written > length) {
					chunkSize = length - bytes_written;
				}

				strncpy(temp.data, data, chunkSize);
				data += chunkSize;

				disk_write(inode.direct[overallIndex], temp.data);
				inode.size += chunkSize;
				bytes_written += chunkSize;
			}
		}

		block.inode[inumber % 128] = inode;
		disk_write(blockNumber, block.data);
		return bytes_written;
	}

	return 0;
}
