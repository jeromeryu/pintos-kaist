/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"
#include "userprog/process.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
struct bitmap *swap_slot_bitmap;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	disk_sector_t disk_max_size = disk_size(swap_disk);
	uint32_t swap_slot_num = disk_max_size / 8;
	// printf("swap_slot num : %d\n", swap_slot_num);
	swap_slot_bitmap = bitmap_create(swap_slot_num);
	// // bitmap_set(swap_slot_bitmap, 0, true);
	// bitmap_set(swap_slot_bitmap, 2525, true);
	// bitmap_set(swap_slot_bitmap, 7559, true);
	// bitmap_dump(swap_slot_bitmap);
	// size_t a = bitmap_scan(swap_slot_bitmap, 0, 1, true);
	// printf("first bit : %d\n", a);

}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("anon_init\n");
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->slot_num = -1;
	anon_page->kva = page->frame->kva;

	list_push_back(frame_list, &page->frame->frame_elem);

	// printf("reach anon initial\n");

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("anon_swap_in\n");
	struct anon_page *anon_page = &page->anon;
	size_t num = anon_page->slot_num;

	// page->frame->page = page;
	int i = 0;
	for (i=0;i<8;i++){
		disk_read(swap_disk, num*8 + i, (page->va) + i * DISK_SECTOR_SIZE);
	}

	bitmap_set(swap_slot_bitmap, num, false);

	anon_page->slot_num = -1;

	list_push_back(frame_list, &page->frame->frame_elem);
	// list_push_back(&frame_list, &page->frame->frame_elem);



	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t num = bitmap_scan(swap_slot_bitmap, 0, 1, false);
	anon_page -> slot_num = num;

	int i = 0;
	for (i=0;i<8;i++){
		disk_write(swap_disk, num*8 + i, (page->va)+ i * DISK_SECTOR_SIZE);
	}

	bitmap_set(swap_slot_bitmap, num, true);
	pml4_clear_page(thread_current()->pml4, page->va);


	return true;

}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
