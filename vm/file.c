/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("file_backed_init\n");
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// printf("file_back_swap_in\n");
	struct file_page *file_page UNUSED = &page->file;
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct segment_info *info = (struct segment_info*)page->uninit.aux;
	if(info->type & VM_MARKER_0){
		do_munmap(page->va);
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// printf("mmap %p\n", addr);
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if(page!=NULL){
		return NULL;
	}

	if(pg_ofs(addr)!=0 || offset % PGSIZE != 0){
		return NULL;
	}

	uint32_t read_bytes = 0;
	uint32_t zero_bytes = 0;
	int full_pages = length;
	if(length % PGSIZE != 0){
		full_pages = (length / PGSIZE + 1) * PGSIZE;
	}
	int len_file = file_length(file);
	if(len_file < full_pages){
		read_bytes = len_file;
		zero_bytes = full_pages - len_file;
	} else {
		read_bytes = length;
		zero_bytes = full_pages - length;
	}

	// printf("full_page  %d\n", full_pages);
	// printf("file_size  %d\n", len_file);
	// printf("length     %d\n", length);
	// printf("read_bytes %d\n", read_bytes);
	// printf("zero_bytes %d\n", zero_bytes);

	bool res = load_segment(file, offset, addr, read_bytes, zero_bytes, writable, true);
	if(!res){
		// printf("mmap fail\n");
		return NULL;
	}
	// printf("mmap succ\n");
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// printf("dounmap %p\n", addr);
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if(page==NULL){
		// printf("return\n");
		return;
	}

	struct segment_info *info = (struct segment_info*)page->uninit.aux;
	if(pml4_is_dirty(thread_current()->pml4, addr)){
		// printf("is dirty\n");
		lock_acquire(&file_lock);
		file_write_at(info->file, addr, info->page_read_bytes, info->ofs);
		lock_release(&file_lock);
	}
	return;
}
