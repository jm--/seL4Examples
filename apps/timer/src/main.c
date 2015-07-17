/*
 * Copyright (c) 2015, Josef Mihalits
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 *
 */

#include <autoconf.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <platsupport/timer.h>    // time stuff
#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/plat/timer.h> // time stuff
#include <sel4utils/vspace.h>
#include <utils/time.h> // time stuff
#ifdef CONFIG_KERNEL_STABLE
#include <simple-stable/simple-stable.h>
#else
#include <simple-default/simple-default.h>
#endif



/* memory management: Virtual Kernel Allocator (VKA) interface and VSpace */
static vka_t vka;
static vspace_t vspace;

/*system abstraction */
static simple_t simple;

/* root task's BootInfo */
static seL4_BootInfo *bootinfo;

/* amount of virtual memory for the allocator to use */
#define VIRT_POOL_SIZE (BIT(seL4_PageBits) * 200)

/* static memory for the allocator to bootstrap with */
#define POOL_SIZE (BIT(seL4_PageBits) * 10)
static char memPool[POOL_SIZE];

/* for virtual memory bootstrapping */
static sel4utils_alloc_data_t allocData;
///

/* platsupport (periodic) timer */
seL4_timer_t* timer;

/* platsupport TSC based timer */
seL4_timer_t* tsc_timer;

/* async endpoint for periodic timer */
vka_object_t timer_aep;


// ======================================================================

/*
 * Initialize all main data structures.
 *
 * The code to initialize simple, allocman, vka, and vspace is modeled
 * after the "sel4test-driver" app:
 * https://github.com/seL4/sel4test/blob/master/apps/sel4test-driver/src/main.c
 */
static void
setup_system()
{
    /* initialize boot information */
    bootinfo  = seL4_GetBootInfo();

    /* initialize simple interface */
    simple_stable_init_bootinfo(&simple, bootinfo);
    //simple_default_init_bootinfo(simple, bootinfo);

    /* create an allocator */
    allocman_t *allocman;
    allocman = bootstrap_use_current_simple(&simple, POOL_SIZE, memPool);
    assert(allocman);

    /* create a VKA */
    allocman_make_vka(&vka, allocman);

    /* create a vspace */
    UNUSED int err;
    err = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&vspace,
            &allocData, seL4_CapInitThreadPD, &vka, bootinfo);
    assert(err == 0);

    /* fill allocator with virtual memory */
    void *vaddr;
    UNUSED reservation_t vres;
    vres = vspace_reserve_range(&vspace, VIRT_POOL_SIZE, seL4_AllRights,
            1, &vaddr);
    assert(vres.res);
    bootstrap_configure_virtual_pool(allocman, vaddr, VIRT_POOL_SIZE,
            seL4_CapInitThreadPD);
}


// gets called every 10 ms
static void
do_something()
{
    static int count = 0;

    count %= 100;
    if (count++ == 0) {
        //only every 100th (i.e., every second)

        //current timer value (in ns)
        //calls pit_get_time(const pstimer_t* device)
        uint64_t t = timer_get_time(timer->timer);
        printf("\ntimer value: %llu (ns) \n", t);

        uint64_t tsc_time = timer_get_time(tsc_timer->timer);
        printf("time since start: %llu (s) \n", tsc_time / NS_IN_S);
    } else {
        printf(".");
    }
    fflush(stdout);
}



/*
 * The code to set up the timer is modeled after the sel4test-tests app:
 * https://github.com/seL4/sel4test/tree/master/apps/sel4test-tests
 */

static void
init_timers()
{
    UNUSED int err = vka_alloc_async_endpoint(&vka, &timer_aep);
    assert(err == 0);

    // we don't have to mint the required caps (IRQHandler and seL4_CapIOPort)
    // manually; "simple" knows how to produce them when required
    timer = sel4platsupport_get_default_timer(&vka, &vspace, &simple, timer_aep.cptr);
    assert(timer != NULL);


    printf("init tsc_timer ---\n");
    fflush(stdout);
    // May generates some error messages (see tsc_calculate_frequency()).
    // Generates a TSC backed seL4_timer_t; the passed in "timer"
    // is only used for setting up the TSC timer (for calculateding freq).
    // All subsequent timer_get_time() calls read the TSC (and not the PIT).
    // So this provides an up counting clock.
    tsc_timer = sel4platsupport_get_tsc_timer(timer);
    assert(tsc_timer != NULL);
    printf("init tsc_timer --- done");

}


static void
wait_for_inerrupt()
{
    seL4_Wait(timer_aep.cptr, NULL);
    sel4_timer_handle_single_irq(timer);
}


static void
test_interrupt()
{
    //timer->timer is of type pstimer_t
    //calls: pit_periodic(timer->timer, 10 * NS_IN_MS),
    //which in turn calls configure_pit
    int error = timer_periodic(timer->timer, 10 * NS_IN_MS);
    assert(error == 0);

    //calls pit_start()
    timer_start(timer->timer);

    //(1) calls timer->timer->handle_irq, which by default
    //    points to: pit_handle_irq(const pstimer_t* device, uint32_t irq)
    //    which does nothing by default (at least for ia32)
    //(2) then calls: seL4_IRQHandler_Ack(data->irq);
    sel4_timer_handle_single_irq(timer);

    printf("\n");
    for (int i = 0; i < 1000; i++) {
        wait_for_inerrupt();
        do_something();
    }
    printf("\n");

    timer_stop(timer->timer);
    sel4_timer_handle_single_irq(timer);
}


int main(void)
{
    setup_system();

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &simple, &vka);

    printf("\n\n>>>>>>>>>> timer - test timer interrupt <<<<<<<<<< \n\n");

    // testing TSC
    uint64_t tsc = rdtsc_pure();
    printf("tsc=%llu\n", tsc);

    /* initialize the timers */
    init_timers();

    test_interrupt();
    return 0;
}
