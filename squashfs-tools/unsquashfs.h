/*
 * Unsquash a squashfs filesystem.  This is a highly compressed read only filesystem.
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
 * Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * unsquashfs.h
 */
#define CONFIG_SQUASHFS_1_0_COMPATIBILITY
#define CONFIG_SQUASHFS_2_0_COMPATIBILITY

#define TRUE 1
#define FALSE 0
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <zlib.h>
#include <sys/mman.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <regex.h>
#include <fnmatch.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#ifndef linux
#define __BYTE_ORDER BYTE_ORDER
#define __BIG_ENDIAN BIG_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#else
#include <endian.h>
#endif

#include "squashfs_fs.h"
#include "squashfs_compat.h"
#include "global.h"

#ifdef SQUASHFS_TRACE
#define TRACE(s, args...)		do { \
						pthread_mutex_lock(&screen_mutex); \
						if(progress_enabled) \
							printf("\n"); \
						printf("unsquashfs: "s, ## args); \
						pthread_mutex_unlock(&screen_mutex);\
					} while(0)
#else
#define TRACE(s, args...)
#endif

#define ERROR(s, args...)		do { \
						pthread_mutex_lock(&screen_mutex); \
						if(progress_enabled) \
							fprintf(stderr, "\n"); \
						fprintf(stderr, s, ## args); \
						pthread_mutex_unlock(&screen_mutex);\
					} while(0)

#define EXIT_UNSQUASH(s, args...)	do { \
						pthread_mutex_lock(&screen_mutex); \
						fprintf(stderr, "FATAL ERROR aborting: "s, ## args); \
						pthread_mutex_unlock(&screen_mutex);\
						exit(1); \
					} while(0)

#define CALCULATE_HASH(start)	(start & 0xffff)

struct hash_table_entry {
	int	start;
	int	bytes;
	struct hash_table_entry *next;
};

struct inode {
	int blocks;
	char *block_ptr;
	long long data;
	int fragment;
	int frag_bytes;
	gid_t gid;
	int inode_number;
	int mode;
	int offset;
	long long start;
	char symlink[65536];
	time_t time;
	int type;
	uid_t uid;
};

typedef struct squashfs_operations {
	struct dir *(*squashfs_opendir)(unsigned int block_start, unsigned int offset, struct inode **i);
	void (*read_fragment)(unsigned int fragment, long long *start_block, int *size);
	void (*read_fragment_table)();
	void (*read_block_list)(unsigned int *block_list, char *block_ptr, int blocks);
	struct inode *(*read_inode)(unsigned int start_block, unsigned int offset);
	void (*read_uids_guids)();
} squashfs_operations;

struct test {
	int mask;
	int value;
	int position;
	char mode;
};


/* Cache status struct.  Caches are used to keep
  track of memory buffers passed between different threads */
struct cache {
	int	max_buffers;
	int	count;
	int	buffer_size;
	int	wait_free;
	int	wait_pending;
	pthread_mutex_t	mutex;
	pthread_cond_t wait_for_free;
	pthread_cond_t wait_for_pending;
	struct cache_entry *free_list;
	struct cache_entry *hash_table[65536];
};

/* struct describing a cache entry passed between threads */
struct cache_entry {
	struct cache *cache;
	long long block;
	int	size;
	int	used;
	int error;
	int	pending;
	struct cache_entry *hash_next;
	struct cache_entry *hash_prev;
	struct cache_entry *free_next;
	struct cache_entry *free_prev;
	char *data;
};

/* struct describing queues used to pass data between threads */
struct queue {
	int	size;
	int	readp;
	int	writep;
	pthread_mutex_t	mutex;
	pthread_cond_t empty;
	pthread_cond_t full;
	void **data;
};

/* default size of fragment buffer in Mbytes */
#define FRAGMENT_BUFFER_DEFAULT 256
/* default size of data buffer in Mbytes */
#define DATA_BUFFER_DEFAULT 256

#define DIR_ENT_SIZE	16

struct dir_ent	{
	char		name[SQUASHFS_NAME_LEN + 1];
	unsigned int	start_block;
	unsigned int	offset;
	unsigned int	type;
};

struct dir {
	int		dir_count;
	int 		cur_entry;
	unsigned int	mode;
	uid_t		uid;
	gid_t		guid;
	unsigned int	mtime;
	struct dir_ent	*dirs;
};

/* globals */
extern squashfs_super_block_3 sBlk;
extern squashfs_operations s_ops;
extern int swap;
extern char *inode_table, *directory_table;
extern struct hash_table_entry *inode_table_hash[65536], *directory_table_hash[65536];
extern squashfs_fragment_entry_3 *fragment_table;
extern squashfs_fragment_entry_2 *fragment_table_2;
extern unsigned int *uid_table, *guid_table;
extern pthread_mutex_t screen_mutex;
extern int progress_enabled;
extern int inode_number;
extern int lookup_type[];

/* unsquash-1.c */
extern void read_block_list_1(unsigned int *, char *, int);
extern void read_fragment_table_1();
extern struct inode *read_inode_1(unsigned int, unsigned int);
extern struct dir *squashfs_opendir_1(unsigned int, unsigned int, struct inode **);
extern void read_uids_guids_1();

/* unsquash-2.c */
extern void read_block_list_2(unsigned int *, char *, int);
extern void read_fragment_table_2();
extern void read_fragment_2(unsigned int, long long *, int *);
extern struct inode *read_inode_2(unsigned int, unsigned int);

/* unsquash-3.c */
extern void read_fragment_table_3();
extern void read_fragment_3(unsigned int, long long *, int *);
extern struct inode *read_inode_3(unsigned int, unsigned int);
extern struct dir *squashfs_opendir_3(unsigned int, unsigned int, struct inode **);