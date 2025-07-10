#include <crescent/common.h>
#include <crescent/mm/slab.h>
#include <crescent/core/panic.h>
#include <crescent/sched/sched.h>
#include <crescent/sched/kthread.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/asm/wrap.h>
#include <crescent/mm/heap.h>
#include <crescent/lib/string.h>
#include <crescent/asm/segment.h>
#include <crescent/asm/ctl.h>
#include "sched.h"

static proc_t* kernel_proc = NULL;
static struct cpu** cpus = NULL;
static u64 cpu_count = 1;

static void select_destroy_thread(thread_t* thread) {
	atomic_store(&thread->state, THREAD_STATE_DEAD, ATOMIC_SEQ_CST);

	proc_t* proc = thread->proc;
	unsigned long irq_flags = local_irq_save();

	spinlock_lock(&proc->threadinfo.lock);
	if (thread->prev)
		thread->prev->next = thread->next;
	else
		proc->threadinfo.threads = thread->next;
	if (thread->next)
		thread->next->prev = thread->prev;
	proc->threadinfo.thread_count--;
	spinlock_unlock(&proc->threadinfo.lock);

	/* select_new_thread locks this already */
	struct cpu* cpu = thread->target_cpu;
	if (thread->sched.prev)
		thread->sched.prev->sched.next = thread->sched.next;
	else
		cpu->thread_queue = thread->sched.next;
	if (thread->sched.next)
		thread->sched.next->sched.prev = thread->sched.prev;
	atomic_sub_fetch(&cpu->thread_count, 1, ATOMIC_SEQ_CST);

	kfree(thread->sched.affinity);

	sched_thread_free(thread);
	local_irq_restore(irq_flags);
}

static thread_t* select_new_thread(thread_t* thread) {
	while (thread) {
		unsigned int state = atomic_load(&thread->state, ATOMIC_SEQ_CST);
		if (state == THREAD_STATE_RUNNABLE)
			break;
		if (state == THREAD_STATE_ZOMBIE) {
			if (unlikely(thread->sched.flags & SCHED_IDLE))
				panic("idle task became a zombie!");
			select_destroy_thread(thread);
		}

		thread = thread->sched.next;
	}

	return thread;
}

void sched_switch_from_interrupt(struct context* context) {
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->thread_queue_lock);

	thread_t* current = cpu->current_thread;
	thread_t* new_thread = select_new_thread(current);
	if (!new_thread) {
		new_thread = select_new_thread(cpu->thread_queue);
		if (!new_thread)
			goto out;
	}

	if (atomic_load(&current->state, ATOMIC_SEQ_CST) == THREAD_STATE_RUNNING)
		atomic_store(&current->state, THREAD_STATE_RUNNABLE, ATOMIC_SEQ_CST);
	current->ctx.general_regs = *context;

	*context = new_thread->ctx.general_regs; /* This will allow the interrupt handler to restore the regs for us */
	if (current->proc != new_thread->proc)
		vmm_switch_context(new_thread->proc->vmm_ctx);

	atomic_store(&new_thread->state, THREAD_STATE_RUNNING, ATOMIC_SEQ_CST);
	cpu->current_thread = new_thread;
out:
	spinlock_unlock(&cpu->thread_queue_lock);
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

static int new_thread_init(thread_t* thread, proc_t* proc, unsigned int flags) {
	size_t affinity_size = (cpu_count + 7) / 8;
	thread->sched.affinity = kzalloc(affinity_size, MM_ZONE_NORMAL);
	if (!thread->sched.affinity)
		return -ENOMEM;

	if (flags & SCHED_THIS_CPU) {
		u32 sched_id = current_cpu()->sched_processor_id;
		size_t byte = sched_id / 8;
		unsigned int bit = sched_id % 8;
		thread->sched.affinity[byte] |= (1 << bit);
	} else {
		memset(thread->sched.affinity, INT_MAX, affinity_size);
	}

	struct cpu* target_cpu = sched_pick_cpu(thread->sched.affinity);
	thread->target_cpu = target_cpu;
	thread->proc = proc;
	thread->sched.flags = flags;
	int state = flags & THREAD_STATE_RUNNING ? THREAD_STATE_RUNNING : THREAD_STATE_RUNNABLE;
	atomic_store(&thread->state, state, ATOMIC_SEQ_CST);
	if (flags & SCHED_THREAD_JOIN)
		atomic_add_fetch(&thread->refcount, 1, ATOMIC_SEQ_CST);

	return 0;
}

static void thread_add_to_proc(proc_t* proc, thread_t* thread) {
	spinlock_lock(&proc->threadinfo.lock);

	if (proc->threadinfo.threads) {
		thread_t* tail = proc->threadinfo.threads;
		while (tail->next)
			tail = tail->next;
		tail->next = thread;
		thread->prev = tail;
		thread->next = NULL;
	} else {
		proc->threadinfo.threads = thread;
		thread->prev = NULL;
		thread->next = NULL;
	}

	proc->threadinfo.thread_count++;
	spinlock_unlock(&proc->threadinfo.lock);
}

static void thread_add_to_queue(thread_t* thread) {
	struct cpu* target_cpu = thread->target_cpu;
	spinlock_lock(&target_cpu->thread_queue_lock);

	if (target_cpu->thread_queue) {
		thread_t* tail = target_cpu->thread_queue;
		while (tail->sched.next)
			tail = tail->sched.next;
		tail->sched.next = thread;
		thread->sched.prev = tail;
		thread->sched.next = NULL;
	} else {
		target_cpu->thread_queue = thread;
		thread->sched.prev = NULL;
		thread->sched.next = NULL;
	}

	atomic_add_fetch(&target_cpu->thread_count, 1, ATOMIC_SEQ_CST);
	spinlock_unlock(&target_cpu->thread_queue_lock);
}

int sched_schedule_new_thread(thread_t* thread, proc_t* proc, unsigned int flags) {
	if (!proc)
		proc = kernel_proc;
	if (!cpus && !(flags & SCHED_THIS_CPU))
		return -EAGAIN;

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

static _Noreturn void* idle(void* arg) {
	(void)arg;
	while (1)
		cpu_halt();
}

void sched_allow_ap_cpu_scheduling(void) {
	cpus = get_cpu_structs(&cpu_count);
}

void sched_init(void) {
	sched_proc_init();
	sched_thread_init();
	sched_lapic_timer_init();

	/* Ignore the return value here, since the AP's haven't been initialized yet */
	get_cpu_structs(&cpu_count);

	proc_t* kproc = sched_proc_alloc();
	assert(kproc);
	assert(kproc->pid == 0);
	kernel_proc = kproc;

	thread_t* this_thread = sched_thread_alloc();
	assert(this_thread != NULL);
	sched_schedule_new_thread(this_thread, kernel_proc, SCHED_THIS_CPU | SCHED_ALREADY_RUNNING);	

	struct cpu* cpu = current_cpu();
	cpu->thread_queue = this_thread;
	cpu->current_thread = this_thread;
	kproc->vmm_ctx = &cpu->vmm_ctx;

	thread_t* idle_thread = kthread_create(SCHED_THIS_CPU | SCHED_IDLE, idle, NULL);
	assert(idle_thread != NULL);
}
