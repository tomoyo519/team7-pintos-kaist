#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
//전처리기 지시자
#if TIMER_FREQ < 19 //해당 조건을 만족하면
//컴파일 에러를 발생시킴
#error 8254 timer requires TIMER_FREQ >= 19 
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
void timer_print_stats(void);
/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt.
   -----------------------------------------------------------
	Korean:8254 programmable interval timer(PIT)를 설정하여 초당 PIT_FREQ 번 인터럽트를 발생시키고
	해당 인터럽트를 등록한다.*/
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. 
	   --------------------------------------------------------
	   korean : 8254의 input frequency를 TIMER_FREQ로 나누고, 가장 가까운 값으로 반올림한다.
	   8254 타이머의 입력 주파수를 TIMER_FREQ 값으로 조절하려는 의도를 설명한다. 
	   이를 통해 원하는 타이밍 빈도로 타이머를 설정할 수 있다.*/
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
	/*outb(port, data)*/
	//0x43 : PIT, 0x34 : PIT의 스펙
	outb (0x43, 0x34);    //타이머를 깨운다 /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). 
   */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
//timer가 적어도 x번 tic할때까지 thread 호출의 실행을 일시 중단한다. 시스템이 idle(다음 thread가 없는) 상태가 아니면,
//thread가 정확히 x번의 tick이 발생한 직후에 wake up할 필요가 없다.
//thread가 적절한 시간 동안 대기한 후 ready queue에 놓이게 하라

//현재 스레드를 tick시간동안 잠재우는 함수
void
timer_sleep (int64_t ticks) {
	// int64_t start = timer_ticks (); //현재 tic을 start에 넣어서
	
	ASSERT (intr_get_level () == INTR_ON); //interrupt 상태인지 확인
	// printf("<1>\n");
	// while (timer_elapsed (start) < ticks) //start로 부터 시간이 얼마나 지났는지

	// 	thread_yield ();
	// printf("Switch : %d\n", swch);
	
	// printf("in 'while' ... running\n");
	make_thread_sleep(timer_ticks () + ticks); //현재 시간부터 재워야할 tick을 더해주어야함
	// printf("timer_sleep finish\n");
	// thread_print_stats();
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	make_thread_wakeup(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
