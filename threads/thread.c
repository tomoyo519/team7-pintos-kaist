#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b // magic number

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list blocked_list;
/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

// void make_thread_sleep(int64_t ticks);
// void make_thread_wakeup(int64_t ticks);
static bool tick_less(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
static bool tick_less_priority_cmp(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
static bool ready_list_cmp(const struct list_elem *a_, const struct list_elem *b_,
						   void *aux UNUSED);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
	// 부팅상태면 interrupt가 꺼져있는게 정상
	// assert 결과가 false면 시스템 셧다운 -> interrupt가 활성상태라는 것
	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 disable 상태인지를 확인하고, 비활성화 상태면 thread_init을 실행시킨다.

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);	// 한번에 하나의 스레드만 생성할 수 있게 lock(세마포어에서의)을 건다. 임계영역에 스레드가 들어갔는데, 다른 스레드가 들어오지 못하게 lock
	list_init(&ready_list); // ready queue 초기화
	list_init(&blocked_list);
	list_init(&destruction_req); // 파괴(kill) 요청이 들어온 스레드들을 모아두는 리스트 초기화

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();				  // running상태의 thread 구조체의 주소를 반환
	init_thread(initial_thread, "main", PRI_DEFAULT); // 스레드를 초기화하고 초기 상태는 blocked 상태 -> running 상태로 바꿔줌
	initial_thread->status = THREAD_RUNNING;		  // running 상태로 만들어줌 (아직 돌아가는건 아님)
	initial_thread->tid = allocate_tid();			  // tid 부여
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started; //
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started); //

	/* Start preemptive thread scheduling.
	 thread scheduling을 선점한다.
	 선점 상태 : 쓰잘데기 없는거라서 언제든지 비켜줄수 있는거
	 비선점 상태 : 비켜줄수 없다!*/
	intr_enable(); // 인터럽트를 활성화 시킨다. 인터럽트가 있다는 거 자체가 선점을 하겠다!라는 말?
	// 선점 상태인 스레드한테 인터럽트를 걸면 뺏을 수 있으니까 인터럽트를 걸어버리자!
	// 비선점은 인터럽트를 못 건다!

	/* Wait for the idle thread to initialize idle_thread.
	idle thread가 초기화 되기 전에 다른 스레드가 실행되면 안돼서 sema_down을 해준다.*/
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;
	// function 인자는 thread가 생성되고 이 스레드가 시작할때 실행할 함수이다.
	ASSERT(function != NULL); // 유효한 함수 포인터를 가리키고 있어야 한다

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO); // 페이지 할당받아서 스레드를 가리키게 만든다.
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority); // 할당받은 우선순위로 init해준다
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument.
	 rip : 명령어가 현재 실행되는 곳의 포인터*/
	t->tf.rip = (uintptr_t)kernel_thread; // 선점 상태로 호출하기 위함
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock(t); // ready queue에 넣어준다.

	// running 쓰레드보다 우선순위 더 높으면 yield
	if (thread_current()->priority < t->priority)
	{
		thread_yield();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
/*running thread를 blocked로 만들어주고 scheduling까지 해주는 함수*/
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

void make_thread_sleep(int64_t ticks)
{
	enum intr_level old_level;
	old_level = intr_disable();
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	// printf("in thread sleep\n");
	struct thread *t = thread_current();

	// ticks를 어디다 쓸까
	// 넣을때 정렬해서 넣기
	//  list_push_back(&blocked_list, &thread_current() -> elem);
	// list, 현재 리스트에 삽입하려는 thread, list에 있는 스레드

	t->thread_tick_count = ticks;
	t->status = THREAD_BLOCKED;
	list_insert_ordered(&blocked_list, &(t->elem), tick_less_priority_cmp, NULL);
	schedule();
	intr_set_level(old_level);
}

void make_thread_wakeup(int64_t ticks)
{

	// 이렇게 구현하려면 순서대로 정렬해서 넣어줘야힘
	while (!list_empty(&blocked_list) && list_entry(list_front(&blocked_list), struct thread, elem)->thread_tick_count <= ticks)
	{
		struct thread *t = list_entry(list_pop_front(&blocked_list), struct thread, elem);
		// list_pop_front(&blocked_list);
		thread_unblock(t);
	}
}

static bool
tick_less(const struct list_elem *a_, const struct list_elem *b_,
		  void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->thread_tick_count < b->thread_tick_count;
}

static bool
tick_less_priority_cmp(const struct list_elem *a_, const struct list_elem *b_,
					   void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	if (a->thread_tick_count == b->thread_tick_count)
	{
		return a->priority >= b->priority;
	}

	return a->thread_tick_count < b->thread_tick_count;
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/*blocked thread를 ready 상태로 바꾸고, ready queue에 넣어주는 함수*/
/*ready list에 넣어줄때 우선순위로 넣어줌 이 함수 수정하거나, 새로 만들어야함*/
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();			 // intr_disable return 값이 previous interrupt
	ASSERT(t->status == THREAD_BLOCKED); // thread blocked 상태면 다음 줄로 넘어감

	list_insert_ordered(&ready_list, &t->elem, priority_cmp, NULL);
	// list_push_back(&ready_list, &t->elem); // ready queue에 해당 스레드의 elem를 넣어줌
	t->status = THREAD_READY;  // 상태를 ready로 바꾸고
	intr_set_level(old_level); // 이전 인터럽트 상태로 원상복귀 시켜준다.
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	// printf("%d\n", t->status);
	ASSERT(is_thread(t));
	printf("😤%s	\n", t->status);
	ASSERT(t->status == THREAD_RUNNING);
	// printf("in thread current, next ASSERT(t->status == THREAD_RUNNING)\n");
	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* 스레드 뺏김 */
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context()); // false일때 넘어간다. 처리 중이 아닌 상태여야 넘어간다

	old_level = intr_disable(); // 인터럽트 비활
	if (curr != idle_thread)	// idle thread면 ready 중인 스레드가 없다
		list_insert_ordered(&ready_list, &curr->elem, priority_cmp, NULL);
	// list_push_back(&ready_list, &curr->elem); // ready queue에 넣는다.
	do_schedule(THREAD_READY); // 뺏기는 과정이 do_schedule 현재 running 중인 thread를 ready queue에 넣어주고, ready queue에 있는 스레드를 실행시킨다.
	intr_set_level(old_level); // 이전 interuppt로 복구
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{

	// // 현재 소유중인 락 데이터 필요함
	// // 현재 쓰레드의 우선순위보다 작을 때
	// if (thread_current()->priority > new_priority) {

	// }
	// // 소유중인 락의 우선순위 변경

	// // 현재 쓰레드의 우선순위보다 클 때
	// // 현재 쓰레드의 우선순위 변경
	// // 소유중인 락의 우선순위 변경

	// // 현재 우선순위 값 확인
	// // 현재 우선순위 < 새 우선순위
	// 	// 락 소유X: 그냥 바꾸기
	// 	// 락 소유O:
	// // 현재 우선순위 > 새 우선순위
	// // 현재 우선순위 == 새 우선순위

	// // 락 소유 안했을 때

	// // 락 소유 했을 때

	// 락이 있을 때,
	// 락의 priority 갱신
	struct thread *t;
	struct list_elem *e;
	struct list *lock_list = &thread_current()->lock_list;
	struct list *dons = &thread_current()->donations;
	struct lock *lock;

	for (e = list_begin(lock_list); e != list_end(lock_list); e = list_next(e))
	{
		struct lock *lock = list_entry(e, struct lock, elem);
		lock->priority = new_priority;
	}

	// 락 있을 때, 없을 때 공통

	// 자기보다 작아질 때,
	if (thread_current()->priority > new_priority)
	{
		// 도네 받은 거일 때,
		// 무시

		// 도네받은 최대 우선순위 구하기
		int max_priority = -1;
		for (e = list_begin(dons); e != list_end(dons); e = list_next(e))
		{
			t = list_entry(e, struct thread, d_elem);
			if (max_priority < t->priority)
			{
				max_priority = t->priority;
			}
		}

		// 도네 안받은 거일 때(맥스값보다 자기가 크면 됨, 기부자 없으면 -1, 있는데 이후 set 해줬을 경우도 고려됨)
		if (thread_current()->priority > max_priority)
		{
			// 락들의 waiters중 최대값 구하기
			int max_priority_in_waiters = -1;
			struct list_elem *ee;

			for (e = list_begin(lock_list); e != list_end(lock_list); e = list_next(e))
			{
				lock = list_entry(e, struct lock, elem);
				struct list *waiters = &lock->semaphore.waiters;

				for (ee = list_begin(waiters); ee != list_end(waiters); ee = list_next(ee))
				{
					t = list_entry(ee, struct thread, elem);

					if (max_priority_in_waiters < t->priority)
					{
						max_priority_in_waiters = t->priority;
					}
				}
			}

			// new_priority가 같거나 크면 기부 받을 필요 없음

			// max_priority_in_waiters가 더 큰 경우
			if (new_priority < max_priority_in_waiters)
			{
				// 기부받기
				thread_current()->priority = max_priority_in_waiters;
				list_push_back(dons, &t->d_elem);
			}

			else if (max_priority_in_waiters == -1)
			{
				thread_current()->priority = new_priority;
			}
		}
		// lock들의 waiters 중 가장 큰 애와 new_priority 비교한다
		// new_priority가 아니면 기부자
		// 락들 웨이터스 중
	}

	// 자기보다 높아질 때,
	else if (thread_current()->priority < new_priority)
	{
		thread_current()->priority = new_priority;
	}
	thread_yield();

	// // 자기랑 같을 때,
	// else {
	// 	// 기부받은 거면 기부 리스트 삭제
	// 	if (!list_empty(&thread_current()->donations)) {
	// 		for (e = list_begin(&thread_current()->donations); e != list_end(&thread_current()->donations); e = list_next(e)) {
	// 			t = list_entry(e, struct thread, d_elem);

	// 			if (t->priority == thread_current()->priority) {
	// 				list_remove(e);
	// 			}
	// 		}
	// 	}
	// }
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t); // 0으로 초기화하고
	list_init(&t->donations);
	list_init(&t->lock_list);
	t->status = THREAD_BLOCKED;						   // blocked 상태로(맨처음 상태가 blocked 상태)
	strlcpy(t->name, name, sizeof t->name);			   // 인자로 받은 이름을 스레드 이름으로 하는것
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *); // 스택 포인터 설정
	t->priority = priority;
	t->magic = THREAD_MAGIC; // 스택 오버플로우 판단하는 변수

	// project 2;
	t->exit_status = -1;
	t->is_child_create = 0;
	t->parent_p = NULL;
	// 현재 스레드가 다음에 할당할 파일 디스크립터 번호
	// 표준 입력과 출력, 그리고 에러출력을 위한 0,1,2 는 이미 예약되어 있기 때문에 초기값은 2
	//
	t->fdt = calloc(FDT_COUNT_LIMIT, sizeof(struct file *));
	t->next_fd = 2;
	list_init(&t->child_list);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
/*인터럽트 프레임 (이를 통해 CPU 레지스터들의 상태)를 복원하는 역할을 합니다. 이 함수는 인터럽트가 반환될 때 호출됩니다.*/
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"		// C코드에서 입력으로 들어온 tf값으로, 스택 포인터(rsp)에 tf를 저장
		"movq 0(%%rsp),%%r15\n" //
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n" // 데이터를 120byte만큼 제거(pop)
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq" // tf상에서
		: : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{ // 파괴하려는 스레드가 모인 리스트가 빌때까지
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim); // 희생자들을 page free 시킨다
	}
	thread_current()->status = status;
	schedule();
}
/*ready queue에 있는 우선순위 높은 스레드를 running상태로 바꿔주는 것*/
static void
schedule(void)
{
	struct thread *curr = running_thread();		// 돌고있는 thread
	struct thread *next = next_thread_to_run(); // 다음에 돌아야할 thread(ready queue 제일 앞에 있는 것)
	// int swch = 0;
	ASSERT(intr_get_level() == INTR_OFF);	// 다른 스레드가 스케줄링일때는 인터럽트 걸면 안돼서 비활성화시켜주기
	ASSERT(curr->status != THREAD_RUNNING); // 러닝 상태가 아니면 다음으로 넘어감(이 함수 호출 직전에 블럭으로 만들어주었던 걸 확인)
	ASSERT(is_thread(next));				// 스레드인지 확인
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		// swch++;
		// printf("Switch : %d\n", swch);
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

bool priority_cmp(const struct list_elem *a_, const struct list_elem *b_,
				  void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->priority > b->priority;
}

bool priority_cmp_for_done_max(const struct list_elem *a_, const struct list_elem *b_,
							   void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, d_elem);
	const struct thread *b = list_entry(b_, struct thread, d_elem);

	return a->priority < b->priority;
}

bool priority_cmp_for_waiters_max(const struct list_elem *a_, const struct list_elem *b_,
								  void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->priority < b->priority;
}

//
bool priority_cmp_for_cond_waiters_max(const struct list_elem *a_, const struct list_elem *b_,
									   void *aux UNUSED)
{
	// a_로부터 semaphore_elem을 찾고, semaphore_elem->holder로 쓰레드 접근 후 우선순위 뽑아내기
	const struct thread *a = list_entry(a_, struct semaphore_elem, elem)->holder;
	const struct thread *b = list_entry(b_, struct semaphore_elem, elem)->holder;

	return a->priority > b->priority;
}

void sort_ready_list(void)
{
	list_sort(&ready_list, priority_cmp, NULL);
}

void print_ready_list(void)
{
	struct list_elem *e;

	for (e = list_begin(&ready_list); e != list_end(&ready_list); e = list_next(e))
	{
		printf("elem's val: %d\n", e->val);
	}
}

bool ready_list_cmp(const struct list_elem *a_, const struct list_elem *b_,
					void *aux UNUSED)
{

	return a_->val < b_->val;
}

void test_list_max(void)
{
	struct list_elem elems[4];
	// for (int i = 0; i < 5; i++) {
	// 	elems[i].val = i + 1;
	// 	elems[i].name = i;
	// }

	elems[0].val = 3;
	elems[0].name = "e1";
	elems[1].val = 3;
	elems[1].name = "e2";
	elems[2].val = 4;
	elems[2].name = "e3";
	elems[3].val = 1;
	elems[3].name = "e0";

	list_push_back(&ready_list, &elems[0]);
	list_push_back(&ready_list, &elems[1]);
	list_push_back(&ready_list, &elems[2]);
	list_push_back(&ready_list, &elems[3]);
	// list_push_back(&ready_list, &elems[4]);
	// list_push_back(&ready_list, &elems[0]);
	// list_push_back(&ready_list, &elems[1]);

	struct list_elem *e = list_max(&ready_list, ready_list_cmp, NULL);
	int a = 12;
}

struct thread *get_thread(struct list_elem *e)
{
	return list_entry(e, struct thread, elem);
}

struct thread *get_d_thread(struct list_elem *e)
{
	return list_entry(e, struct thread, d_elem);
}

struct thread *get_cond_thread(struct list_elem *e)
{
	return list_entry(e, struct semaphore_elem, elem)->holder;
}

void thread_preemption(void)
{
	if (!intr_context() && !list_empty(&ready_list) && thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
		thread_yield();
}