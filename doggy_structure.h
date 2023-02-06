#include <sys/types.h>

#define MEMORI_SIZE 256 * 1024 * 1024
#define BLOCK_SIZE 1024
#define NAME_LENGTH 64
#define PATH_LENGTH 256

// 单个文件结构
typedef struct
{
    /* data */
    int isdir; // 1 则为 目录
    mode_t mode;
    char *filename;
    char *filepath;
    int start_block;
    size_t size;
} doggy_file;

typedef doggy_file *directory_entry;

typedef union
{
    directory_entry directory[BLOCK_SIZE / sizeof(directory_entry)];
    char file_data[BLOCK_SIZE];
} doggy_block;

// 文件系统结构
typedef struct
{
    int fat[MEMORI_SIZE / BLOCK_SIZE];
    doggy_file *root_dir;
    // doggy_file *files;
    unsigned int files_n;
    doggy_block blocks[MEMORI_SIZE / BLOCK_SIZE];
} doggy_filesystem;
