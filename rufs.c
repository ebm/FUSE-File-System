/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
unsigned int inode_number = -1;
char temp_buffer[BLOCK_SIZE];

unsigned int inode_bitmap_block;
unsigned int inode_block;
unsigned int data_bitmap_block;
unsigned int data_block;

unsigned int max_dirent_count;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	// Step 1: Read inode bitmap from disk
	int curr_block_index = inode_bitmap_block;
	bio_read(curr_block_index, temp_buffer);
	// Step 2: Traverse inode bitmap to find an available slot
	int size = MAX_INUM;
	int count = 0;
	for (int i = 0; i < size; i++) {
		if (i >= BLOCK_SIZE) {
			curr_block_index++;
			bio_read(curr_block_index, temp_buffer);
			size -= i;
			i = 0;
		}
		if (get_bitmap((bitmap_t) temp_buffer, i) == 0) {
			set_bitmap((bitmap_t) temp_buffer, i);
			bio_write(curr_block_index, temp_buffer);
			return count;
		}
		count++;
	}
	// Step 3: Update inode bitmap and write to disk 
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	// Step 1: Read data block bitmap from disk
	int curr_block_index = data_bitmap_block;
	bio_read(curr_block_index, temp_buffer);
	// Step 2: Traverse data block bitmap to find an available slot
	int size = MAX_DNUM;
	int count = 0;
	for (int i = 0; i < size; i++) {
		if (i >= BLOCK_SIZE) {
			curr_block_index++;
			bio_read(curr_block_index, temp_buffer);
			size -= i;
			i = 0;
		}
		if (get_bitmap((bitmap_t) temp_buffer, i) == 0) {
			// Step 3: Update data block bitmap and write to disk 
			set_bitmap((bitmap_t) temp_buffer, i);
			bio_write(curr_block_index, temp_buffer);
			return count;
		}
		count++;
	}
	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	// Step 1: Get the inode's on-disk block number
	unsigned int block_number = inode_block + (ino * sizeof(struct inode)) / BLOCK_SIZE;
  	// Step 2: Get offset of the inode in the inode on-disk block
	unsigned int block_offset = (ino * sizeof(struct inode)) % BLOCK_SIZE;
  	// Step 3: Read the block from disk and then copy into inode structure
	bio_read(block_number, temp_buffer);
	for (int i = 0; i < sizeof(struct inode); i++) {
		((char*) inode)[i] = temp_buffer[i + block_offset];
	}
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	// Step 1: Get the block number where this inode resides on disk
	unsigned int block_number = inode_block + (ino * sizeof(struct inode)) / BLOCK_SIZE;
	// Step 2: Get the offset in the block where this inode resides on disk
	unsigned int block_offset = (ino * sizeof(struct inode)) % BLOCK_SIZE;
	// Step 3: Write inode to disk 
	bio_read(block_number, temp_buffer);
	for (int i = 0; i < sizeof(struct inode); i++) {
		temp_buffer[i + block_offset] = ((char*) inode)[i];
	}
	bio_write(block_number, temp_buffer);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
  	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode search;
	readi(ino, &search);
  	int total_blocks = (((int) search.size - 1) / BLOCK_SIZE) + 1;
	for (int i = 0; i < total_blocks && i < 16 && search.size != 0; i++) {
		// Step 2: Get data block of current directory from inode
		if (search.direct_ptr[i] == 0) {
			total_blocks++;
			continue;
		}
		bio_read(search.direct_ptr[i], temp_buffer);
		struct dirent* directory_dirent_list = (struct dirent*) temp_buffer;
		// Step 3: Read directory's data block and check each directory entry.
  		//If the name matches, then copy directory entry to dirent structure
		for (int j = 0; j < max_dirent_count && j * sizeof(struct dirent) + i * BLOCK_SIZE < search.size; j++) {
			if (directory_dirent_list[j].valid == 0) continue;
			if (strcmp(fname, directory_dirent_list[j].name) == 0) {
				for (int k = 0; k < sizeof(struct dirent); k++) {
					((char*) dirent)[k] = ((char*) &directory_dirent_list[j])[k];
				}
				return 0;
			}
		}
	}
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	unsigned int block_number = -1;
	unsigned int offset = -1;
	struct dirent* directory_dirent_list;
	for (int i = 0; i < 16; i++) {
		if (dir_inode.direct_ptr[i] == 0) {
			dir_inode.direct_ptr[i] = data_block + get_avail_blkno();
			memset(temp_buffer, 0, BLOCK_SIZE);
			bio_write(dir_inode.direct_ptr[i], temp_buffer);
			offset = 0;
			block_number = i;
			dir_inode.size = i * BLOCK_SIZE;
			break;
		}
		bio_read(dir_inode.direct_ptr[i], temp_buffer);
		directory_dirent_list = (struct dirent*) temp_buffer;
		for (int j = 0; j < max_dirent_count; j++) {
			if (directory_dirent_list[j].valid == 0 || strcmp(fname, directory_dirent_list[j].name) == 0) {
				offset = j;
				block_number = i;
				break;
			}
		}
		if (offset != -1) break;
	}
	if (offset == -1) {
		return -1;
	}
	dir_inode.size += sizeof(struct dirent);
	writei(dir_inode.ino, &dir_inode);
	bio_read(dir_inode.direct_ptr[block_number], temp_buffer);
	directory_dirent_list = (struct dirent*) temp_buffer;
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	directory_dirent_list[offset].ino = f_ino;
	directory_dirent_list[offset].len = name_len;
	strcpy(directory_dirent_list[offset].name, fname);
	directory_dirent_list[offset].valid = 1;

	// Write directory entry
	bio_write(dir_inode.direct_ptr[block_number], temp_buffer);
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	if (strcmp(path, "/") == 0) {
		readi(ino, inode);
		return 0;
	}
	char path_buffer[PATH_MAX];
	int start = 1;
	uint16_t curr_ino = ino;
	struct dirent dirent;
	for (int i = 1; i < strlen(path) + 1; i++) {
		if (path[i] == '/' || path[i] == '\0') {
			if (path[i] == '\0') i++;
			strncpy(path_buffer, path + start, i - start);
			path_buffer[i - start] = '\0';
			if (dir_find(curr_ino, path_buffer, i - start, &dirent) != 0) return -1;
			curr_ino = dirent.ino;
			start = i + 1;
		}
	}
	readi(curr_ino, inode);
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	// write superblock information
	unsigned int superblock_block_size = ((sizeof(struct superblock) - 1) / (BLOCK_SIZE) + 1);
	unsigned int inum_bitmap_block_size = ((MAX_INUM / 8 - 1) / (BLOCK_SIZE) + 1);
	unsigned int dnum_bitmap_block_size = ((MAX_DNUM / 8 - 1) / (BLOCK_SIZE) + 1);
	unsigned int inum_block_size = ((MAX_INUM * sizeof(struct inode) - 1) / (BLOCK_SIZE) + 1);

	memset(temp_buffer, 0, BLOCK_SIZE);
	struct superblock* block = (struct superblock*) temp_buffer;
	block->magic_num = MAGIC_NUM;
	block->max_inum = MAX_INUM;
	block->max_dnum = MAX_DNUM;

	inode_bitmap_block = superblock_block_size;
	data_bitmap_block = superblock_block_size + inum_bitmap_block_size;
	inode_block = superblock_block_size + inum_bitmap_block_size + dnum_bitmap_block_size;
	data_block = superblock_block_size + inum_bitmap_block_size + dnum_bitmap_block_size + inum_block_size;

	block->i_bitmap_blk = inode_bitmap_block;
	block->d_bitmap_blk = data_bitmap_block;
	block->i_start_blk = inode_block;
	block->d_start_blk = data_block;

	bio_write(0, temp_buffer);
	// initialize inode bitmap
	// initialize data block bitmap
	memset(temp_buffer, 0, BLOCK_SIZE);
	bio_write(data_bitmap_block, temp_buffer);
	// update bitmap information for root directory
	set_bitmap((bitmap_t) temp_buffer, 0);
	bio_write(inode_bitmap_block, temp_buffer);
	// update inode for root directory
	struct inode* directory_inode = (struct inode*) temp_buffer;
	directory_inode->ino = ++inode_number;
	directory_inode->valid = 1;
	directory_inode->size = 0;
	directory_inode->type = 1;
	directory_inode->link = 1;
	directory_inode->vstat.st_mode = S_IFDIR | 0755;
	directory_inode->vstat.st_nlink = 2;
	time(&directory_inode->vstat.st_mtime);
	bio_write(inode_block, temp_buffer);
	max_dirent_count = BLOCK_SIZE / sizeof(struct dirent);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
	// Step 1a: If disk file is not found, call mkfs
	if (inode_number == -1) {
		rufs_mkfs();
	}
  	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk

	return NULL;
}

static void rufs_destroy(void *userdata) {
	// Step 1: De-allocate in-memory data structures
	
	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	// Step 1: call get_node_by_path() to get inode from path
	struct inode terminal_inode;
	if (get_node_by_path(path, 0, &terminal_inode) != 0) return -ENOENT;
	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_atime = time(NULL);
	stbuf->st_mtime = time(NULL);
	if (terminal_inode.type == 1) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_blksize = BLOCK_SIZE;
	} else {
		stbuf->st_mode = S_IFREG | 0755;
		stbuf->st_nlink = 1;
		stbuf->st_blksize = BLOCK_SIZE;
		stbuf->st_size = terminal_inode.size;
	}
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode terminal_inode;
	return get_node_by_path(path, 0, &terminal_inode);
	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode terminal_inode;
	if (get_node_by_path(path, 0, &terminal_inode) != 0) return -1;
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	int total_blocks = (((int) terminal_inode.size - 1) / BLOCK_SIZE) + 1;
	for (int i = 0; i < total_blocks && terminal_inode.size != 0; i++) {
		if (terminal_inode.direct_ptr[i] == 0) continue;
		bio_read(terminal_inode.direct_ptr[i], temp_buffer);
		struct dirent* directory_dirent_list = (struct dirent*) temp_buffer;
		for (int j = 0; j < max_dirent_count && j * sizeof(struct dirent) < terminal_inode.size; j++) {
			if (directory_dirent_list[j].valid == 0) continue;
			filler(buffer, directory_dirent_list[j].name, NULL, 0);
		}
	}
	return 0;
}
void countblocks() {
    int inode_count = 0;
    int data_count = 0;
	bio_read(inode_bitmap_block, temp_buffer);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        inode_count += get_bitmap((bitmap_t) temp_buffer, i);
    }
	bio_read(data_bitmap_block, temp_buffer);
	for (int i = 0; i < BLOCK_SIZE; i++) {
        data_count += get_bitmap((bitmap_t) temp_buffer, i);
    }

    fprintf(stderr, "i: %d d: %d\n", inode_count, data_count);
}

static int rufs_mkdir(const char *path, mode_t mode) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char directory_path_arr[strlen(path) + 1];
	char base_name_arr[strlen(path) + 1];
	strcpy(directory_path_arr, path);
	strcpy(base_name_arr, path);
	char* directory_path = dirname((char*) directory_path_arr);
	char* base_name = basename((char*) base_name_arr);
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode directory_inode;
	get_node_by_path(directory_path, 0, &directory_inode);
	// Step 3: Call get_avail_ino() to get an available inode number
	int available_ino = get_avail_ino();
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	if (dir_add(directory_inode, available_ino, base_name, strlen(base_name)) != 0) return -1;
	// Step 5: Update inode for target directory
	struct inode target_inode;
	readi(available_ino, &target_inode);

	target_inode.ino = available_ino;
	target_inode.valid = 1;			
	target_inode.size = 0;		
	target_inode.type = 1;
	for (int i = 0; i < 16; i++) {
		target_inode.direct_ptr[i] = 0;
	}
	writei(target_inode.ino, &target_inode);
	rufs_getattr(path, &target_inode.vstat);
	target_inode.vstat.st_mode = mode;
	// Step 6: Call writei() to write inode to disk
	writei(target_inode.ino, &target_inode);
	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char directory_path_arr[strlen(path) + 1];
	char base_name_arr[strlen(path) + 1];
	strcpy(directory_path_arr, path);
	strcpy(base_name_arr, path);
	char* directory_path = dirname((char*) directory_path_arr);
	char* base_name = basename((char*) base_name_arr);
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode directory_inode;
	get_node_by_path(directory_path, 0, &directory_inode);
	// Step 3: Call get_avail_ino() to get an available inode number
	int available_ino = get_avail_ino();
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	dir_add(directory_inode, available_ino, base_name, strlen(base_name));
	// Step 5: Update inode for target file
	struct inode target_inode;
	readi(available_ino, &target_inode);

	target_inode.ino = available_ino;
	target_inode.valid = 1;
	target_inode.size = 0;
	target_inode.type = 0;
	for (int i = 0; i < 16; i++) {
		target_inode.direct_ptr[i] = 0;
	}
	writei(target_inode.ino, &target_inode);
	rufs_getattr(path, &target_inode.vstat);
	target_inode.vstat.st_mode = mode;
	// Step 6: Call writei() to write inode to disk
	writei(target_inode.ino, &target_inode);
	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode path_inode;
	return get_node_by_path(path, 0, &path_inode);
	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode file_inode;
	get_node_by_path(path, 0, &file_inode);
	int count = 0;
	// Step 2: Based on size and offset, read its data blocks from disk
	int curr_block_index = offset / BLOCK_SIZE;
	if (curr_block_index >= 16 || file_inode.direct_ptr[curr_block_index] == 0) return count;
	bio_read(file_inode.direct_ptr[curr_block_index], temp_buffer);
	int bytes_left = size;
	int curr_offset = offset % BLOCK_SIZE;
	for (int i = curr_offset; i - curr_offset < bytes_left && count < file_inode.size; i++) {
		if (i >= BLOCK_SIZE) {
			curr_offset = 0;
			curr_block_index++;
			if (file_inode.direct_ptr[curr_block_index] == 0) return count;
			bio_read(file_inode.direct_ptr[curr_block_index], temp_buffer);
			bytes_left -= i;
			i = 0;
		}
		// Step 3: copy the correct amount of data from offset to buffer
		buffer[count] = temp_buffer[i];
		count++;
	}
	// Note: this function should return the amount of bytes you copied to buffer
	return count;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	if (size == 0) return 0;
	struct inode file_inode;
	if (get_node_by_path(path, 0, &file_inode) != 0) {
		return -1;
	}
	int count = 0;
	// Step 2: Based on size and offset, read its data blocks from disk
	int curr_block_index = offset / BLOCK_SIZE;
	if (curr_block_index >= 16) return -1;
	// initialize data
	for (int i = 0; i <= curr_block_index; i++) {
		if (file_inode.direct_ptr[i] == 0) {
			file_inode.direct_ptr[i] = data_block + get_avail_blkno();
		}
	}
	bio_read(file_inode.direct_ptr[curr_block_index], temp_buffer);
	int bytes_left = size;
	int curr_offset = offset % BLOCK_SIZE;
	for (int i = curr_offset; i - curr_offset < bytes_left; i++) {
		if (i >= BLOCK_SIZE) {
			curr_offset = 0;
			bio_write(file_inode.direct_ptr[curr_block_index], temp_buffer);
			curr_block_index++;
			if (curr_block_index >= 16) break;
			if (file_inode.direct_ptr[curr_block_index] == 0) {
				file_inode.direct_ptr[curr_block_index] = data_block + get_avail_blkno();
			}
			bio_read(file_inode.direct_ptr[curr_block_index], temp_buffer);
			bytes_left -= count;

			i = 0;
		}
		// Step 3: Write the correct amount of data from offset to disk
		temp_buffer[i] = buffer[count];
		count++;
	}
	// Step 4: Update the inode info and write it to disk
	bio_write(file_inode.direct_ptr[curr_block_index], temp_buffer);

	if (offset + count > file_inode.size) {
		file_inode.size = offset + count;
	}
	writei(file_inode.ino, &file_inode);
	// Note: this function should return the amount of bytes you write to disk
	return count;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

