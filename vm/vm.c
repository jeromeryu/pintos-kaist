/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "userprog/process.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	// printf("vm_alloc_page_with_initializer\n");

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	// printf("alloc page with initializer %p %p\n", spt, upage);
	// printf("len spt %d\n", list_size(&spt->page_list));
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */

		struct page *page = (struct page *)malloc(sizeof(struct page));
		// page->va = upage;
		bool *init_func;
		if(VM_TYPE(type)==VM_ANON){
			// printf("ano initializer 1\n");
			init_func = anon_initializer;
		} else if(VM_TYPE(type)==VM_FILE){
			// printf("file initializer 1");
			init_func = file_backed_initializer;
		}

		uninit_new(page, upage, init, type, aux, init_func);
		page->writable = writable;
		return spt_insert_page(spt, page);

	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	// struct page *page = NULL;
	/* TODO: Fill this function. */
	// printf("find va %p\n", va);
	struct list_elem *e;
	// printf("spt_find_page");
	// printf("tid %d\n", thread_current()->tid);
	for(e = list_begin(&spt->page_list); e!= list_end(&spt->page_list); e = list_next(e)){
		// printf("hi1\n");
		// printf("%p\n", e);
		// printf("%p\n", e->next);
		struct page *p = list_entry(e, struct page, page_elem);
		// printf("%p\n", p->va);
		// printf("hi2\n");
		if(p->va==va){
			return p;
		}
	}

	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page ) {
	int succ = true;
	/* TODO: Fill this function. */

	// list_insert(&spt->page_list, &page->page_elem);
	// printf("insert %p\n", page);
	list_push_back(&spt->page_list, &page->page_elem);

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	void * p = palloc_get_page(PAL_USER);
	if(p == NULL){
		PANIC("TODO");
		// vm_evict_frame();
	} else {
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = p;
	}
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr ,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	// printf("try handle fault %p %p\n",pg_round_down(addr), addr );
	page = spt_find_page(spt, pg_round_down(addr)); 
	if(page == NULL){
		return false;
	}

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	// printf("claim pave %p %p \n", page->va, va);

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// printf("claim page va %p kva %p \n", page->va, frame->kva);
	bool done = pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

	if(!done){
		return false;
	}
	bool ret = swap_in (page, frame->kva);
	return ret;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	list_init(&spt->page_list);
	// printf("init tid %d\n", thread_current()->tid);

	thread_current()->spt_init = true;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst ,
		struct supplemental_page_table *src ) {
	// printf("copy\n");
	struct list_elem *e;
	for(e = list_begin(&src->page_list); e != list_end(&src->page_list); e = list_next(e)){
		struct page *page = list_entry(e, struct page, page_elem);
		// enum vm_type type = page_get_type(page);
		enum vm_type type = page->operations->type;
		// printf("isequal %d\n", type==type1);
		if(type==VM_UNINIT){
			// printf("uninit\n");
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			struct segment_info *src_info = (struct segment_info*)page->uninit.aux;
			info->file = src_info->file;
			info->ofs = src_info->ofs;
			info->upage = src_info->upage;
			info->read_bytes = src_info->read_bytes;
			info->zero_bytes = src_info->zero_bytes;
			info->writable = src_info->writable;
			info->page_read_bytes = src_info->page_read_bytes;
			info->page_zero_bytes = src_info->page_zero_bytes;
			void * aux = (void*) info;

			if(info->page_read_bytes == 0){
				if (!vm_alloc_page_with_initializer (VM_ANON, info->upage,
					info->writable, page->uninit.init, aux)){
					return false;
				}
			} else {
				if (!vm_alloc_page_with_initializer(VM_FILE, info->upage,
					info->writable, page->uninit.init, aux)){
					return false;
				}
			}
			

		} else if(type==VM_ANON){
			// printf("anon\n");

			bool success = vm_alloc_page(VM_ANON, page->va, true);
			if(!success){
				return false;
			}
			success = vm_claim_page(page->va);
			if(!success){
				palloc_free_page (page->va);
				return false;
			}

			struct page *newpage = spt_find_page(dst, page->va);

			memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);

		} else if(type==VM_FILE){
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			struct segment_info *src_info = (struct segment_info*)page->uninit.aux;
			// printf("file!!!!! %p %p\n", page->frame, src_info->upage);
			info->file = src_info->file;
			info->ofs = src_info->ofs;
			info->upage = src_info->upage;
			info->read_bytes = src_info->read_bytes;
			info->zero_bytes = src_info->zero_bytes;
			info->writable = src_info->writable;
			info->page_read_bytes = src_info->page_read_bytes;
			info->page_zero_bytes = src_info->page_zero_bytes;
			void * aux = (void*) info;
			struct vm_initializer *init = page->uninit.init;
			if (!vm_alloc_page_with_initializer(VM_FILE, info->upage,
				info->writable, init, aux)){

				return false;
			}
			// printf("here 222 %p %d %d\n", info->upage, info->writable, page->writable);
			struct page *newpage = spt_find_page(dst, page->va);
			// printf("checkpoint1\n");
			bool success = vm_claim_page(info->upage);
			// printf("checkpoint2\n");

			newpage->writable = page->writable;
			// printf("checkpoint3  %p, %p\n", newpage->frame, page->frame);

			memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);

			// printf("done file!!!!\n");
		}

	}
	return true;

}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	// for(e = list_begin(&src->page_list); e != list_end(&src->page_list); e = list_next(e)){
	// 	struct page *page = list_entry(e, struct page, page_elem);
	// printf("kill\n");
	// printf("kill tid %d\n", thread_current()->tid);
	struct list_elem *e;
	for(e = list_begin(&spt->page_list); e != list_end(&spt->page_list); e = list_remove(e)){
		struct page *page = list_entry(e, struct page, page_elem);
		// printf("destroy page %p\n", page->va);
		destroy(page);

		// free(page);
	}
}
