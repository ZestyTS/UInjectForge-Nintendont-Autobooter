OUTPUT_ARCH(powerpc:common)
ENTRY(_start)
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}


SECTIONS
{
	. = 0x92F00000;

	.text : ALIGN(32)
	{
		KEEP(*(.text.start))
		*(.text*)
		*(.rodata*)
	} :text

	.data : ALIGN(32)
	{
		*(.data*)
	} :data

	.bss : ALIGN(32)
	{
		*(.bss*)
		*(COMMON)
	} :data

	. = ALIGN(64);
	.stack (NOLOAD) :
	{
		. += 0x8000;
		_stack_top = .;
	} :data

	/DISCARD/ :
	{
		*(.comment*)
		*(.eh_frame*)
		*(.note*)
	}
}
