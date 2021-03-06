#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/fat.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
#ifdef EFILESYS
	ASSERT (inode != NULL);
	if (pos < inode->data.length){
		cluster_t c = inode->data.start;
		// printf("byte to sector %d\n", inode->sector);
		// printf("cstart %d %d %d\n", c, pos, inode->data.length);
		while(pos >= DISK_SECTOR_SIZE){
			if(c==EOChain){
				// printf("cc %d\n", c);
				return -1;
			}
			c = fat_get(c);
			pos -= DISK_SECTOR_SIZE;
		}
		// printf("c %d\n", c);
		disk_sector_t s = cluster_to_sector(c);
		// printf("s %d\n", s);
		if(c == EOChain || c==0){
			return -1;
		}
		return s;
	} else {
		return -1;
	}
#else
	ASSERT (inode != NULL);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
#endif
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, int symlink) {
#ifdef EFILESYS
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	// printf("inode creat %d %d\n", sector, length);
	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		// printf("sectors %d\n", sectors);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->is_directory = 0;
		cluster_t cid = 0;
		if(sectors > 0){
			cid = fat_create_chain(cid);
			disk_inode->start = cid;
			// printf("start %d\n", cid);
			for(int j=1; j<sectors; j++){
				cid = fat_create_chain(cid);
				if(cid == 0){
					return false;
				}
			}
		}
		if (symlink){
			disk_inode->is_symlink = 1;
		}else{
			disk_inode->is_symlink = 0;
		}

		// printf("sector %d\n", sector);

		// printf("write on %d which start %d\n", sector, disk_inode->start);
		//lock_acquire(buffer_lock);
		//buffer_cache_write(sector, disk_inode);
		//lock_release(buffer_lock);
		disk_write (filesys_disk, sector, disk_inode);
		if (sectors > 0) {
			static char zeros[DISK_SECTOR_SIZE];
			size_t i;
			cluster_t c = disk_inode->start;
			for (i = 0; i < sectors; i++) {
				// printf("write on %d \n", cluster_to_sector(c));
				//lock_acquire(buffer_lock);
				//buffer_cache_write(cluster_to_sector(c), zeros);
				//lock_release(buffer_lock);
				disk_write (filesys_disk, cluster_to_sector(c), zeros); 
				c = fat_get(c);
			}
		}
		success = true; 
		free (disk_inode);
	}	
	return success;

#else
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
#endif
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	// printf("inode open\n");
	/* Check whether this inode is already open. */
	// printf("inode open %d\n", sector);
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			// printf("inode reopen %d\n", sector);
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	lock_acquire(buffer_lock);
	buffer_cache_read(inode->sector, &inode->data);
	lock_release(buffer_lock);
	// disk_read (filesys_disk, inode->sector, &inode->data);
	// printf("open %d %d\n", sector, inode->sector);
	// printf("inode data start %d\n", inode->data.start);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	// printf("inode reopen %d %d\n", inode->sector, inode->open_cnt);
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
#ifdef EFILESYS
	// printf("inode_close %d %d\n", inode->sector, inode->open_cnt);
	if (inode == NULL)
		return;
	
	lock_acquire(buffer_lock);
	buffer_cache_write(inode->sector, &inode->data);
	lock_release(buffer_lock);
	// disk_write(filesys_disk, inode->sector, &inode->data);

	// printf("inode close %d %d\n", inode->sector, inode->open_cnt - 1);
	if (--inode->open_cnt == 0) {
		list_remove (&inode->elem);
		if (inode->removed) {
			
			// fat_remove_chain(inode->sector, 0);
			// printf("remove chain\n", inode->sector);
			fat_remove_chain(inode->sector, 0);
			fat_remove_chain(inode->data.start, 0);
		}
		lock_acquire(buffer_lock);
		buffer_cache_write(inode->sector, &inode->data);
		lock_release(buffer_lock);
		// disk_write(filesys_disk, inode->sector, &inode->data);	
		free (inode); 
	}

#else
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length)); 
		}

		free (inode); 
	}
#endif
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	// printf("inode reat at %d %d %d\n", inode->sector, size, offset);

	// printf("data start %d\n", inode->data.start);

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		// printf("sector %d\n", sector_idx);
		if(sector_idx == -1 ) {
			break;
		}
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;
		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			lock_acquire(buffer_lock);
			buffer_cache_read(sector_idx, buffer+bytes_read);
			lock_release(buffer_lock);
			// disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			lock_acquire(buffer_lock);
			buffer_cache_read(sector_idx, bounce);
			lock_release(buffer_lock);
			// disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);
	// printf("read done %d\n", bytes_read);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		cluster_t c;
		while (sector_idx == -1){
			inode->data.length = offset+size;
			c = inode->data.start;
			// printf("hey %d\n", c);
			while ((fat_get(c) != EOChain) && (c != 0)){
				c = fat_get(c);
			}
			c = fat_create_chain(c);
			// printf("c %d %d\n",inode->data.start, c);
			if(c == 0){
				// printf("chain is 0\n");
				break;
			}

			if(inode->data.start == 0){
				inode->data.start = c;
			}
			sector_idx = byte_to_sector(inode, offset);
		}
		if(c == 0){
			break;
		}

		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			lock_acquire(buffer_lock);
			buffer_cache_write(sector_idx, buffer+bytes_written);
			lock_release(buffer_lock);
			// disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left){
				lock_acquire(buffer_lock);
				buffer_cache_read(sector_idx, bounce);
				lock_release(buffer_lock);
				// disk_read (filesys_disk, sector_idx, bounce);
			}
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			lock_acquire(buffer_lock);
			buffer_cache_write(sector_idx, bounce);
			lock_release(buffer_lock);
			// disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	// printf("write done %d\n", bytes_written);
	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	// printf("inode_length\n");
	return inode->data.length;
}
