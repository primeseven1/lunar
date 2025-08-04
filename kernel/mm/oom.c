#include <crescent/core/panic.h>
#include "oom.h"

void out_of_memory(void) {
	panic("System is deadlocked on memory\n");
}
