#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include <string.h>
#include "filesys/file.h"
#include "filesys/filesys.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
int write(uintptr_t user_rsp, int fd, const void* buffer, unsigned size);
int read(uintptr_t user_rsp, int fd, void* buffer, unsigned size);
unsigned tell (int fd);
int seek(int fd, unsigned position);
tid_t fork(const char *name, struct intr_frame *if_);
int dup2(int oldfd, int newfd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&file_lock);
}

void halt(){
	power_off();
}

void exit(int status){
	struct thread * t = thread_current();
	// sema_down(&t->wait_sema);
	t->tf.R.rax = status;
	t->exit_status = status;
	// t->parent->child_exit_status = status;
	thread_exit();
}

int exec(const char *cmd_line){
	int i;
	char *fn_copy;
	if(cmd_line == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(cmd_line))){
		exit(-1);
	}

	if(!(pml4e_walk (thread_current()->pml4, cmd_line, 0))){
		exit(-1);
	}

	fn_copy = palloc_get_page(PAL_USER);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, cmd_line, PGSIZE);
	i = process_exec(fn_copy);
	if(i = -1){
		exit(-1);
	}
	return i;
}

bool create(const char *file, unsigned initial_size){
	bool a;
	if(file == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(file))){
		exit(-1);
	}
	
	if(!(pml4e_walk (thread_current()->pml4, file, 0))){
		exit(-1);
	}

	if(!strcmp(file, "")){
		return false;
	}

	lock_acquire(&file_lock);
	a = filesys_create(file, (off_t)initial_size);
	lock_release(&file_lock);
	return a;
}

bool remove(const char *file){
	bool a;
	if(file == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(file))){
		exit(-1);
	}
	
	if(!(pml4e_walk (thread_current()->pml4, file, 0))){
		exit(-1);
	}
	lock_acquire(&file_lock);
	a = filesys_remove(file);
	lock_release(&file_lock);
	return a;
}

int iself(char * buffer){
	if(buffer[0] == 127 && buffer[1] == 69 && buffer[2] == 76 && buffer[3] == 70){
		return 1;
	}
	return 0;
}

int open(const char *file){
	int fd = 0;
	struct thread *t = thread_current();
	struct file * tfile;
	char * buffer[4];
	if(file == NULL){
		exit(-1);
	}
	if(!(pml4e_walk (thread_current()->pml4, file, 0))){
		exit(-1);
	}

	if(!strcmp(file, "")){
		return -1;
	}
	if (!(is_user_vaddr(file))){
		exit(-1);
	}

	while(thread_current()->fd[fd] != NULL){
		fd++;
	}
	if(fd>=128){
		return -1;
	}
	lock_acquire(&file_lock);
	tfile = filesys_open(file);
	if (tfile == NULL){
		lock_release(&file_lock);
		return -1;
	}
	tfile = (struct file *)((int)tfile + 0x8000000000);
	thread_current()->fd[fd] = tfile;
	file_read_at(tfile, buffer, 4,0);
	if(iself(buffer)){
		file_deny_write(thread_current()->fd[fd]);
	}
	lock_release(&file_lock);

	if(thread_current()->fd[fd] == NULL){
		return -1;
	}

	return fd;
}

int filesize(int fd){
	if(thread_current()->fd[fd] != NULL){
		return (int)file_length(thread_current()->fd[fd]);
	}
	return -1;
}

int write(uintptr_t user_rsp, int fd, const void* buffer, unsigned size){
	if(fd<0 || fd >= NUM_MAX_FILE){
		return -1;
	}

	if(thread_current()->fd[fd] == (struct file*)1){
		return -1;
	}

	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}

	if(buffer == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(buffer))){
		exit(-1);
	}

	if(!(pml4e_walk (thread_current()->pml4, buffer, 0))){
		exit(-1);
	}

#ifdef VM
	// if(page==NULL || page->writable == false){
	// 	exit(-1);
	// }
	if(buffer < pg_round_down(user_rsp)){
		struct page *page = spt_find_page(&thread_current()->spt, pg_round_down(buffer));
		// printf("write %p\n", pg_round_down(buffer));
		// printf("%d\n", page==NULL);
		if(page == NULL ){
			// printf("write %p\n", pg_round_down(buffer));
			
			exit(-1);
		}
	}
#endif


	int res; 

	if(thread_current()->fd[fd] == (struct file*)2){
		putbuf(buffer, size);
	} else {
		lock_acquire(&file_lock);
		struct file *file = thread_current()->fd[fd];
		if(file->inode->data.is_directory){
			lock_release(&file_lock);
			exit(-1);	
		}
		res = file_write(thread_current()->fd[fd], buffer, size);
		lock_release(&file_lock);
		return res;
	}
	return size;
}

int read(uintptr_t user_rsp, int fd, void* buffer, unsigned size){
	if(fd<0 || fd >= NUM_MAX_FILE){
		return -1;
	}
	if(thread_current()->fd[fd] == (struct file*)2){
		return -1;
	}

	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}

	if(buffer == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(buffer))){
		exit(-1);
	}

	if(!(pml4e_walk (thread_current()->pml4, buffer, 0))){
		exit(-1);
	}

#ifdef VM
		// printf("read %p\n", pg_round_down(buffer));
		// printf("USER_STACK %p\n", USER_STACK);

	if(buffer < pg_round_down(user_rsp)){
		struct page *page = spt_find_page(&thread_current()->spt, pg_round_down(buffer));
		// printf("read %p\n", pg_round_down(buffer));
		if(page==NULL){
			// printf("read %p\n", pg_round_down(buffer));
			
			exit(-1);
		}
		// if(page->writable==false){
		// 	exit(-1);
		// }
		if(page->writable_real==false){
			exit(-1);
		}
	}
#endif

	int res; 

	int i = 0;
	if(thread_current()->fd[fd] == (struct file *)1){  
		while (i < size){
			*((uint8_t *)buffer + i) = input_getc();
			i++;
		}
	} else {
		lock_acquire(&file_lock);
		res = file_read(thread_current()->fd[fd], buffer, size);
		lock_release(&file_lock);
		return res;
	}
	return size;
}

unsigned tell (int fd){
	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}

	if(thread_current()->fd[fd] == (struct file *)1 || thread_current()->fd[fd] == (struct file *)2){  
		return -1;
	}
	
	if(fd >= NUM_MAX_FILE){
		return -1;
	}

	int res;
	lock_acquire(&file_lock);
	res = file_tell((thread_current()->fd)[0]);
	lock_release(&file_lock);
	return res;
}

int seek(int fd, unsigned position){
	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}

	if(thread_current()->fd[fd] == (struct file *)1 || thread_current()->fd[fd] == (struct file *)2){  
		return -1;
	}
	
	if(fd >= NUM_MAX_FILE){
		return -1;
	}

	lock_acquire(&file_lock);
	file_allow_write(thread_current()->fd[fd]);
	file_seek((thread_current()->fd)[fd] , position);
	lock_release(&file_lock);
	return 0;
}

void close(int fd){
	if(fd >= NUM_MAX_FILE){
		return;
	}

	lock_acquire(&file_lock);

	if(thread_current()->fd[fd] != (struct file *)1 && thread_current()->fd[fd] != (struct file *)2){
		if(thread_current()->fd[fd] != NULL){
			// file_allow_write(thread_current()->fd[fd]);
		}

		if(is_duped(fd)<0){
			file_close(thread_current()->fd[fd]);
		}

	}
	lock_release(&file_lock);
	thread_current()->fd[fd] = NULL;
}

tid_t fork(const char *name, struct intr_frame *if_){
	return process_fork(name, if_);
}

int dup2(int oldfd, int newfd){
	if(oldfd<0 || newfd<0){
		return -1;
	}
	if((thread_current()->fd)[oldfd]==NULL){
		return -1;
	}

	if(oldfd >= NUM_MAX_FILE || newfd >= NUM_MAX_FILE){
		return -1;
	}
	if(oldfd==newfd){
		return newfd;
	}

	lock_acquire(&file_lock);

	if(thread_current()->fd[newfd] != NULL){
		//duplicated file should not be closed -> file allow write causes problem
		if(is_duped(newfd) < 0){
			file_close(thread_current()->fd[newfd]);
		}
	}
	thread_current()->fd[newfd] = thread_current()->fd[oldfd];

	lock_release(&file_lock);

	return newfd;
}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	if(addr==NULL || addr==0){
		return NULL;
	}

	if(!is_user_vaddr(addr)){
		return NULL;
	}

	int len = length;
	if(len < 0){
		return NULL;
	}

	if(fd >= NUM_MAX_FILE || fd < 0){
		return NULL;
	}

	struct file *file = thread_current()->fd[fd];
	lock_acquire(&file_lock);


	if(thread_current()->fd[fd] == (struct file *)1 || thread_current()->fd[fd] == (struct file *)2){  
		lock_release(&file_lock);
		return NULL;
	}
	off_t size = file_length(file);

	if(size<=0 || length<=0){
		lock_release(&file_lock);
		return NULL;
	}

	struct file *file2 = file_reopen(file);
	void *page = do_mmap(addr, length, writable, file2, offset);
	lock_release(&file_lock);
	return page;

}

bool chdir(const char *dir){
	return filesys_chdir(dir);
}

bool mkdir(const char *dir){
	if(!strcmp(dir, "")){
		return false;
	}
	return filesys_mkdir(dir);
}

bool readdir(int fd, char *name){
	if((thread_current()->fd)[fd]==NULL){
		return false;
	}	
	if(fd >= NUM_MAX_FILE){
		return false;
	}

	struct dir *dir = (struct dir*)(thread_current()->fd[fd]);
	return filesys_readdir(dir, name);	
}

bool isdir(int fd){
	if((thread_current()->fd)[fd]==NULL){
		return false;
	}	
	if(fd >= NUM_MAX_FILE){
		return false;
	}
	struct dir *dir = (struct dir*)(thread_current()->fd[fd]);
	return filesys_isdir(dir);	
}

int inumber(int fd){
	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}	
	if(fd >= NUM_MAX_FILE){
		return -1;
	}
	struct file *file = (struct file*)(thread_current()->fd[fd]);
	return filesys_inumber(file);
}

int symlink(const char *target, const char *linkpath){
	return filesys_symlink(target, linkpath);
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	int res, fd;
	bool tf;
	tid_t tid;
	struct thread *t = thread_current();
	t->on_syscall = true;
	t->user_rsp = f->rsp;
	// printf("syscall rsp : %p\n", f->rsp);

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		f->R.rax = f->R.rdi;
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = 0;
		thread_current()->tf.R.rax = 0;

		res = fork(f->R.rdi, f);
		f->R.rax = res;
		break;
	case SYS_EXEC:
		res = exec((const char *)f->R.rdi);
		f->R.rax = res;
		break;
	case SYS_WAIT:
		tid = process_wait(f->R.rdi);
		f->R.rax = tid;
		break;
	case SYS_CREATE:
		tf = create((char *)(f->R.rdi), f->R.rsi);
		f->R.rax = tf;
		break;
	case SYS_REMOVE:
		tf = remove((const char*)f->R.rdi);
		f->R.rax = tf;
		break;
	case SYS_OPEN:
		fd = open((char *)(f->R.rdi));
		f->R.rax = fd;
		break;
	case SYS_FILESIZE:
		fd = filesize((int)(f->R.rdi));
		f->R.rax = fd;
		break;
	case SYS_READ:
		// printf("user rsp %p\n", t->user_rsp);
		res = read(t->user_rsp, (int)(f->R.rdi), (void* )f->R.rsi, f->R.rdx);
		thread_current()->tf.R.rax = res;
		f->R.rax = res;
		break;
	case SYS_WRITE:
		res = write(t->user_rsp, (int)(f->R.rdi), (void* )f->R.rsi, f->R.rdx);
		thread_current()->tf.R.rax = res;
		f->R.rax = res;
		break;
	case SYS_SEEK:
		res = seek((int)(f->R.rdi), f->R.rsi);
		if(res==-1){
			thread_current()->tf.R.rax = res;
		}	
		f->R.rax = res;
		break;
	case SYS_TELL:
		res = tell((int)(f->R.rdi));
		thread_current()->tf.R.rax = res;
		f->R.rax = res;
		break;
	case SYS_CLOSE:
		close((int)(f->R.rdi));
		break;
	case SYS_DUP2:
		res = dup2(f->R.rdi, f->R.rsi);
		f->R.rax = res;
		break;
	case SYS_MMAP:
		// printf("mmap\n");
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		// printf("munmap\n");
		do_munmap(f->R.rdi);
		break;
	case SYS_CHDIR:
		f->R.rax = chdir(f->R.rdi);
		break;
	case SYS_MKDIR:
		f->R.rax = mkdir(f->R.rdi);
		break;
	case SYS_READDIR:
		f->R.rax = readdir(f->R.rdi, f->R.rsi);
		break;
	case SYS_ISDIR:
		f->R.rax = isdir(f->R.rdi);
		break;
	case SYS_INUMBER:
		f->R.rax = inumber(f->R.rdi);
		break;
	case SYS_SYMLINK:
		f->R.rax = symlink(f->R.rdi, f->R.rsi);
		break;
	default:
		break;
	}
	t->on_syscall = false;
}
