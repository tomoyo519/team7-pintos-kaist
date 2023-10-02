/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). 
   SEMA 세마포어를 VALUE 값으로 초기화한다. 세마포어는 그것을 조작하기 위한 두 개의 원자적 연산자와 함께하는 음수가 아닌 정수이다:

down 또는 "P": 값이 양수가 될 때까지 기다린 후, 그 값을 감소시킨다.

up 또는 "V": 값을 증가시킨다 (그리고 기다리는 스레드가 있다면 하나를 깨운다).*/
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters); //waiter를 초기화
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. 
   세마포어에 대한 Down 또는 "P" 연산이다. SEMA의 값이 양수가 될 때까지 기다린 후 원자적으로 그 값을 감소시킨다.

	이 함수는 슬립(sleep) 상태가 될 수 있으므로, 인터럽트 핸들러 내에서 호출되어서는 안 된다. 
	이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 
	만약 이 함수가 슬립 상태가 된다면, 다음에 스케줄링될 스레드는 아마도 인터럽트를 다시 활성화시킬 것이다. 이것은 sema_down 함수이다.
	
	스레드를 blocked로 만들어준다
	*/

void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable (); //인터럽트 비활
	while (sema->value == 0) { //sema 값이 0이면 무한 루프를 돈다
	//현재 러닝 중인 스레드(자기자신)를 웨이트 리스트에 넣고 블락시킴
	//해당 스레드는 블락 상태가 되고, 다른 스레드로 스케줄링이 됨
	//sema_up이 호출되는 순간 웨이트 리스트에 있는 것들 중 맨앞에 있는 것을 unblock시키고 sema 값을 증가시킴
		list_push_back (&sema->waiters, &thread_current ()->elem); //ELEM을 LIST의 끝에 삽입하여, 그것이 LIST의 마지막 요소가 된다.
		thread_block (); //
	}
	sema->value--; //value 감소 시키고
	intr_set_level (old_level); //이전 인터럽트 상태를 인자로 넣어서(원상복귀)
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0) //value가 0이상이면
	{
		sema->value--; //value감소시키고 
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
   struct list_elem* e;

   //waiters에 thread가 있다면
	if (!list_empty (&sema->waiters)) {
      // waiters에서 우선순위 최댓값 찾아서 ready -> ready큐 삽입
      e = list_max(&sema->waiters, priority_cmp_for_waiters_max, NULL);
      list_remove(e);
		thread_unblock (list_entry (e, struct thread, elem)); //블락된 스레드를 러닝 상태로 바꿔줌
   } 

	sema->value++; //sema 구조체에서 value를 ++
   thread_yield();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2]; //세마포어 두개
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]); //첫번째 세마포어는 계속 value를 up해주고 -> waiters에서 pop하고 unblock
		sema_down (&sema[1]); //두번째 세마포어는 계속 value를 down해준다 -> waiters에 계속 넣어준다
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]); //반대로 
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
/*락의 특징 
  1. 락은 한번에 하나의 스레드만 소유할 수 있다.
  2. 현재 락을 소유하고 있는 스레드가 락을 소유하려 시도 하는것은 오류
  3. 락은 초기값이 1이다
  4. 락은 동일한 스레드가 락을 해제하고 획득할수있다.
  세마포어의 특징
  1. 세마포어는 1 이상의 값을 가질 수 있다.세마포어의 value는 동시에 수행될 수 있는 최대 작업의 수
  2. 세마포어는 소유자가 없다. 즉 하나의 스레드가 세마포어를 down하고 다른 스레드가 up할 수 있다.*/
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
   /*이미 락을 보유하고 있는 스레드는 락을 다시 소유하려 시도해서는 안된다.
   인터럽트가 비활성화된 상태에서 이 함수가 호출되면 스레드가 sleep(block) 상태로 전환해야할때, 인터럽트는 다시 활성화된다.*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
   struct list_elem* e;
   struct thread* temp_thread;
   struct lock* temp_lock;
   
   // enum intr_level prev_level = intr_disable();

   if (lock->semaphore.value == 0) {
      if (lock->holder->priority < thread_current()->priority) {
         thread_current()->wait_on_lock = lock;
         temp_lock = lock;
         temp_thread = thread_current();

         while (temp_lock != NULL) {
            if (list_empty(&temp_lock->holder->donations)) {
               list_push_back(&temp_lock->holder->donations, &temp_thread->d_elem);
               temp_lock->holder->priority = temp_thread->priority;
            }

            else {
               for (e = list_begin(&temp_lock->holder->donations); e != list_end(&temp_lock->holder->donations); e = list_next(e)) {
                  struct thread* t = list_entry(e, struct thread, d_elem);

                  if (t->wait_on_lock == temp_lock) {
                     list_remove(e);
                     list_push_back(&temp_lock->holder->donations, &temp_thread->d_elem);
                     temp_lock->holder->priority = temp_thread->priority;
                     break;
                  }
               }

               if (e == list_tail(&temp_lock->holder->donations)) {
                  list_push_back(&temp_lock->holder->donations, &temp_thread->d_elem);
                  temp_lock->holder->priority = temp_thread->priority;
               }
            }

            temp_thread = temp_lock->holder;
            temp_lock = temp_lock->holder->wait_on_lock;
         }
         sort_ready_list();

      }
   }

   // intr_set_level(prev_level);
	sema_down (&lock->semaphore); //락안의 세마포어의 value를 down시킨다. -> wait리스트에 스레드를 넣고 block상태로 만든다
	lock->holder = thread_current (); //현재 스레드가 lock을 가지고 있다.
   list_push_back(&thread_current()->lock_list, &lock->elem);
   lock->priority = thread_current()->priority;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
   struct list_elem* e;
   struct list* dons = &thread_current()->donations;;
   struct thread* t;   

   // 기부 받았을 때,
   if (!list_empty(dons)) {
      // 기부자 제거
      for (e = list_begin(dons); e != list_end(dons); e = list_next(e)) {
         t = list_entry(e, struct thread, d_elem);
         
         if (t->wait_on_lock == lock) {
            list_remove(e);
            break;
         }
      }

      t = thread_current();

      if (list_empty(dons)) {
         t->priority = lock->priority;
      }
      else {
         t->priority = list_entry(list_max(dons, priority_cmp_for_done_max, NULL), struct thread, d_elem)->priority;
      }
   }

	lock->holder = NULL;
   list_remove(&lock->elem);
	sema_up (&lock->semaphore);
   thread_yield();
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock)); //현재 스레드가 락을 보유한 상태면

	sema_init (&waiter.semaphore, 0); //
   waiter.holder = thread_current();
   list_insert_ordered(&cond->waiters, &waiter.elem, priority_cmp_for_cond_waiters_max, NULL);
	// list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
   waiter.holder = NULL;
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

   struct list_elem* e;
	if (!list_empty (&cond->waiters)) {
      // waiter로부터 우선순위가 가장 큰 애의 semaphore를 넣어야 함. 
      // semaphore_elem 구조체 반환
      // e = list_max(&cond->waiters, priority_cmp_for_cond_waiters_max, NULL);

      // sema_up(&list_entry(e, struct semaphore_elem, elem)->semaphore);
      // list_remove(e);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
   }
		// sema_up (&list_entry (list_pop_front (&cond->waiters),
      // cond waiters에서 우선순위 제일 높은놈을 sema_up 해주고 cond waiters에서 삭제해줌

}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
