#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)
	movq %r12, temp2(%rip)     /* callee saved registers */
	movq %rsp, %rbx            /* Store userland rsp    */
	movabs $tss, %r12
	movq (%r12), %r12
	movq 4(%r12), %rsp         /* Read ring0 rsp from the tss */
	/* Now we are in the kernel stack */
	push $(SEL_UDSEG)      /* if->ss / syscall을 호출하기 전에 상태를 스택에 저장하는 과정*/
	push %rbx              /* if->rsp */
	push %r11              /* if->eflags */
	push $(SEL_UCSEG)      /* if->cs */
	push %rcx              /* if->rip */
	subq $16, %rsp         /* skip error_code, vec_no */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	push %rax
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0				/*popq의 반대 작업을 수행하여, 레지스터의 값을 스택에 넣는다.*/
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* skip r11 */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi

check_intr:
	btsq $9, %r11          /* Check whether we recover the interrupt */
	jnb no_sti
	sti                    /* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12	/* syscall handler 호출*/
	call *%r12						/* syscall handler 호출*/
	popq %r15						/* 스택의 top값을 r15에 저장하고 스택 포인터를 8바이트씩 증가시킨다*/
	popq %r14						/* 증가시키는것 -> 스택에서는 pop개념*/
	popq %r13						/* 함수 호출 후 원상보귀할 때, 이전 상태를 저장해두었던 레지스터 값을 복원하는데 사용*/
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp			/*rsp 를 32바이트 더해줌*/
	popq %rcx              /* if->rip */
	addq $8, %rsp			/*rsp를 8바이트 더해줌*/
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq					/*syscall  종료하고 user 공간으로 복귀*/

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
