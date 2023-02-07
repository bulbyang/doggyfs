#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <errno.h>
#include "utils.h"

doggy_filesystem dfs;

// 文件系统初始化
int doggy_init()
{
    // 初始化fat
    for (size_t i = 0; i < MEMORI_SIZE / BLOCK_SIZE; i++)
        dfs.fat[i] = -2;

    // 创建根目录
    dfs.root_dir = (doggy_file *)malloc(sizeof(doggy_file));
    dfs.root_dir->isdir = 1;
    dfs.root_dir->filepath = "/";
    dfs.root_dir->start_block = 0;
    dfs.root_dir->size = BLOCK_SIZE;
    dfs.fat[0] = -1;

    return 0;
}

void doggy_destroy(void *v)
{
    return;
}

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int doggy_truncate(const char *path, off_t offset)
{
    // 查找文件
    doggy_file *file = path_search(path);
    if (file == NULL)
        return -ENOENT; // No such file or directory

    // 若文件大小与目标大小相等则直接return
    if (file->size == offset)
    {
        return 0;
    }

    int off = offset;
    int file_size = file->size;
    int blk_idx = file->start_block;

    // 寻找需要截取或扩展的地方
    while (file_size > BLOCK_SIZE && offset > BLOCK_SIZE)
    {
        file_size -= BLOCK_SIZE;
        offset -= BLOCK_SIZE;
        blk_idx = dfs.fat[blk_idx];
    }
    doggy_block *blk = &(dfs.blocks[blk_idx]);

    if (file_size < BLOCK_SIZE && offset < BLOCK_SIZE)
    {
        if (offset > file_size) // 扩展文件长度
        {
            memset(blk->file_data + file_size, '\0', offset - file_size);
            file->size = off;
        }
    }
    else if (file_size < BLOCK_SIZE) // 扩展文件长度
    {
        memset(blk->file_data + file_size, '\0', BLOCK_SIZE - file_size);
        while (offset > BLOCK_SIZE)
        {
            int new_idx = get_free_block_index();
            if (new_idx == -1)
            {
                return -ENOSPC; // No space left on device
            }
            dfs.fat[blk_idx] = new_idx;
            blk_idx = new_idx;
            blk = &(dfs.blocks[new_idx]);
            memset(blk->file_data, '\0', BLOCK_SIZE);
            offset -= BLOCK_SIZE;
        }
        if (offset)
        {
            int new_idx = get_free_block_index();
            if (new_idx == -1)
            {
                return -ENOSPC; // No space left on device
            }
            dfs.fat[blk_idx] = new_idx;
            blk_idx = new_idx;
            blk = &(dfs.blocks[new_idx]);
            memset(blk->file_data, '\0', offset);
        }
    }
    else // 减小文件长度
    {
        blk_idx = dfs.fat[blk_idx];
        while (file_size > 0)
        {
            int new_idx = dfs.fat[blk_idx];
            free_block(blk_idx);
            blk_idx = new_idx;
            file_size -= BLOCK_SIZE;
        }
    }

    file->size = off; // 将文件长度更改为 offset，
    return 0;
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int doggy_getattr(const char *path, struct stat *stbuf)
{
    // 初始化stbuf
    memset(stbuf, 0, sizeof(struct stat));

    // 若当前为根目录
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = __S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_size = BLOCK_SIZE;
        return 0;
    }

    doggy_file *cur_file = path_search(path);

    // 不存在该文件返回 -ENOENT
    if (cur_file == NULL)
    {
        return -ENOENT;
    }

    // 传递文件性质到stbuf中
    stbuf->st_mode = cur_file->mode;
    stbuf->st_nlink = 1;
    stbuf->st_size = cur_file->size;
    return 0;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */

int doggy_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi)
{
    // 查找目录
    doggy_file *cur_dir = path_search(path);

    // 若不存在返回 -ENOENT
    if (cur_dir == NULL)
    {
        return -ENOENT; // No such file or directory
    }
    if (cur_dir->isdir == 0)
    {
        return -ENOTDIR; // Not a directory
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // 遍历目录
    int block_index = cur_dir->start_block;
    while (block_index != -1)
    {
        doggy_block block = dfs.blocks[block_index];
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            if (block.directory[i] != NULL)
            {
                filler(buf, block.directory[i]->filename, NULL, 0);
            }
        }
        block_index = dfs.fat[block_index];
    }

    return 0;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */

int doggy_open(const char *path, struct fuse_file_info *fi)
{
    // 查找文件
    doggy_file *cur = path_search(path);
    if (cur == NULL)
    {
        return -ENOENT; // No such file or directory
    }
    return 0;
}

/** Create a directory
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits use  mode|S_IFDIR
 * */

int doggy_mkdir(const char *path, mode_t mode)
{
    // 若不存在同名文件或目录则进行创建
    if (path_search(path) == NULL)
    {
        char *name = path2name(path);                                 // 提取文件名
        char *dir_path = (char *)malloc(sizeof(char) * strlen(path)); // 提取上一级目录名称

        // 若此时上一级目录路径为空，则上一级目录为根目录
        if (strlen(path) - strlen(name) - 1 == 0)
        {
            dir_path = "/";
        }
        else
        {
            strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
        }
        // 查找上一级目录
        doggy_file *prt_dir = path_search(dir_path);
        if (prt_dir == NULL)
        {
            return -ENOENT; // No such file or directory
        }
        // 新建一个目录
        doggy_file *dir = (doggy_file *)malloc(sizeof(doggy_file));
        if (dir == NULL)
        {
            return -ENOSPC; // No space left on device
        }
        dir->isdir = 1;
        dir->mode = __S_IFDIR | mode;
        dir->size = BLOCK_SIZE;

        // 获取一块空间
        int start_block = get_free_block_index();
        if (start_block == -1)
        {
            return -ENOSPC; // No space left on device
        }
        dir->start_block = start_block;
        dir->filename = (char *)malloc(sizeof(char) * PATH_LENGTH);
        dir->filepath = (char *)malloc(sizeof(char) * PATH_LENGTH);
        strcpy(dir->filepath, path);
        strcpy(dir->filename, name);

        // 将新建目录放入上级目录中
        int blk_idx = prt_dir->start_block;
        while (blk_idx != -1)
        {
            doggy_block *block = &(dfs.blocks[blk_idx]);
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
            {
                if (block->directory[i] == NULL)
                {
                    block->directory[i] = dir;
                    return 0;
                }
            }
            blk_idx = dfs.fat[blk_idx];
        }

        // 若上级目录所有的块都被填满则新申请一块空间，并将新建目录放入
        int new_blk_idx = get_free_block_index();
        if (new_blk_idx < 0)
        {
            return -ENOSPC; // No space left on device
        }
        dfs.fat[blk_idx] = new_blk_idx;
        dfs.blocks[new_blk_idx].directory[0] = dir;

        free(name);

        return 0;
    }

    return -EEXIST; // File exists
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int doggy_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    // 若文件不存在则进行创建
    if (path_search(path) == NULL)
    {
        // 根据路径提取上级地址和文件名
        char *name = path2name(path);
        char *dir_path = (char *)malloc(sizeof(char) * strlen(path));
        if (strlen(path) - strlen(name) - 1 == 0)
        {
            dir_path = "/";
        }
        else
        {
            strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
        }

        // 查找上级目录
        doggy_file *dir = path_search(dir_path);
        if (dir == NULL)
        {
            return -ENOENT; // No such file or directory
        }
        if (dir->isdir == 0)
        {
            return -ENOTDIR; // Not a directory
        }

        // 创建一个文件
        doggy_file *file = (doggy_file *)malloc(sizeof(doggy_file));

        if (file == NULL)
        {
            return -ENOSPC; // No space left on device
        }

        file->filepath = (char *)malloc(sizeof(char) * strlen(path));
        file->filename = (char *)malloc(sizeof(char) * strlen(name));
        strcpy(file->filepath, path);
        strcpy(file->filename, name);
        file->isdir = 0;
        file->mode = mode;
        file->size = 0;
        file->start_block = -1;

        // 找到一个空的目录项存放该文件
        int dir_blk_idx = dir->start_block;
        while (dir_blk_idx != -1)
        {
            doggy_block *dir_blk = &(dfs.blocks[dir_blk_idx]);

            for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
            {
                if (dir_blk->directory[i] == NULL)
                {
                    dir_blk->directory[i] = file;
                    return 0;
                }
            }
        }

        int last_dir_blk_idx = get_last_block_index(dir->start_block);
        int new_blk_idx = get_free_block_index();
        if (new_blk_idx == -1)
        {
            return -ENOSPC;
        }

        doggy_block *new_blk = &(dfs.blocks[new_blk_idx]);
        new_blk->directory[0] = file;
        dfs.fat[last_dir_blk_idx] = new_blk_idx;
    }

    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */

int doggy_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    // return number of bytes read

    // 查找目标文件
    doggy_file *file = path_search(path);

    if (file == NULL)
    {
        return -ENOENT; // No such file or directory
    }
    // offset 超出文件大小
    if (offset > file->size)
    {
        return -EPERM; // Operation not permitted
    }

    // 设置读取长度
    size = size > (file->size - offset) ? file->size - offset : size;
    if (size == 0)
    {
        return 0;
    }

    int blk_idx = file->start_block;
    if (blk_idx == -1)
    {
        return -ENOSPC; // No space left on device
    }

    // 寻找开始读取的块
    while (offset > BLOCK_SIZE)
    {
        blk_idx = dfs.fat[blk_idx];
        offset -= BLOCK_SIZE;
    }

    // 将残缺的块写入buf
    doggy_block *blk = &(dfs.blocks[blk_idx]);
    if (offset)
    {
        if (size > BLOCK_SIZE - offset)
        {
            memcpy(buf, blk->file_data + offset, BLOCK_SIZE - offset);
            offset = BLOCK_SIZE - offset;
        }
        else
        {
            memcpy(buf, blk->file_data + offset, size);
            offset = size;
        }

        blk_idx = dfs.fat[blk_idx];
    }

    // 将剩余部分写入buf
    while (size > offset)
    {
        if (blk_idx == -1)
        {
            break;
        }
        blk = &(dfs.blocks[blk_idx]);
        if (size > (offset + BLOCK_SIZE))
        {
            memcpy(buf + offset, blk->file_data, BLOCK_SIZE);
            offset += BLOCK_SIZE;
        }
        else
        {
            memcpy(buf + offset, blk->file_data, size - offset);
            offset = size;
        }
        blk_idx = dfs.fat[blk_idx];
    }
    return offset;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.	 An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int doggy_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    int off = offset;

    // 查找文件
    doggy_file *file = path_search(path);
    if (file == NULL)
    {
        return -ENOENT; // No such file or directory
    }
    int blk_idx = file->start_block;
    if (blk_idx == -1 && size > 0)
    {
        blk_idx = get_free_block_index();
        if (blk_idx == -1)
        {
            return -ENOSPC;
        }
        file->start_block = blk_idx;
    }

    // 寻找开始写入的块
    while (offset > BLOCK_SIZE)
    {
        blk_idx = dfs.fat[blk_idx];
        offset -= BLOCK_SIZE;
    }

    // 将残缺的块写入
    if (offset)
    {
        doggy_block *blk = &(dfs.blocks[blk_idx]);
        if (size > BLOCK_SIZE - offset)
        {
            memcpy(blk->file_data + offset, buf, BLOCK_SIZE - offset);
            offset = BLOCK_SIZE - offset;
        }
        else
        {
            memcpy(blk->file_data + offset, buf, size);
            offset = size;
        }
        if (dfs.fat[blk_idx] == -1 && size > offset)
        {
            int n_blk_idx = get_free_block_index();
            if (n_blk_idx == -1)
                return -ENOSPC;
            dfs.fat[blk_idx] = n_blk_idx;
        }
        blk_idx = dfs.fat[blk_idx];
    }

    // 将剩余的块写入
    while (size > offset)
    {

        doggy_block *blk = &(dfs.blocks[blk_idx]);
        if (size > (offset + BLOCK_SIZE))
        {
            memcpy(blk->file_data, buf + offset, BLOCK_SIZE);
            offset += BLOCK_SIZE;
        }
        else
        {
            memcpy(blk->file_data, buf + offset, size - offset);
            offset = size;
        }
        if (dfs.fat[blk_idx] == -1 && size > offset)
        {
            int n_blk_idx = get_free_block_index();
            if (n_blk_idx == -1)
                return -ENOSPC; // No space left on device
            dfs.fat[blk_idx] = n_blk_idx;
        }
        blk_idx = dfs.fat[blk_idx];
    }
    file->size = off + offset;
    return offset;
}

/** Remove a file */
int doggy_unlink(const char *path)
{
    // 删除一个文件首先将文件的长度置为0，同时释放所有的块
    int status = doggy_truncate(path, 0);
    if (status < 0)
    {
        return status;
    }

    // 从path中提取文件名和上级目录地址
    char *name = path2name(path);
    char *dir_path = (char *)malloc(sizeof(char) * strlen(path));
    if (strlen(path) - strlen(name) - 1 == 0)
    {
        strcpy(dir_path, "/");
    }
    else
    {
        strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
    }

    doggy_file *dir = path_search(dir_path);
    free(dir_path);

    // 遍历目录查找到目标文件将该目录项置为空
    int blk_idx = dir->start_block;
    while (blk_idx != -1)
    {
        int flag = 1;
        doggy_block *block = &(dfs.blocks[blk_idx]);
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            if (block->directory[i] != NULL && strcmp(block->directory[i]->filepath, path) != 0)
            {
                flag = 0;
            }

            if (block->directory[i] != NULL && strcmp(block->directory[i]->filepath, path) == 0)
            {
                free(block->directory[i]->filename);
                free(block->directory[i]->filepath);
                free(block->directory[i]);
                block->directory[i] = NULL;
                return 0;
            }
        }
        if (flag)
        {
            if (blk_idx == dir->start_block)
            {
                dir->start_block = dfs.fat[blk_idx];
            }
            else
            {
                int tmp_blk = dir->start_block;
                while (dfs.fat[tmp_blk] != blk_idx && tmp_blk != -1)
                {
                    tmp_blk = dfs.fat[tmp_blk];
                }
                dfs.fat[tmp_blk] = dfs.fat[blk_idx];
            }
            blk_idx = dfs.fat[blk_idx];
            free_block(blk_idx);
        }
        else
            blk_idx = dfs.fat[blk_idx];
    }
    return -ENOENT; // No such file or directory
}

/** Remove a directory */

int doggy_rmdir(const char *path)
{
    // 不可删除根目录
    if (strcmp(path, "/") == 0)
    {
        return -EACCES;
    }

    // 根据path提取名称和上级目录地址
    char *name = path2name(path);
    char *dir_path = (char *)malloc(sizeof(char) * strlen(path));
    if (strlen(path) - strlen(name) - 1 == 0)
    {
        dir_path = "/";
    }
    else
    {
        strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
    }

    // 首先从上级目录中找到目标目录
    doggy_file *parent_dir = path_search(dir_path);
    int parent_blk_idx = parent_dir->start_block;
    int last_blk_idx = -1;
    while (parent_blk_idx != -1)
    {
        int flag = 1;
        doggy_block *block = &(dfs.blocks[parent_blk_idx]);
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            if (block->directory[i] != NULL && strcmp(block->directory[i]->filename, name) != 0)
            {
                flag = 0;
            }
            if (block->directory[i] != NULL && strcmp(block->directory[i]->filename, name) == 0)
            {
                doggy_file *del_dir = block->directory[i];
                if (!del_dir->isdir)
                {
                    return -ENOTDIR; // Not a directory
                }
                block->directory[i] = NULL;

                // 遍历要删除的目录，将其中的目录和文件都删除
                int blk_idx_del = del_dir->start_block;
                while (blk_idx_del != -1)
                {
                    doggy_block del_blk = dfs.blocks[blk_idx_del];
                    for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
                    {
                        if (del_blk.directory[i] != NULL)
                        {
                            if (del_blk.directory[i]->isdir)
                            {
                                doggy_rmdir(del_blk.directory[i]->filepath); // 删除目录
                            }
                            else
                            {
                                doggy_unlink(del_blk.directory[i]->filename); // 删除文件
                            }
                        }
                    }
                    int tmp_idx = blk_idx_del;
                    blk_idx_del = dfs.fat[blk_idx_del];
                    free_block(tmp_idx); // 回收块
                }
                free(del_dir->filename);
                free(del_dir->filepath);
                free(del_dir);
                return 0;
            }
        }
        // 如果一块中没有目录项则归还这一块
        if (flag)
        {
            if (last_blk_idx == -1)
            {
                parent_dir->start_block = dfs.fat[parent_blk_idx];
                free_block(parent_blk_idx);
            }
            else
            {
                dfs.fat[last_blk_idx] = dfs.fat[parent_blk_idx];
                free_block(parent_blk_idx);
            }
        }
        else
        {
            last_blk_idx = parent_blk_idx;
        }
        parent_blk_idx = dfs.fat[parent_blk_idx];
    }
    return -ENOENT;
}

/** Rename a file */
int doggy_rename(const char *path, const char *newpath)
{
    doggy_file *file = path_search(path);
    if (file == NULL)
        return -ENOENT; // No such file or directory
    if (file->isdir == 1)
    {
        return -EISDIR; // Is a directory
    }

    doggy_file *nf = (doggy_file *)malloc(sizeof(doggy_file));
    memcpy(nf, file, sizeof(doggy_file));
    strcpy(file->filepath, newpath);
    strcpy(file->filename, path2name(newpath));

    doggy_file *replaced = path_search(newpath);
    if (replaced != NULL && replaced->isdir == 0)
    {
        doggy_unlink(newpath);
    }

    char *name = path2name(path);
    char *dir_path = (char *)malloc(sizeof(char) * strlen(path));
    if (strlen(path) - strlen(name) - 1 == 0)
    {
        dir_path = "/";
    }
    else
    {
        strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
    }

    // 查找上级目录
    doggy_file *dir = path_search(dir_path);
    if (dir == NULL)
    {
        return -ENOENT; // No such file or directory
    }
    if (dir->isdir == 0)
    {
        return -ENOTDIR; // Not a directory
    }
    // 找到一个空的目录项存放该文件
    int dir_blk_idx = dir->start_block;
    while (dir_blk_idx != -1)
    {
        doggy_block *dir_blk = &(dfs.blocks[dir_blk_idx]);

        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            if (dir_blk->directory[i] == NULL)
            {
                dir_blk->directory[i] = nf;
                doggy_unlink(path); // 最后删除源文件
                return 0;
            }
        }
    }
    // 若原有的块已满则申请新的块
    int last_dir_blk_idx = get_last_block_index(dir->start_block);
    int new_blk_idx = get_free_block_index();
    if (new_blk_idx == -1)
    {
        return -ENOSPC;
    }
    doggy_block *new_blk = &(dfs.blocks[new_blk_idx]);
    new_blk->directory[0] = nf;
    dfs.fat[last_dir_blk_idx] = new_blk_idx;
    doggy_unlink(path); // 最后删除源文件
    return 0;
}

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given,
 * this method should check if opendir is permitted for this
 * directory. Optionally opendir may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to readdir, closedir and fsyncdir.
 *
 * Introduced in version 2.3
 */

int doggy_opendir(const char *path, struct fuse_file_info *fu)
{
    if (strcmp("/", path) == 0)
    {
        return 0;
    }

    doggy_file *file = path_search(path);
    if (file != NULL)
        return 0;

    return -ENOENT;
}

int doggy_flush(const char *path, struct fuse_file_info *f)
{
    return 0;
}

struct fuse_operations doggy_oper = {
    .getattr = doggy_getattr,
    .readdir = doggy_readdir,
    .opendir = doggy_opendir,
    .mkdir = doggy_mkdir,
    .rmdir = doggy_rmdir,
    .create = doggy_create,
    .truncate = doggy_truncate,
    .open = doggy_open,
    .read = doggy_read,
    .write = doggy_write,
    .unlink = doggy_unlink,
    .flush = doggy_flush,
    .destroy = doggy_destroy,
    .rename = doggy_rename};

int main(int argc, char *argv[])
{

    doggy_init();

    return fuse_main(argc, argv, &doggy_oper, NULL);
}
