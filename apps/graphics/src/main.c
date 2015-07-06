/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/*
 * make menuconfig
 * 		Boot options -> enable linear graphics mode (default is 1024x768, 32)
 * 		select 320x200, 8 colors
 * make run-cdrom
 */

/* Include Kconfig variables. */
#include <autoconf.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

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
//=============================================================================

//seL4_Word vram = 0xB8000; //BIOS_PADDR_VIDEO_RAM_TEXT_MODE_START, EGA_TEXT_FB_BASE
seL4_Word vram = 0xA0000;	//BIOS_PADDR_VIDEO_RAM_START

void mapVideoRam(seL4_BootInfo *info) {
	seL4_CPtr cnodeCap = seL4_CapInitThreadCNode; //the CPtr of the root task's CNode
	seL4_Word emptyStart = info->empty.start; //start of the empty region in cnodeCap
	seL4_CPtr memoryCap = seL4_CapNull;  //CPtr to largest untyped memory object
	int res = seL4_NoError;

	//find largest memory area
	for (int maxbits = -1, i = 0; i < info->untyped.end - info->untyped.start;
			i++) {
		if (info->untypedSizeBitsList[i] > maxbits) {
			//we found a new largest memory object
			memoryCap = info->untyped.start + i;
			maxbits = info->untypedSizeBitsList[i];  //current bits become max
		}
	}

	//////////////////////////////////////////////////////////// map vram
	//find untyped device memory containing vram
	printf("\n--- Device Untyped Details ---\n");
	printf("Untyped Slot       Paddr      Bits\n");
	int offset = info->untyped.end - info->untyped.start;
	int numSlots = info->deviceUntyped.end - info->deviceUntyped.start;
	int i;
	for (i = 0; i < numSlots && info->untypedPaddrList[i + offset] < vram; i++) {
		//this loop is from simple_stable_print() in libsimple
		printf("%3d     0x%08x 0x%08x %d\n", i, info->deviceUntyped.start + i,
				info->untypedPaddrList[i + offset],
				info->untypedSizeBitsList[i + offset]);
	}

	i--;
	seL4_CPtr capUntypedStart = info->deviceUntyped.start + i;
	seL4_Word memUntypedStart = info->untypedPaddrList[i + offset];
	seL4_Word numPagesToVRam = (vram - memUntypedStart) / PAGE_SIZE; //to get to vram
	seL4_Word mumPagesVRam = ceil((double)320 * 200 / PAGE_SIZE); //actual vram pages
	//e.g. 320 * 200 / 4096 = 15.625, so we need 16 pages
	seL4_Word numPages = numPagesToVRam + mumPagesVRam;
	seL4_CPtr empty = emptyStart+1000; //empty slots, hopefully
	seL4_CPtr capPagesStart = empty;
	empty += numPages;

	printf("capUntypedStart = 0x%x\n", capUntypedStart);
	printf("memUntypedStart = 0x%x\n", memUntypedStart);
	printf("numPagesToVRam  = 0x%x\n", numPagesToVRam);
	printf("mumPagesVRam    = 0x%x\n", mumPagesVRam);
	printf("numPages        = %d\n", numPages);
	printf("memoryCap       = 0x%x\n", memoryCap);
	printf("PAGE_SIZE       = 0x%x = %d\n", PAGE_SIZE, PAGE_SIZE); //4K

	//get numPages of memory so to get hold of VRAM area
	res = seL4_Untyped_RetypeAtOffset(capUntypedStart,
			seL4_IA32_4K, 0, 0,           // type, ,size_bits
			cnodeCap, 0, 0,               // root, index, depth
			capPagesStart, numPages);     // offset, num
	printf("seL4_Untyped_RetypeAtOffset--seL4_IA32_4K: %x\n", res);

	//create a page table
	seL4_Word pt = empty++;
	res = seL4_Untyped_RetypeAtOffset(memoryCap,
			seL4_IA32_PageTableObject, 0,0,  // type, ,size_bits
			cnodeCap, 0, 0,                  // root, index, depth
			pt, 1);                          // offset, num
	printf("seL4_Untyped_RetypeAtOffset--seL4_IA32_PageTableObject: %x\n", res);

	//map page table to page directory
	res = seL4_IA32_PageTable_Map(pt, seL4_CapInitThreadPD, vram,
			seL4_IA32_Default_VMAttributes);
	printf("seL4_IA32_PageTable_Map: %x\n", res);

	//map vram pages to page table
	for (i = 0; i < mumPagesVRam; i++) {
		res = seL4_IA32_Page_Map(capPagesStart + numPagesToVRam + i,
				seL4_CapInitThreadPD, vram + PAGE_SIZE * i,
				seL4_AllRights, seL4_IA32_Default_VMAttributes);
		printf("seL4_IA32_Page_Map: %x (page %d)\n", res, i);
	}
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
    printf("\n\n>>>>>>>>>> graphics - write to VRAM <<<<<<<<<< \n\n");
    mapVideoRam(info);

    //write to mapped page
    char *p = (char*) vram; //top left pixel
    int i;
    for (i = 0; i < 320 * 200; i++) {
        *(p + i) = i % 42;
        if (i % PAGE_SIZE == 0) {
            printf("."); // if page is mapped, we'll see the dot
        }
    }
    printf("\n");
}
