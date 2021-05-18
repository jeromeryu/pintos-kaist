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
	frame_list = (struct list *)malloc(sizeof(struct list));
	list_init(frame_list);
	lock_init(&vm_lock);
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
		page->writable_real = writable;
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
	struct frame *victim;
	struct list_elem *e;
	/*
	for(e = list_begin(frame_list); e != list_end(frame_list); e = list_next(e)){
		victim = list_entry(list_pop_front(frame_list), struct frame, frame_elem);
		if (!victim->page->writable){
			list_push_back(frame_list, &victim->frame_elem);
		}else{
			break;
		}
	}
	*/

	victim = list_entry(list_pop_front(frame_list), struct frame, frame_elem);
	// printf("pop frame kva %p\n", victim->kva);
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim  = vm_get_victim ();
	/*
	struct list_elem *e;
	struct frame * frame1;
	// struct supplemental_page_table *spt = &thread_current()->spt;
	for(e = list_begin(frame_list); e != list_end(frame_list); e = list_next(e)){
		frame1 = list_entry(e, struct frame, frame_elem);
		if (frame1->kva != NULL){
			if (frame1->kva == victim->kva){
				// list_remove(e);
				swap_out(frame1->page);
				list_remove(&frame1->frame_elem);
				printf("swap out1\n");
			}
		}
	}*/
	swap_out(victim->page);

	/* TODO: swap out the victim and return the evicted frame. */
	// struct page *page = victim->page;
	// printf("page address %p\n", page->frame->kva);
	// printf("before swap_out\n");
	// printf("page type : %d\n", page->operations->type);
	// swap_out (page);
	// printf("after swap out\n");

	return victim;
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
	// printf("physical memoty : %p\n", p);
	if(p == NULL){
		// PANIC("TODO");
		// printf("reach here\n");
		frame = vm_evict_frame();
	} else {
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = p;
	}
	frame->page = NULL;
	// list_push_back(&frame_list, &frame->frame_elem);


	// printf("reach get frame\n");
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	// printf("grow stack %p\n", pg_round_down(addr));
	vm_alloc_page(VM_ANON, pg_round_down(addr), true);
	vm_claim_page(pg_round_down(addr));
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f , void *addr ,
		bool user , bool write , bool not_present ) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	uintptr_t userstackpointer;

	// printf("user stack %p\n", USER_STACK);
	// printf("fault address %p\n", addr);
	// printf("current stack pointer %p\n", f->rsp);
	// printf("stack minimum1 %p\n", USER_STACK - 1024 * PGSIZE);
	// printf("stack minimum2 %p\n", USER_STACK - 256 * PGSIZE);
	// printf("onsyscall %d\n", thread_current()->on_syscall);
	// printf("reach handle fault\n");

	printf("try handle fault %p %p\n",pg_round_down(addr), addr );
	// printf("%d %d %d \n", user, write, not_present);
	page = spt_find_page(spt, pg_round_down(addr));
	// printf("page == nul %d\n", page==NULL);
	// printf("%d\n", page==NULL);
	
	// if(user && write && !not_present ){
	// 	printf("exit\n");
	// 	exit(-1);
	// }
	// printf("%d\n",user && write && !not_present);
	// printf("dd %d\n",(page==NULL) && user && write && !not_present);
	// if((page==NULL) && user && write && !not_present){
	// 	printf("exit\n");
	// 	exit(-1);
	// }

	// if(page != NULL && user && write && !not_present && page->writable ){
	if(page != NULL && user && write && !not_present && !page->writable_real){		
		exit(-1);
	}
	else if(page != NULL && user && write && !not_present && page->writable_real){
		//copy on write
		// printf("copy on write\n");

		// printf("va %p\n", page->va);
		// printf("type %d\n", page->operations->type);
		// printf("type addr %p\n", page->uninit);
		// printf("type now %d\n", page->uninit.type);

		
		lock_acquire(&evit_lock);
		struct frame *frame = vm_get_frame ();
		lock_release(&evit_lock);

		page->writable = page->writable_real;

		memcpy(frame->kva, page->frame->kva, PGSIZE);

		frame->page = page;
		page->frame = frame;
		bool done = pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

		if(!done){
			return false;
		}


		page->is_altered = true;


		return true;

	}

	if(page == NULL){
		if(USER_STACK >= addr && addr >= USER_STACK - 1024 * PGSIZE && write){
			if (thread_current()->on_syscall){
				// printf("onsyscall\n");
				userstackpointer = thread_current()->user_rsp;
			}else{
				// printf("else\n");
				userstackpointer = f->rsp;
			}
			if (addr >= USER_STACK - 256 * PGSIZE){
				vm_stack_growth(addr);
				return true;
			}else{
				// printf("exit\n");
				exit(-1);
			}
		}
		return false;
	}
	// printf("page not null\n");
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
	lock_acquire(&evit_lock);
	struct frame *frame = vm_get_frame ();
	lock_release(&evit_lock);

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// printf("claim page va %p kva %p %d\n", page->va, frame->kva, page->writable);
	bool done = pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
	// printf("pass this\n");

	if(!done){
		return false;
	}
	bool ret = swap_in (page, frame->kva);
	// printf("done %d\n", ret);
	// printf("reach do claim page\n");
	// printf("ret value %d\n", ret);
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
	// printf("copy@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	lock_acquire(&vm_lock);
	struct list_elem *e;
	struct file *file_lst[8] = {NULL};
	struct file *reopen_file_lst[8] = {NULL};
	for(e = list_begin(&src->page_list); e != list_end(&src->page_list); e = list_next(e)){
		struct page *page = list_entry(e, struct page, page_elem);
		// enum vm_type type = page_get_type(page);
		enum vm_type type = page->operations->type;
		// printf("isequal %d\n", type==type1);
		if(type==VM_UNINIT){
			// printf("uninit\n");
			
			struct segment_info *src_info = (struct segment_info*)page->uninit.aux;
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			// printf("%p\n", page->uninit.aux);
			info->file = src_info->file;
			info->ofs = src_info->ofs;
			info->upage = src_info->upage;
			info->read_bytes = src_info->read_bytes;
			info->zero_bytes = src_info->zero_bytes;
			info->writable = src_info->writable;
			info->page_read_bytes = src_info->page_read_bytes;
			info->page_zero_bytes = src_info->page_zero_bytes;
			info->type = src_info->type;
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
			// bool success = vm_alloc_page(VM_ANON, page->va, true);
			// if(!success){
			// 	return false;
			// }
			// success = vm_claim_page(page->va);
			// if(!success){
			// 	palloc_free_page (page->va);
			// 	return false;
			// }

			// struct page *newpage = spt_find_page(dst, page->va);

			// memcpy(newpage->frame->kva, page->frame->kva, PGSIZE);


			bool success = vm_alloc_page(VM_ANON, page->va, true);
			if(!success){
				// printf("return false 1\n");
				return false;
			}


			struct page *newpage = spt_find_page(dst, page->va);

			// printf("newpage %p\n", newpage->va);
			newpage->frame = page->frame;
			newpage->is_altered = false;


			// struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
			// frame->kva = page->frame->kva;
			// frame->page = newpage;
			// newpage->frame = frame;

			newpage->writable = false;
			newpage->writable_real = page->writable_real;
			page->writable = false;

			// printf("va %p %p\n", page->va, newpage->va);
			// printf("virtual %p %p\n", page->frame, newpage->frame);
			// printf("physical %p %p\n", vtop(page->frame->kva), vtop(newpage->frame->kva));
 

			// printf("copy type %d\n", newpage->uninit.type);
			// printf("copy type addr %p\n", page->uninit);
			bool done = pml4_set_page(thread_current()->pml4, newpage->va, newpage->frame->kva, newpage->writable);
			// printf("pass this\n");

			if(!done){
				// printf("return false\n");
				return false;
			}
			// bool ret = swap_in (newpage, newpage->frame->kva);

			newpage->operations = page->operations;


		} else if(type==VM_FILE){
			struct segment_info *info = (struct segment_info*)malloc(sizeof(struct segment_info));
			struct segment_info *src_info = (struct segment_info*)page->uninit.aux;
			// printf("file!!!!! %p %p\n", page->frame, src_info->file);
			bool is_reopened = false;
			for(int i=0; i<8; i++){
				if(file_lst[i]==src_info->file && reopen_file_lst[i]!=NULL){
					info->file = reopen_file_lst[i];
					is_reopened = true;
				} 
			}
			if(!is_reopened){
				// printf("n\n");
				for(int i=0; i<8; i++){
					// printf("file_lst %p\n", file_lst[i]);
					if(file_lst[i]==NULL){
						// printf("here\n");
						reopen_file_lst[i] = file_reopen(src_info->file);
						info->file = reopen_file_lst[i];
						file_lst[i] = info->file;
						break;
					}
				}
			}

			// printf("file!!!!! %p\n", info->file);

			info->ofs = src_info->ofs;
			info->upage = src_info->upage;
			info->read_bytes = src_info->read_bytes;
			info->zero_bytes = src_info->zero_bytes;
			info->writable = src_info->writable;
			info->page_read_bytes = src_info->page_read_bytes;
			info->page_zero_bytes = src_info->page_zero_bytes;
			info->type = src_info->type;
			void * aux = (void*) info;
			struct vm_initializer *init = page->uninit.init;
			// printf("init %p\n", init);
			if (!vm_alloc_page_with_initializer(VM_FILE, info->upage,
				info->writable, lazy_load_segment, aux)){

				return false;
			}


			struct page * newpage = spt_find_page(dst, page->va);
			newpage->is_altered = false;

			// struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

			// frame->kva = page->frame->kva;
			// newpage->frame = frame;
			// frame->page = newpage;

			newpage->frame = page->frame;



			// printf("writable %d\n", page->writable);
			// printf("writable real %d\n", page->writable_real);
			newpage->writable = false;
			newpage->writable_real = page->writable_real;
			page->writable = false;
			// printf("new writable %d\n", newpage->writable);
			// printf("new writable real %d\n", newpage->writable_real);

			// printf("va %p %p\n", page->va, newpage->va);
			// printf("virtual %p %p\n", page->frame, newpage->frame);
			// printf("physical %p %p\n", vtop(page->frame->kva), vtop(newpage->frame->kva));
 
			bool done = pml4_set_page(thread_current()->pml4, newpage->va, newpage->frame->kva, newpage->writable);

				// printf("pass this\n");

			// bool ret = swap_in (newpage, frame->kva);
			// bool ret = swap_in (newpage, newpage->frame->kva);
			newpage->operations = page->operations;

		}

	}

	lock_release(&vm_lock);

	// printf("done@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
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
		if(!page->is_altered){
			pml4_clear_page(thread_current()->pml4, page->va);
		}
		destroy(page);

		// free(page);
	}
}
