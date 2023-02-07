#include "utils.h"

extern doggy_filesystem dfs;

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
/// @brief 根据路径查找文件或目录
/// @param path
/// @return doggy_file *
doggy_file *path_search(const char *path)
{
    // 新建一个字符串并将path复制到原有的tmp_path上，方便使用strtok
    char *tmp_path = (char *)malloc(sizeof(char) * strlen(path));
    strcpy(tmp_path, path);

    doggy_file *cur = dfs.root_dir;

    char *p;
    p = strtok(tmp_path, "/");

    if (p == NULL) // 如果p为根目录直接返回就行
    {
        free(tmp_path);
        return dfs.root_dir;
    }

    // 利用strtok拆分path，同时遍历每一级目录寻找目标文件
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

/// @brief 分配空闲块
/// @return int 空闲块号
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
    return -1;
}

/// @brief 返回快号idx所属文件或目录的最后一个块号
/// @param idx
/// @return int 块号
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

/// @brief 释放index块
/// @param index
/// @return
int free_block(int index)
{
    dfs.fat[index] = -2;
    return 0;
}