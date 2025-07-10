#pragma once

#define cpu_relax() __asm__ volatile("pause" : : : "memory")
#define cpu_halt() __asm__ volatile("hlt" : : : "memory")
