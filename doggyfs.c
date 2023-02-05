/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall doggy.c `pkg-config fuse --cflags --libs` -o doggy
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <fnmatch.h>
#include "doggy_structure.h"

#define MEMORI_SIZE 256 * 1024 * 1024
#define BLOCK_SIZE 1024
#define NAME_LENGTH 64
#define PATH_LENGTH 256

int NNODES;
int NBLOCKS;
int persistent = 0;
FILE *fp;

doggy_file *current_dir;
doggy_file *last_dir;
doggy_filesystem dfs;

char *path2name(const char *path)
{
    char *s, *ret;
    s = (char *)malloc(sizeof(char) * strlen(path));
    strcpy(s, path);
    for (size_t i = strlen(path); i >= 0; i--)
    {
        if (s[i] == '/')
        {
            ret = (char *)malloc(sizeof(char) * strlen(s + i + 1));
            strcpy(ret, s + i + 1);
            free(s);
            return ret;
        }
    }
}

doggy_file *path_search(const char *path)
{
    char *tmp_path = (char *)malloc(sizeof(char) * strlen(path));
    strcpy(tmp_path, path);

    doggy_file *cur = (doggy_file *)malloc(sizeof(doggy_file));

    char *p;
    p = strtok(tmp_path, "/");

    if (p == NULL) // 如果p为根目录直接返回就行
    {
        free(tmp_path);
        return dfs.root_dir;
    }

    if (strcmp(p, ".") == 0)
    {
        cur = current_dir;
        p = strtok(NULL, "/");
    }
    else if (strcmp(p, "..") == 0)
    {
        cur = last_dir;
        p = strtok(NULL, "/");
    }
    else
    {
        cur = dfs.root_dir;
    }

    int ndirectory_entry = BLOCK_SIZE / sizeof(doggy_file *);
    while (p != NULL)
    {
        int icur_block = cur->start_block;
        while (icur_block != -1)
        {
            doggy_block *cur_block = &(dfs.blocks[icur_block]);
            int finded = 0;
            int i;
            for (i = 0; i < ndirectory_entry; i++)
            {
                if (cur_block->directory[i] != NULL && strcmp(p, cur_block->directory[i]->filename) == 0)
                {
                    finded = 1;
                    cur = cur_block->directory[i];
                    break;
                }
            }
            if (finded)
            {
                p = strtok(NULL, "/");
                if (cur_block->directory[i]->isdir == 0 && p != NULL)
                {
                    return NULL;
                }

                break;
            }
            icur_block = dfs.fat[icur_block];
        }
        if (icur_block == -1)
        {
            free(tmp_path);
            return NULL;
        }
    }
    free(tmp_path);

    return cur;
}

int get_free_block_index()
{
    int i;
    for (i = 0; i < MEMORI_SIZE / BLOCK_SIZE; i++)
    {
        if (dfs.fat[i] == -2)
        {
            dfs.fat[i] = -1;
            return i;
        }
    }
    // perror("No more nodes left!!");
    return -ENOSPC;
}

int get_last_block_index(int idx)
{
    if (idx < 0)
    {
        return -1;
    }

    while (dfs.fat[idx] != -1)
        idx = dfs.fat[idx];
    return idx;
}

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
        FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
        fputs("no such file\n", fp);
        fclose(fp);
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
            FILE *fp = fopen("/home/fighter/linux/fuse/doggyfs/out.txt", "w");
            fputc('0' + i, fp);
            fclose(fp);
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
    doggy_file *cur;
    if ((cur = path_search(path)) == NULL)
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

    // cant read more than the size of file
    size = size > (file->size - offset) ? file->size - offset : size;
    int last_blk_idx = get_last_block_index(file->start_block);
    int blk_off = offset % BLOCK_SIZE;
    int blk_off_idx = offset / BLOCK_SIZE;

    // read partial
    doggy_block blk = dfs.blocks[blk_off_idx];
    if (last_blk_idx == blk_off_idx)
    {
        memcpy(buf, blk.file_data + blk_off, file->size - offset);
        return size;
    }

    memcpy(buf, blk.file_data + blk_off, BLOCK_SIZE - blk_off);
    off_t cpy_off = BLOCK_SIZE - blk_off;

    // read all other whole blocks
    while (cpy_off < size)
    {
        blk_off_idx = dfs.fat[blk_off_idx];
        blk = dfs.blocks[blk_off_idx];
        if (size > cpy_off + BLOCK_SIZE)
        {
            memcpy(buf + cpy_off, blk.file_data, BLOCK_SIZE);
            cpy_off += BLOCK_SIZE;
        }
        else
        {
            memcpy(buf + cpy_off, blk.file_data, size - cpy_off);
            cpy_off = size;
        }
    }

    return size;
}

int doggy_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    // return number of bytes written
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
        memcpy(blk->file_data, buf, offset);
    }

    while (size > offset)
    {
        if (dfs.fat[blk_idx] == -1)
        {
            int n_blk_idx = get_free_block_index();
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

    return offset;
}

int free_block(int index)
{
    dfs.fat[index] = -2;
    return 0;
}

int doggy_truncate(const char *path, off_t offset)
{

    doggy_file *file = path_search(path);
    if (file == NULL)
        return -EEXIST;

    int new_blocks = offset / BLOCK_SIZE;
    int old_blocks = file->size / BLOCK_SIZE;
    int old_last_block_start = file->size % BLOCK_SIZE;
    int new_last_block_end = offset % BLOCK_SIZE;

    if (offset >= file->size)
    {
        int last_block_index = get_last_block_index(file->start_block);

        if (new_blocks > old_blocks)
        {
            memset(dfs.blocks[last_block_index].file_data + old_last_block_start + 1, '\0', BLOCK_SIZE - old_last_block_start - 1);
            while (new_blocks != old_blocks)
            {
                int i = get_free_block_index();
                memset(dfs.blocks[i].file_data, '\0', BLOCK_SIZE);
                if (last_block_index == -1)
                {
                    file->start_block = i;
                }
                else
                {
                    dfs.fat[last_block_index] = i;
                }
                last_block_index = i;
                old_blocks++;
            }
        }
        else
        {
            memset(dfs.blocks[last_block_index].file_data + old_last_block_start + 1, '\0', new_last_block_end - old_last_block_start);
        }
        return 0;
    }

    int block_cnt = 1;
    int idx = file->start_block;
    while (block_cnt < new_blocks)
    {
        idx = dfs.fat[idx];
        ++block_cnt;
    }
    int *to_del;
    to_del = (int *)malloc(sizeof(int) * (old_blocks - block_cnt + 1));
    int top = -1;
    idx = dfs.fat[idx];
    while (idx != -1)
    {
        to_del[++top] = dfs.fat[idx];
        idx = dfs.fat[idx];
    }

    while (top >= 0)
    {
        free_block(to_del[top--]);
    }

    free(to_del);

    file->size = offset;
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
    char *path_ = (char *)malloc(sizeof(char) * (strlen(path) + 1));
    strcpy(path_, path);
    size_t idx;
    for (idx = strlen(path); path_[idx] != '/' && idx >= 0; idx--)
        ;
    path_[idx] = '\0';
    doggy_file *dir = path_search(path_);
    free(path_);

    int blk_idx = dir->start_block;
    while (blk_idx != -1)
    {
        doggy_block *block = &(dfs.blocks[blk_idx]);
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(doggy_file *); i++)
        {
            if (strcmp(block->directory[i]->filepath, path) == 0)
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
    .write = doggy_write,       // done
    .unlink = doggy_unlink,     // done de
    .flush = doggy_flush,       // done
    .destroy = doggy_destroy,
    .rename = doggy_rename // done
};

int main(int argc, char *argv[])
{

    doggy_init(argv[1]);

    return fuse_main(argc, argv, &doggy_oper, NULL);
}
