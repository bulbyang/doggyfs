#include <sys/types.h>

#define MEMORI_SIZE 256 * 1024 * 1024
#define BLOCK_SIZE 1024
#define NAME_LENGTH 64
#define PATH_LENGTH 256

// 单个文件结构
typedef struct
{
    /* data */
    int isdir;       // 1 则为 目录
    mode_t mode;     // 读写权限
    char *filename;  // 文件名
    char *filepath;  // 文件路径
    int start_block; // 起始块号
    size_t size;     // 文件大小
} doggy_file;

typedef doggy_file *directory_entry; // 目录项

typedef union
{
    directory_entry directory[BLOCK_SIZE / sizeof(directory_entry)]; // 目录项数组
    char file_data[BLOCK_SIZE];                                      // 内存块
} doggy_block;

// 文件系统结构
typedef struct
{
    int fat[MEMORI_SIZE / BLOCK_SIZE];            // FAT表
    doggy_file *root_dir;                         // 根目录
    doggy_block blocks[MEMORI_SIZE / BLOCK_SIZE]; // 内存块
} doggy_filesystem;
