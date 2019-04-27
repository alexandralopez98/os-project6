
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

	disk_write(0, superblock.data);

	//Destroy any data already present
	union fs_block reset;
	memset(reset.data, 0, 4096);

	int i;
	for (i = 0; i < superblock.super.ninodeblocks; i++) {
		disk_write(i, reset.data);
	}

	// TODO: clear the inode table
	// TODO: return 1 on success, 0 otherwise

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
				for (k = 0; (k * 4096 < inode.size) && (k < 5); k++) {
					printf(" %d", inode.direct[k]);
				}
				printf("\n");

				// check for indirect block (inode size will be greater than the total size of 5 direct blocks)
				if (inode.size > 5 * 4096) {
					printf("\tindirect block: %d\n", inode.indirect);

					// find the indirect data blocks
					union fs_block blockforindirects;
					disk_read(inode.indirect, blockforindirects.data);

					int indirectblocks;
					if (inode.size % 4096 == 0) {
						indirectblocks = inode.size/4096 - 5;
					}
					else {
						indirectblocks = inode.size/4096 - 5 + 1;
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
	return 0;
}

int fs_create()
{
	// no mounted disk
	if (bitmap == NULL) {
		return 0;
	}

	union fs_block block;
	disk_read(0, block.data);

	int i;
	for (i = 1; i < block.super.nblocks; i++) {
		// read and heck inode block for empty spaces
		disk_read(i, block.data);

		struct fs_inode inode;
		int j;
		for (j = 0; j < POINTERS_PER_BLOCK; j++) {
			if (j == 0 && i == 1) {
				j = 1;
			}

			inode = block.inode[j];

			if (!inode.isvalid) {

				// valid inode = safe to fill space
				inode.isvalid = 1;
				inode.size = 0;
				memset(inode.direct, 0, sizeof(inode.direct));
				inode.indirect = 0;

				bitmap[i] = 1;
				block.inode[j] = inode;
				disk_write(i, block.data);
				return j + (i - 1) * 128;
			}
		}
	}

	// failed to create inode because blocks are full
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
	union fs_block block;
	disk_read(0, block.data);
	if (inumber == 0 || inumber > block.super.ninodes) {
		printf("Error: Invalid inumber!\n");
		return 0;
	}

	int total_bytes_written = 0;
	int block_index = (inumber + 128 - 1) / 128;

	// we need to read the block to be written to
	disk_read(block_index, block.data);

	// fetch the inode data
	struct fs_inode inode = block.inode[inumber % 128];
	if (!inode.isvalid) {
		printf("Error: invalid inode!\n");
	} else {
		// find block to begin writing to
		union fs_block temp_block;
		int chunkSize = 4096;

		int overallIndex = floor(offset / 4096); // ensure we get a valid int index
		while (overallIndex <  5 && total_bytes_written < length) {
			if (inode.direct[overallIndex] == 0) {
				int index = get_new_datablock();

				if (index == -1) {
					printf("Not enough space left, cannot write\n");
					return -1;
				}
				inode.direct[overallIndex] = index;
				bitmap[overallIndex] = 1;
			}

			if (chunkSize + total_bytes_written > length) {
				chunkSize = length - total_bytes_written;
			}

			strncpy(temp_block.data, data, chunkSize);
			data += chunkSize;

			disk_write(inode.direct[overallIndex], temp_block.data);
			inode.size += chunkSize;
			total_bytes_written += chunkSize;

			i++;
		}

		// still have some data left to write, need indirect blocks
		if (total_bytes_written < length) {

			union fs_block indirect_block;

			if (inode.indirect == 0) {
				int newIndex = get_new_datablock();

				if (newIndex == -1) {
					printf("Error: No space left to write to!\n");
					return -1;
				}

				inode.indirect = newIndex;
				bitmap[newIndex] = 1;
			}

			disk_read(inode.indirect, indirect_block.data);

			int blockIndex = 0;
			for (blockIndex; total_bytes_written < length; blockIndex++) {

				if (indirect_block.pointers[blockIndex] == 0) {
					printf("Error: No space left to write to!\n");
					return -1;
				}
				inode.direct[overallIndex] = newIndex;
			}

			if (chunkSize + total_bytes_written > length) {
				chunkSize = length - total_bytes_written;
			}

			strcpy(temp_block.data, data, chunkSize);
			data += chunkSize;

			disk_write(inode.direct[overallIndex], temp_block.data);
			inode.size += chunkSize;

			total_bytes_written += chunkSize;
		}

		block.inode[inumber % 128] = inode;
		disk_write(overallIndex, block.data);
		return total_bytes_written;
	}

	return 0;
}
