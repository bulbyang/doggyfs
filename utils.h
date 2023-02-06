#include <stdlib.h>
#include <string.h>
#include "doggy_structure.h"

char *path2name(const char *path);

doggy_file *path_search(const char *path);

int get_free_block_index();
int get_last_block_index(int idx);