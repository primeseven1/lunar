#include <lunar/init.h>
#include <lunar/printk.h>
#include <lunar/panic.h>
#include <lunar/sched.h>
#include <lunar/vmm.h>
#include <lunar/module.h>
#include <lunar/vfs.h>
#include <lunar/input.h>

#include <arch/tlb.h>
#include <arch/irq_flags.h>

#include <acpi/driver.h>
#include <acpi/sleep.h>

#include "initrd.h"

static void wait_on_bsp_dependency(const char* curr_name, struct init_task* dependency) {
	bool logged = false;
	while (!cpumask_test(&dependency->done, 0)) {
		if (!logged) {
			printk(PRINTK_INFO "init: %s waiting on %s\n",
					curr_name, dependency->name);
			logged = true;
		}
		arch_cpu_relax();
	}
}

void init_task_run(struct init_task* task) {
	u32 sched_id = current_cpu()->runqueue.sched_id;
	if ((sched_id != 0 && task->scope == INIT_TASK_SCOPE_BSP) || (sched_id == 0 && task->scope == INIT_TASK_SCOPE_AP))
		return;

	if (cpumask_test(&task->done, sched_id))
		return;
	if (cpumask_test(&task->running, sched_id))
		panic("recursive init dependency: %s", task->name);

	cpumask_set(&task->running, sched_id, true);
	for (struct init_task** deps = task->dependencies; *deps; deps++) {
		if ((*deps)->scope == INIT_TASK_SCOPE_BSP && sched_id != 0) {
			wait_on_bsp_dependency(task->name, *deps);
		} else {
			if ((task->scope == INIT_TASK_SCOPE_BSP || task->scope == INIT_TASK_SCOPE_BSP_AP) && (*deps)->scope == INIT_TASK_SCOPE_AP) {
				panic("init task %s (scope %d) relies on init task %s (scope %d)",
						task->name, task->scope, (*deps)->name, (*deps)->scope);
			}
			init_task_run(*deps);
		}
	}

	task->func();
	cpumask_set(&task->done, sched_id, true);
	cpumask_set(&task->running, sched_id, false);
}

extern struct init_task _ld_kernel_initt_start[];
extern struct init_task _ld_kernel_initt_end[];

static inline void init_run_all_tasks(void) {
	for (struct init_task* t = _ld_kernel_initt_start; t < _ld_kernel_initt_end; t++)
		init_task_run(t);
}

_Noreturn void kernel_ap_main(void) {
	init_run_all_tasks();
	arch_tlb_flush_all();
	smp_register_cpu();
	smp_init_complete();
	smp_init_wait_for_all();

	preempt_init();
	local_irq_enable();
	sched_thread_exit();
}

INIT_TASK_DECLARE(printk_init_task, term_init_task);

_Noreturn void kernel_main(void) {
	init_task_run(&printk_init_task); /* Set loglevel */
	init_task_run(&term_init_task); /* See printk messages on the screen */
	init_run_all_tasks();
	smp_init_bsp_wait_for_others();

	/*
	 * smp_init_bsp_wait_for_others() will ensure no mutexes are held the time of transition,
	 * and if mutexes are held, somebody (probably me) really fucked up.
	 */
	mutex_disable_spinlock();
	arch_tlb_flush_all();
	tlb_shootdown_init();
	smp_init_complete();

	/* All CPU's are running at this point */
	preempt_init();
	local_irq_enable();
	module_load_builtins();
	acpi_drivers_load();
	printk(PRINTK_CRIT "init: kernel_main() thread ended!\n");

	vfs_mount_root();
	initrd_init();

	size_t total_page_count, free_page_count;
	mm_get_free_pages(&total_page_count, &free_page_count);
	size_t used_pages = total_page_count - free_page_count;
	printk("Memory usage: %zu/%zu pages (%zu/%zu KB used) after init\n",
			used_pages, total_page_count,
			(used_pages * PAGE_SIZE) / 1024, (total_page_count * PAGE_SIZE) / 1024);

	/* Will get removed eventually */
	keyboard_reader_thread_init();

	sched_thread_exit();
}
