#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"
#include "filesys/inode.h"
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

	struct dir *dir = dir_open_root();
	// printf("dir inode sector %d\n", dir->inode->sector);
	dir->inode->data.is_directory = 1;
	dir_add(dir, ".", dir->inode->sector);
	dir_add(dir, "..", dir->inode->sector);
	dir_close(dir);

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
	// printf("create %s\n", name);
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {

		// printf("open inode sector %d\n",  thread_current()->cur_sector);
		// printf("check %d\n",sector_to_cluster( cluster_to_sector( thread_current()->cwd_cluster)));
		dir = dir_open(inode_open(  thread_current()->cur_sector));
	}
	// printf("filesys_create1 %d %d\n", dir->inode->sector,dir->inode->open_cnt);	

	// printf("file create name %s\n", name);
	// printf("filesys_create !!!!! %d \n", thread_current()->cur_sector);
	// printf("dir null %d\n", dir != NULL);
	// bool ic  = inode_create (inode_sector, initial_size);
	// printf("%d\n", ic);
	// bool da = dir_add (dir, name, inode_sector);
	// printf("%d %d\n", ic, da);
	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	struct inode *i;
	while(ret){
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			break;
		}
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			return false;
		}
		// printf("filesys_create2 %d %d\n", i->sector, i->open_cnt);	
		// disk_sector_t s = 0;
		// s = i->sector;
		// struct inode *inode_new = inode_open(s);
		// if (inode_new->data.is_symlink){
		// 	char *target = malloc(20 * sizeof(char));
		// 	inode_read_at(inode_new, target, 20, 0);
		// 	dir = filesys_open(target);
		// 	ret = ret_after;
		// 	continue;
		// }

		if (i->data.is_symlink){
			char *target = malloc(20 * sizeof(char));
			inode_read_at(i, target, 20, 0);
			dir = filesys_open(target);
			ret = ret_after;
			continue;
		}

		// printf("create %s\n", ret);
		dir_close(dir);
		dir = dir_open(i);
		ret = ret_after;
	}
	// printf("filesys_create2 %d %d\n", dir->inode->sector,dir->inode->open_cnt);	

	// printf("%s\n", ret);

	disk_sector_t inode_sector = 0;
	cluster_t clst = fat_create_chain(0);
	inode_sector = cluster_to_sector(clst);
	// printf("filesys_create3 %d %d\n", dir->inode->sector,dir->inode->open_cnt);	

	// printf("clst %d\n", clst);
	// printf("inode_sector %d\n", inode_sector);
	// printf("ret %s\n", ret);
	if(dir_lookup(dir, ret, &i)){
		// printf("dir lookup  true\n");
		inode_close(i);
		dir_close(dir);
		free(name_copy);
		return false;
	}
	// printf("filesys_create4 %d %d\n", dir->inode->sector,dir->inode->open_cnt);	

	bool success = (dir != NULL
			&& inode_create (inode_sector, initial_size,0)
			&& dir_add (dir, ret, inode_sector));
	// bool success = (dir!=NULL) && ic && da;
	
	// printf("@@@@@@success %d\n", success);

	if (!success && inode_sector != 0){
		// printf("fail\n");
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	}
	
	// printf("filesys_create5 %d %d\n", dir->inode->sector,dir->inode->open_cnt);	
	
	dir_close (dir);
	// printf("filesys_create2 %d %d\n", i->sector,i->open_cnt);	
	// printf("create %d\n", success);
	free(name_copy);
	return success;
#else
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size,0)
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
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {
		// dir = dir_open(inode_open( cluster_to_sector( thread_current()->cwd_cluster)));
		dir = dir_open(inode_open(  thread_current()->cur_sector));
	}
	// printf("filesys_open %s\n", name);

	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	struct inode *i;
	while(ret){
		// printf("ret1\n");
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			// printf("break\n");
			break;
		}

		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			// printf("null1\n");
			return NULL;
		}
		// disk_sector_t s = 0;
		// s = i->sector;
		// printf("open %d %d\n", i->sector, i->open_cnt);
		// struct inode *inode_new = inode_open(s);
		// printf("open %d %d\n", inode_new->sector, inode_new->open_cnt);
		// if (inode_new->data.is_symlink){
		// 	char *target = malloc(20 * sizeof(char));
		// 	inode_read_at(inode_new, target, 20, 0);
		// 	dir = filesys_open(target);
		// 	ret = ret_after;
		// 	continue;
		// }

		if (i->data.is_symlink){
			char *target = malloc(20 * sizeof(char));
			inode_read_at(i, target, 20, 0);
			dir = filesys_open(target);
			ret = ret_after;
			continue;
		}

		dir_close(dir);
		dir = dir_open(i);
		ret = ret_after;
		// printf("ret %s\n", ret);
	}
	if(ret==NULL){
		//directory
		// printf("return dir\n");
		// printf("return null %d %d\n", dir->inode->sector, dir->inode->open_cnt);
		// dir_close(dir);
		free(name_copy);
		return dir;
	} else {
		// printf("dir open %d %d %s\n", dir->inode->sector, dir->inode->open_cnt, ret);
		disk_sector_t s = 0;
		if (dir != NULL){

			// printf("ret is %s\n", ret);
			if(!dir_lookup (dir, ret, &i)){
				dir_close(dir);
				free(name_copy);
				// printf("null2\n");
				return NULL;
			}
			s = i->sector;
			inode_close(i);
			// printf("i close\n");
		}
		// printf("i %d %d\n", i->sector, i->open_cnt);
		struct inode *inode_new = inode_open(s);
		if(inode_new->data.is_symlink){
			char *target = malloc(20 * sizeof(char));
			inode_read_at(inode_new, target, 20, 0);
			return filesys_open(target);
		}
		// printf("inew %d %d\n", inode_new->sector, inode_new->open_cnt);
		if(inode_new->data.is_directory == 1){
			struct dir *open_dir = dir_open(inode_new);
			dir_close(dir);
			free(name_copy);
			// printf("open dir %d %d\n", open_dir->inode->sector, open_dir->inode->open_cnt);
			return open_dir;
		} else {
			dir_close (dir);
			free(name_copy);
			return file_open (i);
		}
	}
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
#ifdef EFILESYS
	// printf("remove %s\n", name);
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {
		dir = dir_open(inode_open(thread_current()->cur_sector));
	}
	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);
	
	// printf("remove 1 %d, %d\n", dir->inode->sector, dir->inode->open_cnt);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	// struct inode *i;
	struct inode *i = dir->inode;
	while(ret){
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			break;
		}
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			return false;
		}
		// printf("remove11 %d %d\n", i->sector, i->open_cnt);
		dir_close(dir);
		dir = dir_open(i);
		// printf("remove12 %d %d\n", i->sector, i->open_cnt);
		ret = ret_after;
	}

	// printf("remove 2 %d, %d\n", dir->inode->sector, dir->inode->open_cnt);
	if(ret==NULL){
		// dir_remove()
		// return dir;
		// TODO
		free(name_copy);
		// printf("false null\n");
		return false;
	} else {
		// printf("file remove %s %d %d\n", ret, i->sector, i->open_cnt);
		disk_sector_t s = 0;
		bool success = false;
		if (dir != NULL){
			bool a = dir_lookup (dir, ret, &i);
			s = i->sector;
			inode_close(i);
		}
		

		struct inode *inode_new = inode_open(s);

		// printf("file remove after %s %d %d\n", ret, inode_new->sector, inode_new->open_cnt);

		if(inode_new->data.is_directory == 1){
			struct dir *remove_dir = dir_open(inode_new);
			char ch[NAME_MAX + 1];
			if(dir_readdir(remove_dir, ch)){
				//not empty => false
				dir_close(remove_dir);
				dir_close(dir);
				free(name_copy);
				// printf("false not empty\n");

				return false;
			}
			
			// printf("remove %d\n", remove_dir->inode->sector);
			// printf("%d\n", thread_current()->cur_sector == remove_dir->inode->sector);
			// printf("%d\n", remove_dir->inode->open_cnt );
			if(thread_current()->cur_sector == remove_dir->inode->sector || remove_dir->inode->open_cnt > 1){
				dir_close(remove_dir);
				dir_close(dir);
				free(name_copy);
				// printf("false open\n");
				return false;
			}
			dir_close(remove_dir);
			success = dir_remove(dir, ret);

		} else {
			success = dir_remove(dir, ret);
			// printf("remove file dir %d %d\n", dir->inode->sector, dir->inode->open_cnt);
		}

		dir_close (dir);
		free(name_copy);
		return success;
	}


#else
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
#endif
}

bool filesys_chdir( char *name){
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {
		dir = dir_open(inode_open(thread_current()->cur_sector));
	}

	// printf("filesys_ch@@@ %d %d\n", dir->inode->sector, dir->inode->open_cnt);

	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	// struct inode *i;
	struct inode *i = dir->inode;
	// printf("check1\n");
	while(ret){
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			break;
		}
		// printf("check2\n");
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			// printf("false1\n");
			return false;
		}
		// printf("check3\n");
		dir_close(dir);
		// printf("inode %d\n", i->sector);
		dir = dir_open(i);
		ret = ret_after;
	}
	// printf("changedir sec %d\n", i->sector);
	// printf("check4 %s\n", ret);
	if(ret != NULL && !dir_lookup(dir, ret, &i)){
		// printf("name %s\n", name);
		// printf("ret %s\n", ret);
		// printf("%d\n", dir->inode->sector);
		dir_close(dir);
		free(name_copy);
		// printf("false2\n");
		return false;
	}
	// printf("check 5\n");
	thread_current()->cur_sector = i->sector;
	// printf("filesys_chdir %d %d %s\n", dir->inode->sector, dir->inode->open_cnt, ret);
	if(ret!=NULL){
		inode_close(i);
	}
	dir_close(dir);
	free(name_copy);
	return true;

}

bool filesys_mkdir(char *name){
	// printf("mkdir start\n");
	struct dir *dir;
	if(name[0] == '/'){
		dir = dir_open_root();
	} else {
		dir = dir_open(inode_open(thread_current()->cur_sector));
	}
	// printf("filesys_mkdir! %d %d\n", dir->inode->sector, dir->inode->open_cnt);

	char * next;
	char *name_copy = malloc(strlen(name) + 1);
	strlcpy(name_copy, name, strlen(name) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	struct inode *i;
	while(ret){
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			break;
		}
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			return false;
		}
		// printf("filesys_mkdir0 %d %d\n", dir->inode->sector, dir->inode->open_cnt);
		dir_close(dir);
		// printf("filesys_mkdir1 %d %d\n", dir->inode->sector, dir->inode->open_cnt);
		dir = dir_open(i);
		// printf("filesys_mkdir2 %d %d\n", dir->inode->sector, dir->inode->open_cnt);
		ret = ret_after;
	}
	// printf("filesys_mkdir3 %d %d\n", dir->inode->sector, dir->inode->open_cnt);

	// printf("clst %d\n", clst);
	// printf("inode_sector %d\n", inode_sector);
	// printf("ret %s\n", ret);
	if(dir_lookup(dir, ret, &i)){
		// printf("dir lookup  true\n");
		inode_close(i);
		dir_close(dir);
		free(name_copy);
		return false;
	}

	disk_sector_t inode_sector = 0;
	cluster_t clst = fat_create_chain(0);
	inode_sector = cluster_to_sector(clst);
	if(clst==0){
		inode_close(i);
		dir_close(dir);
		free(name_copy);
		return false;
	}

	bool success = (dir != NULL
			&& dir_create (inode_sector, 16)
			&& dir_add (dir, ret, inode_sector));
	// bool success = (dir!=NULL) && ic && da;

	// printf("success %d\n", success);
	if (!success && inode_sector != 0){
		// printf("fail\n");
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
		dir_close(dir);
		inode_close(i);
		free(name_copy);
		return success;
	}

	struct dir *created_dir = dir_open(inode_open(inode_sector));
	// printf("create .. %s %d %d\n", name, inode_sector, dir->inode->sector);
	bool a = dir_add(created_dir, ".", inode_sector);
	bool b = dir_add(created_dir, "..", dir->inode->sector);
	// printf("%d %d\n", a, b);
	if(!a || !b){
		dir_close(created_dir);
		// fat_remove_chain(sector_to_cluster(inode_sector), 0);
		dir_remove(dir, ret);
		inode_close(i);
		dir_close(dir);
		free(name_copy);
		return false;
	}

	// printf("filesys_mkdir %d %d\n", created_dir->inode->sector, created_dir->inode->open_cnt);
	// printf("filesys_mkdir4 %d %d\n", dir->inode->sector, dir->inode->open_cnt);
	dir_close(created_dir);
	dir_close (dir);
	// printf("makedir %s %d\n", name, success);
	free(name_copy);
	// printf("mkdir done\n");
	return success;
}

bool filesys_readdir(struct dir* dir, char* name){
	if(dir->inode->data.is_directory == 0){
		return false;
	}
	// printf("readdir %d %d\n", dir->inode->sector, dir->inode->open_cnt);
	return dir_readdir(dir, name);
}

bool filesys_isdir(struct dir *dir){
	return dir->inode->data.is_directory == 1 ? true : false;
}

int filesys_inumber(struct file *file){
	return file->inode->sector;
}

int filesys_symlink(const char *target, const char *linkpath){

	struct dir *dir;
	if(linkpath[0] == '/'){
		dir = dir_open_root();
	} else {
		dir = dir_open(inode_open(thread_current()->cur_sector));
	}

	char *next;
	char *name_copy = malloc(strlen(linkpath) + 1);
	strlcpy(name_copy, linkpath, strlen(linkpath) + 1);

	char *ret_after;
	char *ret = strtok_r(name_copy, "/", &next);
	struct inode *i;
	while(ret){
		ret_after = strtok_r(NULL, "/", &next);
		if(ret_after == NULL){
			break;
		}
		if(!dir_lookup(dir, ret, &i)){
			//no subdirectory
			dir_close(dir);
			free(name_copy);
			return -1;
		}
		dir_close(dir);
		dir = dir_open(i);
		ret = ret_after;
	}

	disk_sector_t inode_sector = 0;
	cluster_t clst = fat_create_chain(0);
	inode_sector = cluster_to_sector(clst);
	if(clst==0){
		inode_close(i);
		dir_close(dir);
		free(name_copy);
		return -1;
	}
	

	bool success = (dir != NULL
			&& inode_create(inode_sector, strlen(target) + 1, 1)
			&& dir_add (dir, ret, inode_sector));
	
	dir_lookup(dir, ret, &i);
	inode_write_at(i, target, strlen(target)+1, 0);
	inode_close(i);

	if (!success && inode_sector != 0){
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	}

	dir_close (dir);
	free(name_copy);
	return success ? 0 : -1;


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
