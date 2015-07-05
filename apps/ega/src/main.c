/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */


/* Include Kconfig variables. */
#include <autoconf.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <arch_stdio.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <platsupport/timer.h>
#include <platsupport/chardev.h> //jm

#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/plat/timer.h>
#include <sel4platsupport/arch/io.h> //jm

#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <sel4utils/process.h>

#include <simple/simple.h>
#ifdef CONFIG_KERNEL_STABLE
#include <simple-stable/simple-stable.h>
#else
#include <simple-default/simple-default.h>
#endif

#include <utils/util.h>

#include <vka/object.h>
#include <vka/capops.h>

#include <vspace/vspace.h>

#include <sel4test/test.h>

//--------------------------------------------------------------------

struct env {
    /* An initialised vka that may be used by the test. */
    vka_t vka;
    /* virtual memory management interface */
    vspace_t vspace;
    /* abtracts over kernel version and boot environment */
    simple_t simple;
    cspacepath_t keyboardirq_path;	//jm
    /* path for the default timer irq handler */
    cspacepath_t irq_path;
    #ifdef CONFIG_ARCH_ARM
    /* frame for the default timer */
    cspacepath_t frame_path;
#elif CONFIG_ARCH_IA32
    /* io port for the default timer */
    seL4_CPtr io_port_cap;
#endif
    /* init data frame vaddr */
    //test_init_data_t *init;
    /* extra cap to the init data frame for mapping into the remote vspace */
    //seL4_CPtr init_frame_cap_copy;
};


/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

/* static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* static memory for virtual memory bootstrapping */
static sel4utils_alloc_data_t data;

/* environment encapsulating allocation interfaces etc */
static struct env env;


//libsel4test
struct testcase* __start__test_case;
struct testcase* __stop__test_case;

//--------------------------------------------------------------------
/* initialise our runtime environment */
static void
init_env(env_t env)
{
    allocman_t *allocman;
    UNUSED reservation_t virtual_reservation;
    UNUSED int error;

    /* create an allocator */
    allocman = bootstrap_use_current_simple(&env->simple, ALLOCATOR_STATIC_POOL_SIZE, allocator_mem_pool);
    assert(allocman);

    /* create a vka (interface for interacting with the underlying allocator) */
    allocman_make_vka(&env->vka, allocman);

    /* create a vspace (virtual memory management interface). We pass
     * boot info not because it will use capabilities from it, but so
     * it knows the address and will add it as a reserved region */
    error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&env->vspace,
                                                           &data, simple_get_pd(&env->simple), &env->vka, seL4_GetBootInfo());

    /* fill the allocator with virtual memory */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace,
                                               ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    assert(virtual_reservation.res);
    bootstrap_configure_virtual_pool(allocman, vaddr,
                                     ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&env->simple));
}



void demo(seL4_BootInfo *info) {

	seL4_CPtr cnodeCap = seL4_CapInitThreadCNode; //the CPtr of the root task's CNode
	seL4_Word emptyStart = info->empty.start; //start of the empty region in cnodeCap
	seL4_Word emptyEnd = info->empty.end;  //end of the empty region in cnodeCap
	seL4_CPtr memoryCap = seL4_CapNull;  //CPtr to largest untyped memory object

	for (int maxbits = -1, i = 0; i < info->untyped.end - info->untyped.start;
			i++) {
		if (info->untypedSizeBitsList[i] > maxbits) {
			//we found a new largest memory object
			memoryCap = info->untyped.start + i;
			maxbits = info->untypedSizeBitsList[i];  //current bits become max
		}
	}

	printf("** demo: 0x%x 0x%x 0x%x 0x%x\n", cnodeCap, emptyStart, emptyEnd,
			memoryCap);

	int res = seL4_NoError;
	////////////////////  map a 4MB page
	seL4_Word offs = emptyStart + 2;

//some memory mapping experiments (with handselected, hardcoded values)
	/*
	 //create a 4MB frame
	 seL4_CPtr cap4MB = 0x159;     //cap to a 4MB (22 bits) untyped memory area
	 seL4_Word vaddr = 0x00400000; //address of said area, needs to align to 4MB
	 res = seL4_Untyped_RetypeAtOffset(cap4MB,
	 seL4_IA32_LargePage, 0, 0, // type, ,size_bits
	 cnodeCap, 0, 0,            // root, index, depth
	 offs, 1);                  // offset, num
	 printf("seL4_Untyped_RetypeAtOffset--seL4_IA32_LargePage (4MB page): %x\n", res);

	 //map frame to page directory
	 res = seL4_IA32_Page_Map(offs ,seL4_CapInitThreadPD, vaddr, seL4_AllRights, seL4_IA32_Default_VMAttributes);
	 //res = seL4_IA32_Page_Map(offs ,offs+1, vaddr, seL4_AllRights, seL4_IA32_Default_VMAttributes);
	 printf("page map result is %x\n", res);

	 //now can access it
	 char* p = (char*) vaddr;
	 printf("current value at 0x%x is %d\n", vaddr, *p);
	 *p=65;
	 printf("new value is %d\n", *p);	//prints 65 (to serial output)
	 */
	//////////////////////////////////////////////////////////// map vram
	seL4_Word vram = 0xB8000;
	//seL4_Word vram = 0xA0000;
	seL4_CPtr empty = offs+1000;
	seL4_CPtr capFrame = empty;
	seL4_CPtr numPages = 57;
	empty += numPages;

	//create a 4KB frame
	seL4_CPtr capVram = 0x173;
	res = seL4_Untyped_RetypeAtOffset(capVram, seL4_IA32_4K, 0, 0, // type, ,size_bits
			cnodeCap, 0, 0,            // root, index, depth
			capFrame, numPages);                  // offset, num
	printf("seL4_Untyped_RetypeAtOffset--seL4_IA32_4K: %x\n", res);

	seL4_Word pt = empty++;
	res = seL4_Untyped_RetypeAtOffset(memoryCap, seL4_IA32_PageTableObject, 0,
			0, // type, ,size_bits
			cnodeCap, 0, 0,                  // root, index, depth
			pt, 1);                          // offset, num
	printf("seL4_Untyped_RetypeAtOffset--seL4_IA32_PageTableObject: %x\n", res);

	res = seL4_IA32_PageTable_Map(pt, seL4_CapInitThreadPD, vram,
			seL4_IA32_Default_VMAttributes);
	printf("seL4_IA32_PageTable_Map: %x\n", res);

//	for (int i = 0; 0 && i < numPages; i++) {
//		//res = seL4_IA32_Page_Map(capFrame, seL4_CapInitThreadPD, vram, seL4_AllRights, seL4_IA32_Default_VMAttributes);
//		res = seL4_IA32_Page_Map(capFrame, seL4_CapInitThreadPD,
//				0x80000 + 4096 * i, seL4_AllRights,
//				seL4_IA32_Default_VMAttributes);
//		printf("seL4_IA32_Page_Map: %x\n", res);
//	}
	res = seL4_IA32_Page_Map(capFrame + 56, seL4_CapInitThreadPD, vram,
			seL4_AllRights, seL4_IA32_Default_VMAttributes);
	printf("seL4_IA32_Page_Map: %x\n", res);

	char *p2 = (char*) vram;
	printf("VRAM: |%c|\n", *p2);

	for (int i = 0; i < 80 * 2; i++) {
		*(p2 + i + 80 * 10) = i;
	}
//  for (int i=0; i < 0x1000; i++) {
//	  *(p2 + i) = i % 256;
//  }
	//seL4_NoError
	////////////////////////////////////////////////////////////////
}


//=============================================================================
int main()
{
    seL4_BootInfo *info = seL4_GetBootInfo();

    /* initialize libsel4simple, which abstracts away which kernel version
     * we are running on */
#ifdef CONFIG_KERNEL_STABLE
    //experimental kernel
    simple_stable_init_bootinfo(&env.simple, info);
#else
    //master kernel (currently)
    simple_default_init_bootinfo(&env.simple, info);
#endif

    /* initialize the test environment - allocator, cspace manager, vspace manager, timer */
    init_env(&env);

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &env.simple, &env.vka);

    simple_print(&env.simple);
    printf("\n\n>>>>>>>>>> ega - write to VRAM <<<<<<<<<< \n\n");
    demo(info);

}
