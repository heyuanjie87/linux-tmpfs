#pragma once

#include <linux/types.h>

struct shmem_inode_info
{
	struct inode vfs_inode;
	union
	{
		struct offset_ctx dir_offsets; /* stable directory offsets */
		struct 
		{
			struct offset_ctx dir_offsets;

            struct list_head subdirs;
            struct list_head child;
		} _d;
	};
};

static inline struct shmem_inode_info *SHMEM_I(struct inode *inode)
{
	return container_of(inode, struct shmem_inode_info, vfs_inode);
}

static inline struct offset_ctx *shmem_get_offset_ctx(struct inode *inode)
{
	return &SHMEM_I(inode)->dir_offsets;
}
