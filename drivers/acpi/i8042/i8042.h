#pragma once

#include <lunar/core/interrupt.h>
#include <lunar/input/keyboard.h>

struct i8042_keyboard {
	i16 irq_number;
	struct isr* isr;
	struct irq* irq_struct;
	bool in_extended;
	struct keyboard* kbd;
};

enum i8042_io_ports {
	I8042_IO_DATA = 0x60,
	I8042_IO_COMMAND = 0x64,
	I8042_IO_STATUS = I8042_IO_COMMAND
};

enum i8042_commands {
	I8042_COMMAND_GET_CFG = 0x20,
	I8042_COMMAND_SET_CFG = 0x60,
	I8042_COMMAND_DISABLE_P2 = 0xa7,
	I8042_COMMAND_ENABLE_P2 = 0xa8,
	I8042_COMMAND_SELFTEST_P2 = 0xa9,
	I8042_COMMAND_SELFTEST = 0xaa,
	I8042_COMMAND_SELFTEST_P1 = 0xab,
	I8042_COMMAND_DISABLE_P1 = 0xad,
	I8042_COMMAND_ENABLE_P1 = 0xae,
	I8042_COMMAND_WRITE_P2 = 0xd4
};

enum i8042_port_commands {
	I8042_PORT_COMMAND_ENABLE_SCANNING = 0xf4,
	I8042_PORT_COMMAND_DISABLE_SCANNING = 0xf5,
	I8042_PORT_COMMAND_RESET_SELF_TEST = 0xff
};

enum i8042_port_responses {
	I8042_PORT_SELFTEST_OK = 0xaa,
	I8042_PORT_SELFTEST_FAIL = 0xfc
};

enum i8042_cfg_flags {
	I8042_CFG_P1_IRQ_ENABLED = (1 << 0),
	I8042_CFG_P2_IRQ_ENABLED = (1 << 1),
	I8042_CFG_P1_CLOCK = (1 << 4),
	I8042_CFG_P2_CLOCK = (1 << 5),
	I8042_CFG_TRANSLATION = (1 << 6)
};

enum i8042_responses {
	I8042_RESEND = 0xfe,
	I8042_SELFTEST_OK = 0x55,
	I8042_ACK = 0xfa
};

int i8042_setup_irq_kbd(struct i8042_keyboard* kbd);

void i8042_command_write(u8 command);
void i8042_data_write(u8 command);
u8 i8042_data_read(void);
int i8042_data_read_timeout(time_t ms, u8* out_value);
void i8042_flush_outbuffer(void);
void i8042_port_command(int port, u8 command);
int i8042_port_reset_selftest(int port);
int i8042_disable_scanning(int port);
int i8042_enable_scanning(int port);
