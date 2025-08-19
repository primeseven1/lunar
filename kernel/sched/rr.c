#include <crescent/sched/scheduler.h>
#include <crescent/core/cpu.h>
#include "internal.h"

static inline void enqueue_thread(struct runqueue* rq, struct thread* thread) {
	list_add_tail(&rq->queue, &thread->queue_link);
}

static inline void dequeue_thread(struct thread* thread) {
	list_remove(&thread->queue_link);
}

struct thread* rr_pick_next(struct runqueue* rq) {
	if (list_empty(&rq->queue))
		return rq->idle;

	struct list_node* node = rq->queue.node.next;
	return list_entry(node, struct thread, queue_link);
}

void rr_enqueue_thread(struct thread* thread) {
	struct runqueue* queue = &thread->target_cpu->runqueue;
	enqueue_thread(queue, thread);
}

void rr_dequeue_thread(struct thread* thread) {
	dequeue_thread(thread);
}
