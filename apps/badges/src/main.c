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
//#include <sel4/sel4.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <platsupport/timer.h>

#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/plat/timer.h>
#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <sel4utils/process.h>

//#include <simple/simple.h>
#ifdef CONFIG_KERNEL_STABLE
#include <simple-stable/simple-stable.h>
#else
#include <simple-default/simple-default.h>
#endif

#include <utils/util.h>

#include <vka/object.h>
#include <vka/capops.h>
#include <vka/object_capops.h>

#include <vspace/vspace.h>


struct env {
    /* An initialized vka that may be used by the test. */
    vka_t vka;
    /* virtual memory management interface */
    vspace_t vspace;
    /* abstracts over kernel version and boot environment */
    simple_t simple;
    /* async endpoint*/
    vka_object_t aep;
};

#include <sel4test/test.h>
struct testcase* __start__test_case;
struct testcase* __stop__test_case;


/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

/* static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* static memory for virtual memory bootstrapping */
static sel4utils_alloc_data_t data;

/* environment encapsulating allocation interfaces etc */
static struct env env;




/* initialize our runtime environment */
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
    assert(error == 0);

    /* fill the allocator with virtual memory */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace,
                                               ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    assert(virtual_reservation.res);
    bootstrap_configure_virtual_pool(allocman, vaddr,
                                     ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&env->simple));

    // create endpoint
    error = vka_alloc_async_endpoint(&env->vka, &env->aep);
    assert(error == 0);
}
//******************************************************************
//******************************************************************




void testEP(env_t env) {

    UNUSED int error;

    cspacepath_t epb1;	//badged endpoint (derived from env->aep)
    seL4_CapData_t badge1 = seL4_CapData_Badge_new (1);
    //mint a badged endpoint with badge value 1
    error = vka_mint_object(&env->vka, &env->aep, &epb1, seL4_CanWrite, badge1);
    assert(error == 0);

    cspacepath_t epb2;	//badged endpoint (derived from env->aep)
    seL4_CapData_t badge2 = seL4_CapData_Badge_new (2);
    //mint a badged endpoint with badge value 2
    error = vka_mint_object(&env->vka, &env->aep, &epb2, seL4_CanWrite, badge2);
    assert(error == 0);

    seL4_Notify(epb1.capPtr, 0);
    seL4_Notify(epb2.capPtr, 0);

    seL4_Word senderBadge;
    seL4_Wait(env->aep.cptr, &senderBadge);
    printf("senderBadge= %d\n", senderBadge); //prints 3; the two badges 1|2
    //=======================================
//    uint32_t label = 0xF;
//    uint32_t capsUnwrapped = 0;
//    uint32_t extraCaps = 0;
//    uint32_t length = 3;
//    seL4_MessageInfo_t tag = seL4_MessageInfo_new(
//    		label, capsUnwrapped, extraCaps, length);
//    seL4_SetMR(0, 0); //0xAFF);
//    seL4_SetMR(1, 0); //0xBFF);
//    seL4_SetMR(2, 0xCFF);
//    seL4_SetMR(3, 0xDFF);
//    seL4_SetMR(4, 0xEFF);
//    seL4_SetMR(5, 0xFFF);
//    seL4_NBSend(epb1.capPtr, tag);
//    tag = seL4_Wait(env->aep.cptr, &senderBadge);
//    printf("senderBadge %d\n", senderBadge);
//
//    // in build/x86/pc99/libsel4/include/sel4/types_gen.h
//    label = seL4_MessageInfo_get_label(tag);
//    length = seL4_MessageInfo_get_length(tag);
//
//    printf("** label=%x \n", label);
//    printf("** length=%x \n", length);
//    printf("** seL4_GetMR0=%x \n", seL4_GetMR(0));
//    printf("** seL4_GetMR1=%x \n", seL4_GetMR(1));
//    printf("** seL4_GetMR2=%x \n", seL4_GetMR(2));
//    printf("** seL4_GetMR3=%x \n", seL4_GetMR(3));
//    printf("** seL4_GetMR4=%x \n", seL4_GetMR(4));
//    printf("** seL4_GetMR4=%x \n", seL4_GetMR(5));

}


int main(void)
{
    seL4_BootInfo *info = seL4_GetBootInfo();

    /* initialize libsel4simple*/
#ifdef CONFIG_KERNEL_STABLE
    simple_stable_init_bootinfo(&env.simple, info);
#else
    simple_default_init_bootinfo(&env.simple, info);
#endif

    /* initialize the environment */
    init_env(&env);

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &env.simple, &env.vka);

    printf("\n\n>>>>>>>>>> badges - test badged endpoints <<<<<<<<<< \n\n");

    simple_print(&env.simple);
    testEP(&env);
    return 0;
}

