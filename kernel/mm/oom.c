#include <lunar/core/panic.h>
#include "internal.h"

void out_of_memory(void) {
	panic("System is deadlocked on memory\n");
}
