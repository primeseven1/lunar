OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS {
	text PT_LOAD; /* Execute + Read */
	rodata PT_LOAD; /* Read only */
	data PT_LOAD; /* Write + Read */
	dynamic PT_DYNAMIC; /* Write + Read */
}

SECTIONS {
	. = 0xffffffff80000000;

	_ld_kernel_start = .;

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.text : {
		_ld_kernel_text_start = .;
		*(.text .text.*)
		_ld_kernel_text_end = .;
	} :text

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.rodata : {
		_ld_kernel_rodata_start = .;
		*(.rodata .rodata.*)
		_ld_kernel_rodata_end = .;
	} :rodata

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.data : {
		_ld_kernel_data_start = .;
		*(.data .data.*)

		. = ALIGN(8);
		_ld_kernel_modules_start = .;
		KEEP(*(.modules .modules.*))
		_ld_kernel_modules_end = .;

		KEEP(*(.limine_requests_start_marker))
		KEEP(*(.limine_requests))
		KEEP(*(.limine_requests_end_marker))
		_ld_kernel_data_end = .;
	} :data

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.dynamic : {
		_ld_kernel_dynamic_start = .;
		*(.dynamic)
		_ld_kernel_dynamic_end = .;
	} :data :dynamic

	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.bss : {
		_ld_kernel_bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		_ld_kernel_bss_end = .;
	} :data

	/DISCARD/ : {
		*(.eh_frame)
		*(.note .note.*)
	}

	_ld_kernel_end = .;
}
