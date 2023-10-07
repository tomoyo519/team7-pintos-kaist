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

// #include <syscall.h>
#include <stdint.h>
#include "include/lib/syscall-nr.h"
#include "filesys/filesys.h"
/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t)-1)

/* Map region identifier. */
typedef int off_t;
#define MAP_FAILED ((void *)NULL)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0 /* Successful execution. */
#define EXIT_FAILURE 1 /* Unsuccessful execution. */

__attribute__((always_inline)) static __inline int64_t syscall(uint64_t num_, uint64_t a1_, uint64_t a2_,
															   uint64_t a3_, uint64_t a4_, uint64_t a5_, uint64_t a6_)
{
	int64_t ret;
	register uint64_t *num asm("rax") = (uint64_t *)num_;
	register uint64_t *a1 asm("rdi") = (uint64_t *)a1_;
	register uint64_t *a2 asm("rsi") = (uint64_t *)a2_;
	register uint64_t *a3 asm("rdx") = (uint64_t *)a3_;
	register uint64_t *a4 asm("r10") = (uint64_t *)a4_;
	register uint64_t *a5 asm("r8") = (uint64_t *)a5_;
	register uint64_t *a6 asm("r9") = (uint64_t *)a6_;

	__asm __volatile(
		"mov %1, %%rax\n"
		"mov %2, %%rdi\n"
		"mov %3, %%rsi\n"
		"mov %4, %%rdx\n"
		"mov %5, %%r10\n"
		"mov %6, %%r8\n"
		"mov %7, %%r9\n"
		"syscall\n"
		: "=a"(ret)
		: "g"(num), "g"(a1), "g"(a2), "g"(a3), "g"(a4), "g"(a5), "g"(a6)
		: "cc", "memory");
	return ret;
}

/* Invokes syscall NUMBER, passing no arguments, and returns the
   return value as an `int'. */
#define syscall0(NUMBER) ( \
	syscall(((uint64_t)NUMBER), 0, 0, 0, 0, 0, 0))

/* Invokes syscall NUMBER, passing argument ARG0, and returns the
   return value as an `int'. */
#define syscall1(NUMBER, ARG0) ( \
	syscall(((uint64_t)NUMBER),  \
			((uint64_t)ARG0), 0, 0, 0, 0, 0))
/* Invokes syscall NUMBER, passing arguments ARG0 and ARG1, and
   returns the return value as an `int'. */
#define syscall2(NUMBER, ARG0, ARG1) ( \
	syscall(((uint64_t)NUMBER),        \
			((uint64_t)ARG0),          \
			((uint64_t)ARG1),          \
			0, 0, 0, 0))

#define syscall3(NUMBER, ARG0, ARG1, ARG2) ( \
	syscall(((uint64_t)NUMBER),              \
			((uint64_t)ARG0),                \
			((uint64_t)ARG1),                \
			((uint64_t)ARG2), 0, 0, 0))

#define syscall4(NUMBER, ARG0, ARG1, ARG2, ARG3) ( \
	syscall(((uint64_t *)NUMBER),                  \
			((uint64_t)ARG0),                      \
			((uint64_t)ARG1),                      \
			((uint64_t)ARG2),                      \
			((uint64_t)ARG3), 0, 0))

#define syscall5(NUMBER, ARG0, ARG1, ARG2, ARG3, ARG4) ( \
	syscall(((uint64_t)NUMBER),                          \
			((uint64_t)ARG0),                            \
			((uint64_t)ARG1),                            \
			((uint64_t)ARG2),                            \
			((uint64_t)ARG3),                            \
			((uint64_t)ARG4),                            \
			0))

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/*---------------project2-----------------------*/
void halt(void) NO_RETURN;
int exit(int status);
int fork(const char *thread_name, struct intr_frame *t);
int exec(const char *file);
int wait(pid_t);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
void power_off(void);
void check_address(void *addr);
int open(const char *file_name);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void syscall_handler(struct intr_frame *f UNUSED)
{
	// int syscall_n = f->R.rax; // 시스템 콜 넘버;
	struct thread *curr = thread_current();
	// TODO: Your implementation goes here.
	// 인터럽트 프레임에서 esp를 가져온다.
	void *esp = f->rsp;
	int fd;

	switch (f->R.rax)
	{
	case SYS_HALT: // power off! 핀토스를 종료, 강종 느낌
		power_off();
		break;
	case SYS_EXIT:
		// f->R.rax = exit(f->R.rdi);
		// todo: 왜 f->R.rax 포인터가 rdi 를 가르키게 하는지 ???
		f->R.rax = f->R.rdi;
		printf("%s: exit(%d)\n", curr->name, f->R.rdi);
		thread_exit();
		break;
	case SYS_FORK:
		// fork(curr->name);
		// todo: 왜 f->R.rax = ?
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		// f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		// wait();
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		printf("%s: create(%d)\n", curr->name, f->R.rdi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		printf("%s: remove\n", curr->name);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		printf("%s: open\n", curr->name);
		break;
	case SYS_FILESIZE:
		// filesize();
		break;
	case SYS_READ:

		// read();
		break;

	case SYS_WRITE:
		printf("%s", f->R.rsi);
		break;
	case SYS_SEEK:
		// seek();
		break;
	case SYS_TELL:
		// tell();
		break;
	case SYS_CLOSE:
		// close();
		break;
	default:
		break;
	}

	// printf("system call!\n");
	// thread_exit ();
}

void check_address(void *addr)
{
	// /*주소 값이 유저 영역에서 사용하는 주소 값인지 확인 하는 함수
	// 		유저 영역을 벗어난 영역일 경우 프로세스
	// 		종료(exit(-1)) *
	// 	/
	if (!is_user_vaddr(addr))
	{ // 유저 영억이 아니거나,
		exit(-1);
	}
	if (addr == NULL)
	{ // null이면 프로세스 종료.
		exit(-1);
	}
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

int exit(int status)
{
	thread_exit();
	return status;
}

int fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

// 파일 이름과 사이즈를 인자값으로 받아 파일 생성
bool create(const char *file, unsigned initial_size)
{
	// 파일 생성에 성공했다면 true, 실패했다면 false
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file_name)
{
	check_address(file_name);
	struct file *curr_file = filesys_open(file_name);
	if (curr_file == NULL)
	{
		return -1;
	}
	int fd = process_add_file(curr_file);
	if (fd == -1)
	{
		return -1;
	}
	else
	{
		return fd;
	}
}
