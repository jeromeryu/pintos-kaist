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

void check_addr(void *addr);
void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
int write(int fd, const void* buffer, unsigned size);
int read(int fd, void* buffer, unsigned size);
unsigned tell (int fd);
int seek(int fd, unsigned position);
tid_t fork(const char *name, struct intr_frame *if_);


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

void check_addr(void *addr){

}

void halt(){
	power_off();
}

void exit(int status){
	struct thread * t = thread_current();
	t->tf.R.rax = status;
	t->exit_status = status;
	// printf("%s: exit(%d)\n", t->name, status);
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

	lock_acquire(&file_lock);
	a = filesys_create(file, (off_t)initial_size);
	lock_release(&file_lock);
	return a;
}

int open(const char *file){
	int fd = 0;
	struct thread *t = thread_current();
	struct file * tfile;
	if(file == NULL){
		exit(-1);
	}

	if (!(is_user_vaddr(file))){
		exit(-1);
	}

	if(!(pml4e_walk (thread_current()->pml4, file, 0))){
		exit(-1);
	}

	while(thread_current()->fd[fd] != NULL){
		fd++;
	}
	lock_acquire(&file_lock);
	tfile = filesys_open(file);
	if (tfile == NULL){
		return -1;
	}
	tfile = (struct file *)((int)tfile + 0x8000000000);
	thread_current()->fd[fd] = tfile;
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

int write(int fd, const void* buffer, unsigned size){
	if (!(is_user_vaddr(buffer))){
		exit(-1);
	}

	if(!(pml4e_walk (thread_current()->pml4, buffer, 0))){
		exit(-1);
	}

	if(fd<=0 || fd >= 128){
		return -1;
	}

	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}
	
	int res; 

	if(fd==1){
		putbuf(buffer, size);
	} else {
		if(thread_current()->fd[fd]==NULL){
			return -1;
		}
		lock_acquire(&file_lock);
		res = file_write(thread_current()->fd[fd], buffer, size);
		lock_release(&file_lock);
		return res;
	}
	return size;
}

int read(int fd, void* buffer, unsigned size){

	if(fd<0 || fd >= 128 || fd==1){
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

	if((thread_current()->fd)[fd]==NULL){
		exit(-1);
	}
	int res; 

	int i = 0;
	if(fd==0){
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

	if(fd<=1 || fd >= 128){
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

	if(fd<=1 || fd >= 128){
		return -1;
	}

	lock_acquire(&file_lock);
	file_seek((thread_current()->fd)[fd] , position);
	lock_release(&file_lock);
	return 0;
}

void close(int fd){
	if(fd >= 128){
		return;
	}
	lock_acquire(&file_lock);
	file_close(thread_current()->fd[fd]);
	lock_release(&file_lock);
	thread_current()->fd[fd] = NULL;
}

tid_t fork(const char *name, struct intr_frame *if_){
	return process_fork(name, if_);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	int res, fd;
	bool tf;

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
		res = process_wait(f->R.rdi);
		f->R.rax = res;
		break;
	case SYS_CREATE:
		tf = create((char *)(f->R.rdi), f->R.rsi);
		f->R.rax = tf;
		break;
	case SYS_REMOVE:
		/* code */
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
		res = read((int)(f->R.rdi), f->R.rsi, f->R.rdx);
		thread_current()->tf.R.rax = res;
		f->R.rax = res;
		break;
	case SYS_WRITE:
		res = write((int)(f->R.rdi), f->R.rsi, f->R.rdx);
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
	default:
		break;
	}

	// thread_exit ();
}
