#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
// #define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

struct buffer_cache_entry {
    bool dirty_bit;
    int clock_bit;
    disk_sector_t sector;
    uint8_t *buffer;
};

struct buffer_cache {
    uint32_t buffer_cache_size;
    struct buffer_cache_entry *buffer_array;
    //struct lock *lock;
};


/* Disk used for file system. */
extern struct disk *filesys_disk;
//struct lock *buffer_read_lock;
//struct lock *buffer_write_lock;
struct lock *buffer_lock;
struct lock *buffer_evict_lock;

void buffer_cache_read(disk_sector_t sector_idx, void *buffer);
unsigned int buffer_cache_evict(void);
bool buffer_cache_write(disk_sector_t sector_idx, void* buffer);
void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
bool filesys_chdir(char *name);
bool filesys_mkdir(char *name);
bool filesys_readdir(struct dir* dir, char* name);
bool filesys_isdir(struct dir *dir);
int filesys_inumber(struct file *file);

#endif /* filesys/filesys.h */
