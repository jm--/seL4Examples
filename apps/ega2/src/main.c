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

#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/io.h>

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
    /* platsupport I/O */
    ps_io_ops_t io_ops;
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

    //initialize env->io_ops
    error = sel4platsupport_new_io_ops(env->simple, env->vspace,
                    env->vka, &env->io_ops);
    assert(error == 0);
}


//=============================================================================

/*
 * Map video RAM with support libs.
 */
void* mapVideoRam(env_t env) {
	void* vram = ps_io_map(&env->io_ops.io_mapper, EGA_TEXT_FB_BASE,
			0x1000, false, PS_MEM_NORMAL);
	assert(vram != NULL);
	return vram;
}

void unmapVideoRam(env_t env, void* vram) {
	ps_io_unmap(&env->io_ops.io_mapper, vram, 0x1000);
}

void writeVideoRam(uint16_t* vram, int row) {
	printf("VRAM mapped at: 0x%x\n", (unsigned int) vram);

	const int width = 80;
	for (int col = 0; col < 80; col++) {
		vram[width * row + col] =  ('0' + col) | (col << 8);
	}
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
    printf("\n\n>>>>>>>>>> ega2 - write to VRAM <<<<<<<<<< \n\n");

    void* vram = mapVideoRam(&env);
    writeVideoRam((uint16_t*)vram, 5);

    //hmm, unmap seems not to work in that the page is not unmapped
    //from the real page table
    //unmapVideoRam(&env, vram);
    //writeVideoRam((uint16_t*)vram, 10);
}
