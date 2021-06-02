#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	// printf("fat_init\n");
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	// printf("fat_open %d\n", fat_fs->fat_length);
	// printf("%p\n", fat_fs->fat);
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	// printf("load\n");
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");
	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
	// printf("byte read %d\n", bytes_read);
}

void
fat_close (void) {
	// Write FAT boot sector
	// printf("fat close\n");
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	// printf("close %d %d\n", fat_fs->bs.fat_sectors, fat_size_in_bytes);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
	// printf("close bytes writtedn %d\n", bytes_wrote);
}

void
fat_create (void) {
	// printf("fat_create\n");
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);

	// for(int i=fat_fs->data_start; i<=fat_fs->last_clst; i++){
	// 	fat_put(i, 0);
	// }	
	// fat_put(fat_fs->last_clst, EOChain);


	uint32_t i = 0;
	for (i ; i < fat_fs->last_clst ; i++)
	{
		fat_fs->fat[i] = 0;
	}
	fat_put(0, EOChain);
	fat_put(1, EOChain);
	fat_put(fat_fs->bs.fat_start+fat_fs->bs.fat_sectors-1, EOChain);
	fat_put(fat_fs->last_clst, EOChain);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 2,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	// fat_fs->fat_length = disk_size(filesys_disk) / 2 - 2;
	// fat_fs->data_start = fat_fs->fat_length + 1;
	// fat_fs->last_clst = 2;
	// fat_fs->fat = NULL;
	fat_fs->fat = NULL;
    fat_fs->fat_length = fat_fs->bs.total_sectors;
    fat_fs->data_start = fat_fs->bs.fat_start+fat_fs->bs.fat_sectors;
    fat_fs->last_clst = fat_fs->bs.total_sectors-1;
	// printf("fat_length %d \n", fat_fs->fat_length);
	// printf("data_start %d \n", fat_fs->data_start);
	// printf("last_clst  %d \n", fat_fs->last_clst);
	
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	// if(fat_fs->last_clst >= fat_fs->fat_length){
	// 	return 0;
	// }

	// cluster_t cid = fat_fs->last_clst;
	// if(clst == 0){
	// 	fat_put(cid, EOChain);
	// } else {
	// 	fat_put(clst, cid);
	// 	fat_put(cid, EOChain);
	// }
	// fat_fs->last_clst = 2;
	// while((fat_get(fat_fs->last_clst) != 0) && (fat_fs->last_clst < fat_fs->fat_length)){
	// 	fat_fs->last_clst++;
	// }

	// return cid;
	// printf("create chain %d\n", clst);

	cluster_t free_cluster = fat_fs->data_start;
	while(fat_get(free_cluster) != 0 && free_cluster <= fat_fs->last_clst ){
		// printf("fat_get %d %d\n", free_cluster, fat_get(free_cluster));
		free_cluster++;
	}

	if(free_cluster > fat_fs->last_clst){
		return 0;
	}
	
	if(clst==0){
		fat_put(free_cluster, EOChain);
	} else {
		fat_put(clst, free_cluster);
		fat_put(free_cluster, EOChain);
	}

	// printf("link to %d\n", free_cluster);
	return free_cluster;

}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	// if(pclst != 0){
	// 	fat_put(pclst, EOChain);
	// }

	// cluster_t nclst = clst;
	// while(nclst != EOChain){
	// 	cluster_t tmp_clst = fat_get(nclst);
	// 	fat_put(nclst, EOChain);
	// 	nclst = tmp_clst;
	// }
	// fat_fs->last_clst = 2;
	// while((fat_get(fat_fs->last_clst) != 0) && (fat_fs->last_clst < fat_fs->fat_length)){
	// 	fat_fs->last_clst++;
	// }

	// printf("remove chain %d %d\n", clst, pclst);
	if(pclst != 0){
		fat_put(pclst, EOChain);
	}

	cluster_t nclst = clst;
	while(nclst != EOChain){
		cluster_t tmp_clst = fat_get(nclst);
		fat_put(nclst, EOChain);
		nclst = tmp_clst;
	}

}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	// *(fat_fs->fat + clst) = val;
	fat_fs->fat[clst] = val;
}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	// return *(fat_fs->fat + clst);
	return fat_fs->fat[clst];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	// return fat_fs->fat_length + clst + 1;
	return clst;
}

cluster_t
sector_to_cluster (disk_sector_t sec) {
	/* TODO: Your code goes here. */
	// return sec - fat_fs->fat_length - 1;
	return sec;
}
