#include <crescent/lib/list.h>
#include <crescent/mm/heap.h>
#include <crescent/core/cpu.h>
#include "internal.h"

#define PBRR_PRIO_COUNT 32
#define PBRR_MIN_PRIO 0
#define PBRR_MAX_PRIO 31
#define PBRR_PRIO_GROUP_SHIFT 3
#define DEFAULT_SLICE_TICKS 10

struct rr_thread {
	struct thread* thread;
	unsigned long slice_left;
	int prio;
	struct list_node link;
};

struct rr_runqueue {
	struct list_head queues[PBRR_PRIO_COUNT];
	unsigned long active_bitmap;
	int prio_budget[PBRR_PRIO_COUNT];
};

/* Sanity check */
_Static_assert(PBRR_PRIO_COUNT <= (sizeof(((struct rr_runqueue*)0)->active_bitmap)) * 8, "pbrr bitmap too small");

static inline int prio_weight(int p) {
	int w = 1ul << (p >> PBRR_PRIO_GROUP_SHIFT);
	return w;
}

static inline void reset_budgets(struct rr_runqueue* rrq) {
	for (int p = 0; p < PBRR_PRIO_COUNT; p++)
		rrq->prio_budget[p] = prio_weight(p);
}

static int scale_prio(int posix_prio) {
	int span_posix = (SCHED_PRIO_MAX - SCHED_PRIO_MIN);
	int span_pbrr = (PBRR_MAX_PRIO - PBRR_MIN_PRIO);
	int scaled0 = ((posix_prio - SCHED_PRIO_MIN) * span_pbrr + span_posix / 2) / span_posix;
	int scaled = PBRR_MIN_PRIO + scaled0;
	assert(scaled >= PBRR_MIN_PRIO && scaled <= PBRR_MAX_PRIO);
	return scaled;
}

static void pbrr_thread_attach(struct runqueue* rq, struct thread* thread, int posix_prio) {
	(void)rq;
	int prio = scale_prio(posix_prio);
	struct rr_thread* rrt = thread->policy_priv;
	rrt->prio = prio;
	rrt->thread = thread;
	rrt->slice_left = DEFAULT_SLICE_TICKS;
}

static int pbrr_init(struct runqueue* rq) {
	rq->policy_priv = kzalloc(sizeof(struct rr_runqueue), MM_ZONE_NORMAL);
	if (!rq->policy_priv)
		return -ENOMEM;

	struct rr_runqueue* pbrq = rq->policy_priv;
	for (size_t i = 0; i < ARRAY_SIZE(pbrq->queues); i++)
		list_head_init(&pbrq->queues[i]);
	pbrq->active_bitmap = 0;
	reset_budgets(pbrq);
	return 0;
}

static int pbrr_enqueue(struct runqueue* rq, struct thread* thread) {
	struct rr_thread* rrt = thread->policy_priv;
	if (list_node_linked(&rrt->link))
		return -EALREADY;

	struct rr_runqueue* rrq = rq->policy_priv;
	list_add_tail(&rrq->queues[rrt->prio], &rrt->link);
	rrq->active_bitmap |= (1ul << rrt->prio);

	return 0;
}

static int pbrr_dequeue(struct runqueue* rq, struct thread* thread) {
	struct rr_thread* rrt = thread->policy_priv;
	if (!list_node_linked(&rrt->link))
		return -ENOENT;

	struct rr_runqueue* rrq = rq->policy_priv;
	int prio = rrt->prio;
	list_remove(&rrt->link);
	if (list_empty(&rrq->queues[prio]))
		rrq->active_bitmap &= ~(1ul << prio);

	return 0;
}

static int highest_ready_prio_budget(struct rr_runqueue* rrq) {
	unsigned long bm = rrq->active_bitmap;
	while (bm) {
		int p = (int)((sizeof(unsigned long) * 8) - 1 - __builtin_clzl(bm));
		if (rrq->prio_budget[p] > 0)
			return p;
		bm &= ~(1ul << p);
	}

	return -1;
}

static inline int highest_ready_prio(unsigned long bm) {
	if (!bm)
		return -1;
	return (int)((sizeof(unsigned long) * 8) - 1 - __builtin_clzl(bm));
}

static struct rr_thread* pop_head_and_maybe_clear(struct rr_runqueue* rrq, int prio) {
	struct list_head* head = &rrq->queues[prio];
	if (list_empty(head))
		return NULL;

	struct list_node* node = head->node.next;
	list_remove(node);

	struct rr_thread* rrt = list_entry(node, struct rr_thread, link);
	if (list_empty(head))
		rrq->active_bitmap &= ~(1ul << prio);

	return rrt;
}

static struct thread* pbrr_pick_next(struct runqueue* rq) {
	struct rr_runqueue* rrq = rq->policy_priv;

	struct thread* current = rq->current;
	struct rr_thread* crt = current->policy_priv;
	int state = atomic_load(&current->state, ATOMIC_ACQUIRE);
	bool runnable = (state == THREAD_RUNNING || state == THREAD_READY);

	/* Place current thread at the end of the list and mark the priority as active */
	if (runnable && !list_node_linked(&crt->link) && current != rq->idle) {
		int p = crt->prio;
		list_add_tail(&rrq->queues[p], &crt->link);
		rrq->active_bitmap |= 1ul << p;
	}

	/* Make sure budgets are reset */
	if (unlikely(rrq->prio_budget[0] == 0))
		reset_budgets(rrq);

	int p = highest_ready_prio_budget(rrq);
	if (p < 0) {
		reset_budgets(rrq); /* All active had empty budgets, so reset them */
		p = highest_ready_prio_budget(rrq);
		if (p < 0) /* No threads to schedule */ 
			return NULL;
	}

	/* Pop a thread from the priority queue */
	struct rr_thread* next_rrt = pop_head_and_maybe_clear(rrq, p);
	assert(next_rrt != NULL); /* Well, I guess the bitmap lied to us!! */

	if (rrq->prio_budget[p] > 0)
		rrq->prio_budget[p]--;

	next_rrt->slice_left = DEFAULT_SLICE_TICKS;
	return next_rrt->thread;
}

static int pbrr_change_prio(struct runqueue* rq, struct thread* thread, int posix_prio) {
	int prio = scale_prio(posix_prio);

	struct rr_runqueue* rrq = rq->policy_priv;
	struct rr_thread* rrt = thread->policy_priv;

	if (rrt->prio == prio)
		return 0;
	if (list_node_linked(&rrt->link)) {
		if (list_empty(&rrq->queues[prio]))
			rrq->active_bitmap &= ~(1ul << prio);
		list_remove(&rrt->link);
	}
	rrt->prio = prio;
	list_add_tail(&rrq->queues[prio], &rrt->link);
	rrq->active_bitmap |= (1ul << prio);

	return 0;
}

static bool pbrr_on_tick(struct runqueue* rq, struct thread* current) {
	struct rr_thread* rr_current = current->policy_priv;
	if (current != rq->idle) {
		if (rr_current->slice_left == 0)
			return true;
		if (--rr_current->slice_left == 0)
			return true;
	}

	return false;
}

static void pbrr_on_yield(struct runqueue* rq, struct thread* current) {
	(void)rq; /* pbrr_pick_next already adds to the end of the queue */
	struct rr_thread* rr_current = current->policy_priv;
	rr_current->slice_left = DEFAULT_SLICE_TICKS;
}

static const struct sched_policy_ops pbrr_ops = {
	.init = pbrr_init,
	.thread_attach = pbrr_thread_attach,
	.thread_detach = NULL,
	.enqueue = pbrr_enqueue,
	.dequeue = pbrr_dequeue,
	.pick_next = pbrr_pick_next,
	.change_prio = pbrr_change_prio,
	.on_tick = pbrr_on_tick,
	.on_yield = pbrr_on_yield
};

static struct sched_policy __sched_policy pbrr = {
	.name = "pbrr",
	.desc = "Priority-based round robin",
	.ops = &pbrr_ops,
	.thread_priv_size = sizeof(struct rr_thread)
};

/*
 * Round robin no priorities. This uses PBRR, but puts every thread into the same queue,
 * so it's effectively just round robin.
 */

static void rr_thread_attach(struct runqueue* rq, struct thread* thread, int posix_prio) {
	(void)posix_prio;
	pbrr_thread_attach(rq, thread, SCHED_PRIO_MAX);
}

static const struct sched_policy_ops rr_ops = {
	.init = pbrr_init,
	.thread_attach = rr_thread_attach,
	.thread_detach = NULL,
	.enqueue = pbrr_enqueue,
	.dequeue = pbrr_dequeue,
	.pick_next = pbrr_pick_next,
	.change_prio = NULL,
	.on_tick = pbrr_on_tick,
	.on_yield = pbrr_on_yield
};

static struct sched_policy __sched_policy rr = {
	.name = "rr",
	.desc = "Round robin",
	.ops = &rr_ops,
	.thread_priv_size = sizeof(struct rr_thread)
};
