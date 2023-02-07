#include <stdlib.h>
#include <string.h>
#include "doggy_structure.h"

/**
 *将路径转化为文件名
 */
char *path2name(const char *path);

// 根据路径寻找文件
doggy_file *path_search(const char *path);

// 分配空闲块
int get_free_block_index();

// 寻找一个文件或目录的最后一个块
int get_last_block_index(int idx);

// 释放块
int free_block(int index);