/*
 * Copyright (c) 2015, Josef Mihalits
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 *
 */

    .text
    .align 32

/* Holds copy of VESA BIOS extension (VBE) protected mode interface,
   version 2.0. */
    .globl vbeTable
vbeTable:
    .space    4096

/*
 * Call VESA VBE function 7 via protected mode interface version 2.0.
 * This code assumes that we don't have to set register ES; i.e., the
 * BIOS does not require a memory mapped region. This works with QEMU and
 * VirtualBox as of Aug 24, 2015, but will stop working once the TSS issue
 * is fixed. http://sel4.systems/pipermail/devel/2015-August/000488.html
 * TODO: So to make this work in seL4, the "in" in the protected mode interface
 * have to be replaced somehow with seL4_IA32_IOPort_In8() system calls
 * or something along that line...
 */
    .globl    callVBE7
callVBE7:
    /* grab parameters before we manipulate stack:
       cx: bits 0-15 of display start address
       dx: bits 16-31 of display start address
       (the display start address is the offset from the start of frame
       buffer divided by 4) */
    movw 6(%esp), %dx
    movw 4(%esp), %cx
    /* save registers */
    pushl %ebx
    pushl %edi
    /* load table start address into edi*/
    leal vbeTable, %edi
    /* grab the 2 byte offset of function 7 located two bytes into table */
    movl $1, %eax
    movw (%edi, %eax, 2), %ax
	/* add offset to edi so that edi contains absolute
	   address of function entry */
    addl %eax, %edi
    /* specify the function we want to call */
    movl $0x4f07, %eax
    /* specify the sub function we want to use
       0x00 = set display start
       0x80 = set display start during vertical retrace */
    #movl $0x80, %ebx
    movl $0x00, %ebx
    /* call function (some code inside vbeTable doing port I/O) */
    call *%edi
	/* restore registers */
	popl %edi
    popl %ebx
    #movl %edx, %eax
    /* return value in %ax is passed through */
    ret



#
# test I/O without caps (should not work)
#
	.globl    tstport1
tstport1:
	#init with some dummy value
	movl $3, %eax
	#movw $0x338, %dx   #338 fails; 337 is okay
	movw $0x64, %dx   # 0x64 is keyboard buffer
	inb (%dx), %al
	ret
