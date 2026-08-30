/*
 * Tiny entry shim for the clean UIF DOL booter stub.
 */

	.section .text.start, "ax", @progbits
	.global _start
	.extern app_booter_main
	.extern _stack_top

_start:
	# Match the execution-state preconditions used by established Wii DOL
	# booters: interrupts disabled, floating point enabled, and MSR synchronized.
	mfmsr 3
	rlwinm 3, 3, 0, 17, 15
	ori 3, 3, 0x2000
	mtmsr 3
	isync

	# Use linker-reserved private memory rather than the argv handoff area.
	lis 1, _stack_top@ha
	addi 1, 1, _stack_top@l
	li 0, 0
	stwu 0, -64(1)
	bl app_booter_main

1:
	b 1b
