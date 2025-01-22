#include <linux/vfs/fs.h>

#include "tmpfs.h"

static struct dentry *offset_find_next(struct offset_ctx *octx, loff_t offset)
{
    MA_STATE(mas, &octx->mt, offset, offset);
    struct dentry *child, *found = NULL;

    child = mas_find(&mas, LONG_MAX);
    if (child)
    {
        d_lock(child);

        if (simple_positive(child))
            found = dget_dlock(child);

        d_unlock(child);
    }

    return found;
}

static inline uintptr_t dentry2offset(struct dentry *dentry)
{
    return (uintptr_t)dentry->d_fsdata;
}

static bool offset_dir_emit(struct dir_context *ctx, struct dentry *dentry)
{
    struct inode *inode = d_inode(dentry);
    uintptr_t offset = dentry2offset(dentry);

    return ctx->actor(ctx, dentry->d_name.name, dentry->d_name.len, offset,
                      inode->i_ino, fs_umode_to_dtype(inode->i_mode));
}

static void offset_iterate_dir(struct inode *inode, struct dir_context *ctx, uintptr_t last_index)
{
    struct offset_ctx *octx;
    struct dentry *dentry;

    octx = &(SHMEM_I(inode)->dir_offsets);

    while (true)
    {
        dentry = offset_find_next(octx, ctx->pos);
        if (!dentry)
            return;

        if (dentry2offset(dentry) >= last_index)
        {
            dput(dentry);
            return;
        }

        if (!offset_dir_emit(ctx, dentry))
        {
            dput(dentry);
            return;
        }

        ctx->pos = dentry2offset(dentry) + 1;
        dput(dentry);
    }
}

static int offset_readdir(struct file *file, struct dir_context *ctx)
{
    struct dentry *dir;
    uintptr_t last_index = (uintptr_t)file->private_data;

    dir = path_dentry(&file->f_path);

    if (!dir_emit_dots(file, ctx))
        return 0;

    offset_iterate_dir(d_inode(dir), ctx, last_index);

    return 0;
}

static int offset_dir_open(struct inode *inode, struct file *file)
{
    struct offset_ctx *ctx = shmem_get_offset_ctx(inode);

    file->private_data = (void *)ctx->next_offset;

    return 0;
}

static const struct file_operations simple_offset_dir_operations = {
    .open = offset_dir_open,
    .iterate_shared = offset_readdir,
    .read = generic_read_dir,
};

static int shmem_file_open(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t shmem_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    ssize_t ret = 0;

    return ret;
}

static const struct file_operations shmem_file_operations = {
    .open = shmem_file_open,
    .write_iter = shmem_file_write_iter,
};

/***********************************************************/
static struct dentry *list_find_next(struct shmem_inode_info *dir_info, uintptr_t last_index)
{
    struct shmem_inode_info *pos;
    struct dentry *found = NULL, *child;

    list_for_each_entry(pos, &dir_info->_d.subdirs, _d.child)
    {
        child = pos->_d.dir_offsets.mt.ma_root;
        if (child->d_fsdata >= (void *)last_index)
        {
            d_lock(child);

            if (simple_positive(child))
                found = dget_dlock(child);

            d_unlock(child);

            break;
        }
    }

    return found;
}

static void list_iterate_dir(struct inode *inode, struct dir_context *ctx, uintptr_t last_index)
{
    struct dentry *dentry;
    struct shmem_inode_info *child, *dir_info;

    dir_info = SHMEM_I(inode);

    while (true)
    {
        dentry = list_find_next(dir_info, ctx->pos);
        if (!dentry)
            return;

        if (!offset_dir_emit(ctx, dentry))
        {
            dput(dentry);
            return;
        }

        ctx->pos = dentry2offset(dentry) + 1;
        dput(dentry);
    }
}

static int list_dir_open(struct inode *inode, struct file *file)
{
    struct offset_ctx *ctx = shmem_get_offset_ctx(inode);

    file->private_data = (void *)2;

    return 0;
}

static int list_readdir(struct file *file, struct dir_context *ctx)
{
    struct dentry *dir;
    uintptr_t last_index = (uintptr_t)file->private_data;

    dir = path_dentry(&file->f_path);

    if (!dir_emit_dots(file, ctx))
        return 0;

    list_iterate_dir(d_inode(dir), ctx, last_index);

    return 0;
}

static const struct file_operations tmpfs_dir_fops = {
    .open = list_dir_open,
    .iterate_shared = list_readdir,
    .read = generic_read_dir,
};

static void tmpfs_inode_init(struct inode *inode)
{
    struct shmem_inode_info *info = SHMEM_I(inode);

    INIT_LIST_HEAD(&info->_d.subdirs);
    INIT_LIST_HEAD(&info->_d.child);
}

static int tmpfs_add_sub_dentry(struct inode *dir, struct inode *inode, struct dentry *dentry)
{
    int error = 0;
    struct offset_ctx *octx;
    struct shmem_inode_info *child, *dir_info;

    dir_info = SHMEM_I(dir);
    child = SHMEM_I(inode);

    octx = shmem_get_offset_ctx(dir);

    d_set_fsdata(dentry, octx->next_offset);
    octx->next_offset++;

    child->_d.dir_offsets.mt.ma_root = dentry;
    list_add_tail(&child->_d.child, &dir_info->_d.subdirs);

    return error;
}
