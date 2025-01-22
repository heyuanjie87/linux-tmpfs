#include <linux/vfs/fs.h>

#include "fileops.c"

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

struct shmem_options
{
};

static struct inode *__shmem_get_inode(struct mnt_idmap *idmap,
									   struct super_block *sb,
									   struct inode *dir, umode_t mode,
									   dev_t dev, unsigned long flags);

static const struct inode_operations shmem_inode_operations;

static const char *shmem_get_link(struct dentry *dentry, struct inode *inode,
								  struct delayed_call *callback)
{
	return inode->i_link;
}

static const struct inode_operations shmem_short_symlink_operations = {
	.get_link = shmem_get_link,
};

static inline struct inode *shmem_get_inode(struct mnt_idmap *idmap,
											struct super_block *sb, struct inode *dir,
											umode_t mode, dev_t dev, unsigned long flags)
{
	return __shmem_get_inode(idmap, sb, dir, mode, dev, flags);
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int shmem_mknod(struct mnt_idmap *idmap, struct inode *dir,
					   struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = 0;

	inode = shmem_get_inode(idmap, dir->i_sb, dir, mode, dev, 0);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

    error = tmpfs_add_sub_dentry(dir, inode, dentry);
	if (error)
		goto out_iput;

	dir->i_size += BOGO_DIRENT_SIZE;
	d_instantiate(dentry, inode);
	dget(dentry); /* Extra count - pin the dentry in core */
    return 0;

out_iput:
	iput(inode);
	return error;
}

static int shmem_mkdir(struct mnt_idmap *idmap, struct inode *dir,
					   struct dentry *dentry, umode_t mode)
{
	int error;

	error = shmem_mknod(idmap, dir, dentry, mode | S_IFDIR, 0);
	if (error)
		return error;
	inc_nlink(dir);
	return 0;
}

static int shmem_create(struct mnt_idmap *idmap, struct inode *dir,
						struct dentry *dentry, umode_t mode, bool excl)
{
	return shmem_mknod(idmap, dir, dentry, mode | S_IFREG, 0);
}

static int shmem_symlink(struct mnt_idmap *idmap, struct inode *dir,
						 struct dentry *dentry, const char *symname)
{
	int error;
	int len;
	struct inode *inode;
	struct folio *folio;

	len = strlen(symname) + 1;

	inode = shmem_get_inode(idmap, dir->i_sb, dir, S_IFLNK | 0777, 0,
							0);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	pr_todo();

	inode->i_size = len - 1;
	inode->i_link = kmemdup(symname, len, GFP_KERNEL);
	inode->i_op = &shmem_short_symlink_operations;

	dir->i_size += BOGO_DIRENT_SIZE;

	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename2(struct mnt_idmap *idmap,
						 struct inode *old_dir, struct dentry *old_dentry,
						 struct inode *new_dir, struct dentry *new_dentry,
						 unsigned int flags)
{
	int error;

	if (!simple_empty(new_dentry))
	{
		error = -ENOTEMPTY;
	}

	return error;
}

static const struct inode_operations shmem_dir_inode_operations = {
	.lookup = simple_lookup,
	.mkdir = shmem_mkdir,
	.create = shmem_create,
	.mknod = shmem_mknod,
	.symlink = shmem_symlink,
	.rename = shmem_rename2,
};

static struct inode *__shmem_get_inode(struct mnt_idmap *idmap,
									   struct super_block *sb,
									   struct inode *dir, umode_t mode,
									   dev_t dev, unsigned long flags)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
	{
		return ERR_PTR(-ENOSPC);
	}

	inode->i_mode = mode;

	switch (mode & S_IFMT)
	{
	case S_IFDIR:
	{
		inc_nlink(inode);
		/* Some things misbehave if size == 0 on a directory */
		inode->i_size = 2 * BOGO_DIRENT_SIZE;
		inode->i_op = &shmem_dir_inode_operations;
		inode->i_fop = &tmpfs_dir_fops;
		simple_offset_init(shmem_get_offset_ctx(inode));
	}
	break;
	case S_IFREG:
	{
		inode->i_op = &shmem_inode_operations;
		inode->i_fop = &shmem_file_operations;
	}
	break;
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
	{
		init_special_inode(inode, mode, dev);
	}
	break;
	}

	tmpfs_inode_init(inode);

	return inode;
}

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *info;

	info = kzalloc(sizeof(*info), 0);
	if (!info)
		return NULL;

	return &info->vfs_inode;
}

static const struct super_operations shmem_ops = {
	.alloc_inode = shmem_alloc_inode,
};

static int shmem_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;

	pr_todo();

	sb->s_op = &shmem_ops;

	inode = shmem_get_inode(NULL, sb, NULL,
							S_IFDIR | 0, 0, 0);

	sb->s_root = d_make_root(inode);

	return 0;
}

static void shmem_free_fc(struct fs_context *fc)
{
}

static int shmem_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, shmem_fill_super);
}

static const struct fs_context_operations shmem_fs_context_ops = {
	.free = shmem_free_fc,
	.get_tree = shmem_get_tree,
};

int shmem_init_fs_context(struct fs_context *fc)
{
	struct shmem_options *ctx;

	ctx = kzalloc(sizeof(struct shmem_options), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	fc->ops = &shmem_fs_context_ops;

	return 0;
}

static struct file_system_type shmem_fs_type = {
	.name = "tmpfs",
	.init_fs_context = shmem_init_fs_context,
};

int tmpfs_init(void)
{
	register_filesystem(&shmem_fs_type);

	return 0;
}
