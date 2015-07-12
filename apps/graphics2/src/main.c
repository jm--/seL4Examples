/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/* Note: requires "experimental" kernel and IA32
 *
 * HOWTO:
 * make menuconfig
 * 		=> goto  : Boot options -> enable linear graphics mode
 * 		=> select: select 640x480, 8 colors
 * make
 * make run-cdrom
 */

/* Include Kconfig variables. */
#include <autoconf.h>
#include <sel4/arch/bootinfo.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <sel4platsupport/io.h>

#include <sel4platsupport/platsupport.h>

#include <sel4utils/vspace.h>
#include <sel4utils/vspace_internal.h>
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
    /* platsupport I/O */
    ps_io_ops_t io_ops;
    /* root task's bootinfo */
    seL4_BootInfo *bootinfo;
    /* root task's IA32_BootInfo */
    seL4_IA32_BootInfo *bootinfo2;
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

//=============================================================================

/* initialize our runtime environment */
static void
init_env(env_t env)
{
    allocman_t *allocman;
    UNUSED reservation_t virtual_reservation;
    UNUSED int error;

    /* initialize boot information */
    env->bootinfo  = seL4_GetBootInfo();
    env->bootinfo2 = seL4_IA32_GetBootInfo();

    /* initialize libsel4simple, which abstracts away which kernel version
     * we are running on */
#ifdef CONFIG_KERNEL_STABLE
    //experimental kernel
    simple_stable_init_bootinfo(&env->simple, env->bootinfo);
#else
    //master kernel (may not work)
    simple_default_init_bootinfo(&env->simple, env->bootinfo);
#endif

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

    /* initialize env->io_ops */
    error = sel4platsupport_new_io_ops(env->simple, env->vspace,
                    env->vka, &env->io_ops);
    assert(error == 0);
}
//=============================================================================

void* mapVideoRam(env_t env, seL4_VBEModeInfoBlock* mib) {
	size_t size = mib->xRes * mib->xRes;
	void* vram = ps_io_map(&env->io_ops.io_mapper,
			mib->physBasePtr,
			size,
			false,
			PS_MEM_NORMAL);
	assert(vram != NULL);
	return vram;
}

/*
 * @param vid base address of frame buffer
 *             (this virtual address is not mapped 1:1)
 */
void writeVideoRam(void* vid, seL4_VBEModeInfoBlock* mib) {
	char* p = (char*) vid;
	size_t size = mib->xRes * mib->xRes;
	printf("pages: ");
	for (int i = 0; i < size; i++) {
		*(p + i) = i % 42; //generates some pattern
		if (i % PAGE_SIZE_4K == 0) {
			printf("."); // if page is mapped, we see a dot
		}
	}
	printf("\n");
}

void printVBE(seL4_IA32_BootInfo* bootinfo2) {
	seL4_VBEInfoBlock* ib      = &env.bootinfo2->vbeInfoBlock;
	seL4_VBEModeInfoBlock* mib = &env.bootinfo2->vbeModeInfoBlock;

	printf("\n");
	printf("vbeMode: 0x%x\n", bootinfo2->vbeMode);
	printf("VESA mode=%d; linear frame buffer=%d\n",
			bootinfo2->vbeMode & ( 1 << 8) ? 1:0,
					bootinfo2->vbeMode & ( 1 << 14)? 1:0);
	printf("vbeInterfaceSeg: 0x%x\n", bootinfo2->vbeInterfaceSeg);
	printf("vbeInterfaceOff: 0x%x\n", bootinfo2->vbeInterfaceOff);
	printf("vbeInterfaceLen: 0x%x\n", bootinfo2->vbeInterfaceLen);

	printf("ib->signature: %c%c%c%c\n",
			ib->signature[0],
			ib->signature[1],
			ib->signature[2],
			ib->signature[3]);
	printf("ib->version: %x\n", ib->version);
	//printf("ib->oemStringPtr: %x\n", ib->oemStringPtr);
	printf("mib->xRes: %d\n", mib->xRes);
	printf("mib->yRes: %d\n", mib->yRes);
	printf("mib->planes: %d\n", mib->planes);
	printf("mib->banks: %d\n", mib->banks);
	printf("mib->physBasePtr: %x\n", mib->physBasePtr);
}

//=============================================================================

int main()
{
	/* initialize the environment */
	init_env(&env);

	/* enable serial driver */
	platsupport_serial_setup_simple(NULL, &env.simple, &env.vka);

	simple_print(&env.simple);
	printf("\n\n>>>>>>>>>> graphics2 - write to VRAM <<<<<<<<<< \n\n");

	//seL4_VBEInfoBlock* ib      = &env.bootinfo2->vbeInfoBlock;
	seL4_VBEModeInfoBlock* mib = &env.bootinfo2->vbeModeInfoBlock;

	void* vid = mapVideoRam(&env, mib);
	printf("frame buffer mapped to vaddr: 0x%x \n", (unsigned int) vid);

	writeVideoRam(vid, mib);
	printVBE(env.bootinfo2);

	printf("\ndone\n");
}
