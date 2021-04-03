#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void check_addr(void *addr);
void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);


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
	thread_current()->tf.R.rax = status;
	struct thread * t = thread_current();
	printf("%s: exit(%d)\n", thread_current()->name, 0);
	thread_exit();
}

int write(int fd, const void* buffer, unsigned size){
	if((thread_current()->fd)[fd]==NULL){
		return -1;
	}

	if(fd<=0 || fd >= 128){
		return -1;
	}
	int res; 

	if(fd==1){
		putbuf(buffer, size);
	} else {
		lock_acquire(&file_lock);
		res = file_write(thread_current()->fd[fd], buffer, size);
		lock_release(&file_lock);
		return res;
	}
	return size;
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	int res; 

	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rsi);
		break;
	case SYS_FORK:
		/* code */
		break;
	case SYS_EXEC:
		/* code */
		break;
	case SYS_WAIT:
		/* code */
		break;
	case SYS_CREATE:
		/* code */
		break;
	case SYS_REMOVE:
		/* code */
		break;
	case SYS_OPEN:
		/* code */
		break;
	case SYS_FILESIZE:
		/* code */
		break;
	case SYS_READ:
		/* code */
		break;
	case SYS_WRITE:
		res = write((int)(f->R.rdi), (const void*)f->R.rsi, f->R.rdx);
		thread_current()->tf.R.rax = res;
		break;
	case SYS_SEEK:
		/* code */
		break;
	case SYS_TELL:
		/* code */
		break;
	case SYS_CLOSE:
		/* code */
		break;
	default:
		break;
	}

	// thread_exit ();
}
