#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <errno.h>
#include "utils.h"

#define MEMORI_SIZE 256 * 1024 * 1024
#define BLOCK_SIZE 1024
#define NAME_LENGTH 64
#define PATH_LENGTH 256

int NNODES;
int NBLOCKS;
int persistent = 0;

doggy_file *current_dir;
doggy_file *last_dir;
doggy_filesystem dfs;

int doggy_init(char *root_path)
{

    for (size_t i = 0; i < MEMORI_SIZE / BLOCK_SIZE; i++)
        dfs.fat[i] = -2;
    dfs.root_dir = (doggy_file *)malloc(sizeof(doggy_file));
    dfs.root_dir->isdir = 1;
    dfs.root_dir->filepath = "/";
    dfs.root_dir->start_block = 0;
    dfs.root_dir->size = BLOCK_SIZE;
    dfs.fat[0] = -1;
    current_dir = dfs.root_dir;
    last_dir = dfs.root_dir;
    dfs.files_n = 1;

    return 0;
}

void doggy_destroy(void *v)
{
    return;
}

int doggy_getattr(const char *path, struct stat *stbuf)
{

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = __S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_size = BLOCK_SIZE;
        return 0;
    }
    doggy_file *cur_file = path_search(path);

    if (cur_file == NULL)
    {
        // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        // fputs("no such file\n", fp);
        // fclose(fp);
        return -ENOENT;
    }
    stbuf->st_mode = cur_file->mode;
    stbuf->st_nlink = 1;
    stbuf->st_size = cur_file->size;
    return 0;
}

int doggy_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi)
{
    // printf("ok");
    doggy_file *cur_dir = path_search(path);
    // printf("ok");
    if (cur_dir == NULL || cur_dir->isdir == 0)
    {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    int block_index = cur_dir->start_block;
    while (block_index != -1)
    {
        doggy_block block = dfs.blocks[block_index];
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
            // fputc('0' + i, fp);
            // fclose(fp);
            if (block.directory[i] != NULL)
            {
                filler(buf, block.directory[i]->filename, NULL, 0);
            }
        }
        block_index = dfs.fat[block_index];
    }

    return 0;
}

int doggy_open(const char *path, struct fuse_file_info *fi)
{
    doggy_file *cur = path_search(path);
    if (cur == NULL)
    {
        return -ENOENT;
    }
    return 0;
}

int doggy_mkdir(const char *path, mode_t mode)
{
    // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
    // fputs(path, fp);
    // fputc('\n', fp);

    if (path_search(path) == NULL)
    {
        char *name = path2name(path);
        // fputs(name, fp);
        char *dir_path = (char *)malloc(sizeof(char) * strlen(path));
        if (strlen(path) - strlen(name) - 1 == 0)
        {
            dir_path = "/";
        }
        else
        {
            strncpy(dir_path, path, strlen(path) - strlen(name) - 1);
        }

        // fputs(dir_path, fp);
        // fclose(fp);
        doggy_file *prt_dir = path_search(dir_path);
        if (prt_dir == NULL)
        {
            return -ENOENT;
        }

        doggy_file *dir = (doggy_file *)malloc(sizeof(doggy_file));
        if (dir == NULL)
        {
            // perror("Cannot create directory: No space left.");
            return -ENOSPC;
        }

        dir->isdir = 1;
        dir->mode = __S_IFDIR | mode;
        dir->size = BLOCK_SIZE;
        int start_block = get_free_block_index();
        if (start_block == -1)
        {
            return -ENOSPC;
        }
        dir->start_block = start_block;
        dir->filename = (char *)malloc(sizeof(char) * PATH_LENGTH);
        dir->filepath = (char *)malloc(sizeof(char) * PATH_LENGTH);
        strcpy(dir->filepath, path);
        strcpy(dir->filename, name);
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
        int new_blk_idx = get_free_block_index();
        if (new_blk_idx < 0)
        {
            return -ENOSPC;
        }

        dfs.fat[blk_idx] = new_blk_idx;
        dfs.blocks[new_blk_idx].directory[0] = dir;
        free(name);

        return 0;
    }

    return -EEXIST;
}

// 重复path_search
int doggy_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    // open file if it exists. if not the create and open file

    // file doesn't exist
    if (path_search(path) == NULL)
    {

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

        doggy_file *dir = path_search(dir_path);
        // free(dir_path);
        if (dir == NULL)
        {
            return -ENOENT;
        }

        doggy_file *file = (doggy_file *)malloc(sizeof(doggy_file));

        if (file == NULL)
        {
            return -ENOSPC;
        }

        file->filepath = (char *)malloc(sizeof(char) * strlen(path));
        file->filename = (char *)malloc(sizeof(char) * strlen(name));

        strcpy(file->filepath, path);
        strcpy(file->filename, name);
        // free(name);
        file->isdir = 0;
        file->mode = mode;
        file->size = 0;
        file->start_block = -1;

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

int doggy_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    // return number of bytes read

    doggy_file *file = path_search(path);

    if (file == NULL)
    {
        return -EEXIST;
    }

    if (offset > file->size)
    {
        return -EPERM;
    }

    size = size > (file->size - offset) ? file->size - offset : size;

    int blk_idx = file->start_block;
    if (blk_idx == -1)
    {
        return -ENOSPC;
    }

    while (offset > BLOCK_SIZE)
    {
        blk_idx = dfs.fat[blk_idx];
        offset -= BLOCK_SIZE;
    }

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
    }

    while (size > offset)
    {
        blk_idx = dfs.fat[blk_idx];
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
    }
    // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
    // fputs("read:", fp);
    // fputs(buf, fp);
    // fputs("\n", fp);
    // fclose(fp);
    return offset;
}

int doggy_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
    // fputs("write:", fp);
    // fputs(buf, fp);
    // fputs("\n", fp);
    // fclose(fp);
    // return number of bytes written
    int off = offset;
    doggy_file *file = path_search(path);
    if (file == NULL)
    {
        return -EEXIST;
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

    while (offset > BLOCK_SIZE)
    {
        blk_idx = dfs.fat[blk_idx];
        offset -= BLOCK_SIZE;
    }

    if (offset)
    {
        doggy_block *blk = &(dfs.blocks[blk_idx]);
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("write:", fp);
        char *file_data = (char *)malloc(sizeof(char) * file->size);
        strncpy(file_data, blk->file_data, size);
        fputs(file_data, fp);
        fputs("\n", fp);
        fputs("size:", fp);
        fputc('0' + size, fp);
        fputs("offset:", fp);
        fputc('0' + offset, fp);
        fputc('\n', fp);
        fclose(fp);
        free(file_data);
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
    }

    while (size > offset)
    {
        if (dfs.fat[blk_idx] == -1)
        {
            int n_blk_idx = get_free_block_index();
            if (n_blk_idx == -1)
                return -ENOSPC;
            dfs.fat[blk_idx] = n_blk_idx;
        }
        blk_idx = dfs.fat[blk_idx];
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
    }
    // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
    // fputs("write:", fp);
    // fputs(buf, fp);
    // fputs("\n", fp);
    // fclose(fp);
    file->size = off + offset;
    return offset;
}

int doggy_truncate(const char *path, off_t offset)
{
    doggy_file *file = path_search(path);
    if (file == NULL)
        return -EEXIST;

    if (file->size == offset)
    {
        return 0;
    }

    int off = offset;
    int file_size = file->size;
    int blk_idx = file->start_block;
    while (file_size > BLOCK_SIZE && offset > BLOCK_SIZE)
    {
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("while\n", fp);
        fclose(fp);
        file_size -= BLOCK_SIZE;
        offset -= BLOCK_SIZE;
        blk_idx = dfs.fat[blk_idx];
    }
    doggy_block *blk = &(dfs.blocks[blk_idx]);
    if (file_size < BLOCK_SIZE && offset < BLOCK_SIZE)
    {
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("if\n", fp);
        fputc('0' + offset, fp);
        fputc('\n', fp);
        fputc('0' + file_size, fp);
        fclose(fp);
        if (offset > file_size)
        {
            memset(blk->file_data + file_size, '\0', offset - file_size);
            file->size = off;
        }
    }
    else if (file_size < BLOCK_SIZE)
    {
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("else if\n", fp);
        fclose(fp);
        memset(blk->file_data + file_size, '\0', BLOCK_SIZE - file_size);
        while (offset > BLOCK_SIZE)
        {
            int new_idx = get_free_block_index();
            if (new_idx == -1)
            {
                return -ENOSPC;
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
                return -ENOSPC;
            }
            dfs.fat[blk_idx] = new_idx;
            blk_idx = new_idx;
            blk = &(dfs.blocks[new_idx]);
            memset(blk->file_data, '\0', offset);
        }
    }
    else
    {
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("else\n", fp);
        fclose(fp);
        blk_idx = dfs.fat[blk_idx];
        while (file_size > 0)
        {
            int new_idx = dfs.fat[blk_idx];
            free_block(blk_idx);
            blk_idx = new_idx;
            file_size -= BLOCK_SIZE;
        }
    }

    file->size = off;
    return 0;
}

int doggy_unlink(const char *path)
{
    int status = 0;

    status = doggy_truncate(path, 0);
    if (status < 0)
    {
        return status;
    }
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

    int blk_idx = dir->start_block;
    while (blk_idx != -1)
    {
        doggy_block *block = &(dfs.blocks[blk_idx]);
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
            fputc('0' + i, fp);
            fclose(fp);
            if (block->directory[i] != NULL && strcmp(block->directory[i]->filepath, path) == 0)
            {
                free(block->directory[i]);
                block->directory[i] = NULL;
                return 0;
            }
        }
        blk_idx = dfs.fat[blk_idx];
    }

    return -EEXIST;
}

int doggy_rmdir(const char *path)
{
    if (strcmp(path, "/") == 0)
    {
        return -EACCES;
    }
    // FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
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
                    return -ENOTDIR;
                }
                block->directory[i] = NULL;
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
                                doggy_rmdir(del_blk.directory[i]->filepath);
                            }
                            else
                            {
                                doggy_unlink(del_blk.directory[i]->filename);
                            }
                        }
                    }
                    blk_idx_del = dfs.fat[blk_idx_del];
                }
                free(del_dir->filename);
                free(del_dir->filepath);
                free(del_dir);
                return 0;
            }
        }

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

int doggy_rename(const char *path, const char *newpath)
{
    // works only for files and not directories
    doggy_file *file = path_search(path);
    if (file == NULL)
        return -EEXIST;

    if (file->isdir == 0)
    {
        doggy_file *replaced = path_search(newpath);
        if (replaced != NULL && replaced->isdir == 0)
        {
            doggy_unlink(newpath);
        }
        strcpy(file->filepath, newpath);
        strcpy(file->filename, path2name(newpath));
    }
    return 0;
}

int doggy_opendir(const char *path, struct fuse_file_info *fu)
{
    if (strcmp("/", path) == 0)
    {
        return 0;
    }

    doggy_file *file = path_search(path);
    if (file != NULL)
    {
        last_dir = current_dir;
        current_dir = file;
        return 0;
    }

    return -ENOENT;
}

int doggy_flush(const char *path, struct fuse_file_info *f)
{
    return 0;
}

struct fuse_operations doggy_oper = {
    .getattr = doggy_getattr,   // done de
    .readdir = doggy_readdir,   // done de
    .opendir = doggy_opendir,   // done de
    .mkdir = doggy_mkdir,       // done de
    .rmdir = doggy_rmdir,       // done de
    .create = doggy_create,     // done de
    .truncate = doggy_truncate, // done de?
    .open = doggy_open,         // done de
    .read = doggy_read,         // done de
    .write = doggy_write,       // done de
    .unlink = doggy_unlink,     // done de
    .flush = doggy_flush,       // done de
    .destroy = doggy_destroy,
    .rename = doggy_rename // done
};

int main(int argc, char *argv[])
{

    doggy_init(argv[1]);

    return fuse_main(argc, argv, &doggy_oper, NULL);
}
