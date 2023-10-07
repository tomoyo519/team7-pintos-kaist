#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/*-----project2-----------*/
#include <stdbool.h>
#include <debug.h>
#include <stddef.h>

#include <stdint.h>
#include "include/lib/syscall-nr.h"
#include "threads/init.h"
#include <string.h>
#include "include/userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/* An offset within a file.
 * This is a separate header because multiple headers want this
 * definition but not any others. */
typedef int32_t off_t;

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
}

//file의 배열을 만들자!
//128인 이유 몰라 페이지 하나래
		//fd_table[fd_num] = fd

void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	//인터럽트 프레임에서 esp를 가져온다.
	void *esp = f->rsp;
	int fd;

	switch (f->R.rax)
	{
	case SYS_HALT:      //power off! 핀토스를 종료, 강종 느낌
		//어셈블러로 halt수행하는 방법?
		//stop
		power_off();
		break;
	case SYS_EXIT:
		//1.현재 동작중인 유저 프로그램을 종료
		//2. 커널에 상태를 리턴하면서 종료
		struct thread *cur = thread_current(); //kernel 쪽 스레드
		f -> R.rax = f -> R.rdi;		//syscall num 다 썼으니까 exit status로 덮어 씌워줌. 
		printf("%s : exit(%d)\n", cur -> name, f->R.rax);
		thread_exit();
		break;
	case SYS_FORK:      
		//1.thread_name 이라는 이름을 가진 현재 프로세스의 복제본인 새 프로세스를 만든다

		//2.자식 프로세스의 pid를 반환해야 한다.
		break;
	case SYS_EXEC:      
		
		break;
	case SYS_WAIT:    //made by 유정  
		//1. 자식의 종료 상태(exit status)를 가져온다
		// tid_t exit_status = f->R.rdi;
		// return process_wait(exit_status);
		break;
	case SYS_CREATE:      
		
		break;
	case SYS_REMOVE:      
		
		break;
	case SYS_OPEN:      	
		
		break;
	case SYS_FILESIZE:
		fd = f->R.rdi;
		file_length(fd);
		break;
	case SYS_READ:      
		
		break;
	case SYS_WRITE:      
		write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:      
		
		break;
	case SYS_TELL:      
		
		break;
	case SYS_CLOSE:      
		
		break;
	default:
		break;
	}

	// printf ("system call!\n");
	// thread_exit ();
}

int write(int fd, const void* buffer, unsigned int size){
  if(fd==1){
    putbuf(buffer, size);
    return size;
  }
  return -1;
}

/* Returns the length, in bytes, of INODE's data. */
// off_t
// file_length (int fd) {
// 	struct thread *cur = thread_current();
// 	off_t file_size = cur -> fdt->fd_table->file->inode.data.length;
// 	return file_size;
// }