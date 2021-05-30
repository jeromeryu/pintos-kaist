#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
#ifdef EFILESYS
	// struct dir *dir = dir_open_root ();
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {
		dir = dir_open(inode_open( cluster_to_sector( thread_current()->cwd_cluster)));
	}

	printf("filesys_create !!!!! %d \n", thread_current()->cwd_cluster);
	// printf("dir null %d\n", dir != NULL);
	// bool ic  = inode_create (inode_sector, initial_size);
	// printf("%d\n", ic);
	// bool da = dir_add (dir, name, inode_sector);
	// printf("%d %d\n", ic, da);
	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, " ", &next);
	struct inode *i;
	while(ret){
		ret_after = strtok_r(NULL, " ", &next);
		if(ret_after == NULL){
			break;
		}
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			return false;
		}
		dir_close(dir);
		dir = dir_open(i);
		ret = ret_after;
	}

	// char *ret;
	// ret = strtok_r(full_file_name, " ", &next);
	// while(ret){
	// 	arr[idx] = ret;
	// 	ret = strtok_r(NULL, " ", &next);
	// 	idx ++;
	// }

	disk_sector_t inode_sector = 0;
	cluster_t clst = fat_create_chain(0);
	inode_sector = cluster_to_sector(clst);

	printf("clst %d\n", clst);
	printf("inode_sector %d\n", inode_sector);

	if(dir_lookup(dir, ret, &i)){
		printf("dir lookup  true\n");
		inode_close(i);
		free(name_copy);
		return false;
	}

	bool success = (dir != NULL
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, ret, inode_sector));
	// bool success = (dir!=NULL) && ic && da;
	
	printf("@@@@@@success %d\n", success);

	if (!success && inode_sector != 0){
		printf("fail\n");
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	}
	dir_close (dir);
	printf("create %d\n", success);
	free(name_copy);

	return success;
#else
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);
	return success;
#endif
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
#ifdef EFILESYS
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	printf("%s\n", name);
	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
#else
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
#endif

}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
