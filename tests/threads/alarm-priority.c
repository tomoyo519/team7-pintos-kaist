/* Checks that when the alarm clock wakes up threads, the
   higher-priority threads run first. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;
static struct semaphore wait_sema;

void
test_alarm_priority (void) 
{
  int i;
  
  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  wake_time = timer_ticks () + 5 * TIMER_FREQ; //현재 ticks +(500)
  sema_init (&wait_sema, 0); //init을 할때 value는 0으로
  
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1; //i가 5가 되어야 2를 빼주고, 5가 되기 전까지는 1만 빼줌
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, alarm_priority_thread, NULL); //해당 thread를 ready큐에 넣어줌
    }

  thread_set_priority (PRI_MIN); //현재running중인 스레드의 priority의 우선순위를 0으로 -> why

  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);
}

static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* Busy-wait until the current time changes. */
  int64_t start_time = timer_ticks (); //현재 ticks를 넣어줌
  while (timer_elapsed (start_time) == 0) //경과시간이 0이면 while문 동작
    continue;

  /* Now we know we're at the very beginning of a timer tick, so
     we can call timer_sleep() without worrying about races
     between checking the time and a timer interrupt. */
  timer_sleep (wake_time - timer_ticks ()); //일어나야하는 시간 - 현재시간 = 자는 시간

  /* Print a message on wake-up. */
  msg ("Thread %s woke up.", thread_name ());

  sema_up (&wait_sema);
}
