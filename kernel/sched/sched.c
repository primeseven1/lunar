#include <crescent/common.h>
#include <crescent/sched/sched.h>
#include <crescent/sched/kthread.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>
#include <crescent/mm/heap.h>
#include "sched.h"

static struct proc* kernel_proc;
static struct cpu** cpus = NULL;
static u64 cpu_count = 1;

struct thread* select_new_thread(struct thread* start) {
	struct thread* thread = start->sched_info.next;
	if (!thread)
		thread = start;

	while (thread) {
		unsigned int state = atomic_load(&thread->state, ATOMIC_SEQ_CST);
		if (state == THREAD_STATE_RUNNABLE)
			break;
		
		thread = thread->sched_info.next;
	}

	return thread;
}

static struct cpu* sched_pick_cpu(const u8* affinity) {
	if (cpu_count == 1 || !cpus)
		return current_cpu();

	struct cpu* best = NULL;
	u64 i;

	/* Find the first allowed CPU */
	for (i = 0; i < cpu_count; i++) {
		size_t byte = i / 8;
		unsigned int bit  = i % 8;
		if (affinity[byte] & (1 << bit)) {
			best = cpus[i];
			break;
		}
	}

	/* pick the allowed CPU with fewest threads */
	for (i = i + 1; i < cpu_count; i++) {
		size_t byte = i / 8;
		unsigned int bit  = i % 8;
		u64 current_thread_count = atomic_load(&cpus[i]->thread_count, ATOMIC_SEQ_CST);
		u64 best_thread_count = atomic_load(&best->thread_count, ATOMIC_SEQ_CST);
		if ((affinity[byte] & (1 << bit)) && current_thread_count < best_thread_count)
			best = cpus[i];
	}

	/* 
	 * thread affinity doesn't want any CPU to run this thread, 
	 * but at least one CPU must run this thread 
	 */
	return best ? best : current_cpu();
}

static int new_thread_init(struct thread* thread, struct proc* proc, int flags) {
	size_t affinity_size = (cpu_count + 7) / 8;
	thread->sched_info.affinity = kzalloc(affinity_size, MM_ZONE_NORMAL);
	if (!thread->sched_info.affinity)
		return -ENOMEM;

	if (flags & SCHED_THIS_CPU) {
		u32 sched_id = current_cpu()->sched_processor_id;
		size_t byte = sched_id / 8;
		unsigned int bit = sched_id % 8;
		thread->sched_info.affinity[byte] |= (1 << bit);
	} else {
		memset(thread->sched_info.affinity, INT_MAX, affinity_size);
	}

	struct cpu* target_cpu = sched_pick_cpu(thread->sched_info.affinity);
	thread->target_cpu = target_cpu;
	thread->proc = proc;
	int state = flags & THREAD_STATE_RUNNING ? THREAD_STATE_RUNNING : THREAD_STATE_RUNNABLE;
	atomic_store(&thread->state, state, ATOMIC_SEQ_CST);

	return 0;
}

static void thread_add_to_proc(struct proc* proc, struct thread* thread) {
	spinlock_lock(&proc->threadinfo.lock);

	if (proc->threadinfo.threads) {
		struct thread* tail = proc->threadinfo.threads;
		while (tail->proc_info.next)
			tail = tail->proc_info.next;
		tail->proc_info.next = thread;
		thread->proc_info.prev = tail;
		thread->proc_info.next = NULL;
	} else {
		proc->threadinfo.threads = thread;
		thread->proc_info.prev = NULL;
		thread->proc_info.next = NULL;
	}

	proc->threadinfo.thread_count++;
	spinlock_unlock(&proc->threadinfo.lock);
}

static void thread_add_to_queue(struct thread* thread) {
	struct cpu* target_cpu = thread->target_cpu;
	spinlock_lock(&target_cpu->thread_lock);

	if (target_cpu->thread_queue) {
		struct thread* tail = target_cpu->thread_queue;
		while (tail->sched_info.next)
			tail = tail->sched_info.next;
		tail->sched_info.next = thread;
		thread->sched_info.prev = tail;
		thread->sched_info.next = NULL;
	} else {
		target_cpu->thread_queue = thread;
		thread->sched_info.prev = NULL;
		thread->sched_info.next = NULL;
	}

	atomic_add_fetch(&target_cpu->thread_count, 1, ATOMIC_SEQ_CST);
	spinlock_unlock(&target_cpu->thread_lock);
}

int schedule_thread(struct thread* thread, struct proc* proc, int flags) {
	if (!cpus && !(flags & SCHED_THIS_CPU))
		return -EAGAIN;
	if (!proc)
		proc = kernel_proc;

	unsigned long irq_flags = local_irq_save();
	int err = new_thread_init(thread, proc, flags);
	if (err)
		goto out;

	thread_add_to_proc(proc, thread);
	thread_add_to_queue(thread);
out:
	local_irq_restore(irq_flags);
	return err;
}

static _Noreturn void* idle(void* _unused) {
	(void)_unused;
	while (1)
		__asm__ volatile("hlt");
}

void sched_init(void) {
	sched_create_init();
	sched_preempt_init();

	kernel_proc = sched_proc_alloc();
	assert(kernel_proc != NULL);
	assert(kernel_proc->pid == 0);
	kernel_proc->mm_struct = current_cpu()->mm_struct;

	struct thread* this_thread = sched_thread_alloc();
	assert(this_thread != NULL);
	schedule_thread(this_thread, kernel_proc, SCHED_RUNNING | SCHED_THIS_CPU);

	struct cpu* cpu = current_cpu();
	cpu->thread_queue = this_thread;
	cpu->current_thread = this_thread;

	struct thread* idle_thread = kthread_create(SCHED_THIS_CPU | SCHED_IDLE, 0, idle, NULL);
	assert(idle_thread != NULL);
}
