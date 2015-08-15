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
#include <sel4/arch/bootinfo.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <platsupport/chardev.h>
#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/timer.h>
#include <sel4platsupport/arch/io.h>
#include <sel4utils/vspace.h>
#include <vka/object_capops.h>

#ifdef CONFIG_KERNEL_STABLE
#include <simple-stable/simple-stable.h>
#else
#include <simple-default/simple-default.h>
#endif

#include "local_libplatsupport/keyboard_ps2.h"
#include "local_libplatsupport/keyboard_chardev.h"

typedef struct _chardev_t {
    /* platsupport char device */
    ps_chardevice_t dev;
    /* IRQHandler cap (with cspace path) */
    cspacepath_t handler;
    /* endpoint cap - waiting for IRQ */
    vka_object_t ep;
} chardev_t;


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

/* platsupport IO */
static struct ps_io_ops opsIO;


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

    err = sel4platsupport_get_io_port_ops(&opsIO.io_port_ops, &simple);
    assert(err == 0);

}


// creates IRQHandler cap "handler" for IRQ "irq"
static void
get_irqhandler_cap(int irq, cspacepath_t* handler)
{
    seL4_CPtr cap;
    // get a cspace slot
    UNUSED int err = vka_cspace_alloc(&vka, &cap);
    assert(err == 0);

    // convert allocated cptr to a cspacepath, for use in
    // operations such as Untyped_Retype
    vka_cspace_make_path(&vka, cap, handler);

    // exec seL4_IRQControl_Get(seL4_CapIRQControl, irq, ...)
    // to get an IRQHandler cap for IRQ "irq"
    err = simple_get_IRQ_control(&simple, irq, *handler);
    assert(err == 0);
}


static const int my_keyboard_irqs[] = {KEYBOARD_PS2_IRQ, -1};
static const struct dev_defn my_keyboard_def = {
        .id      = PC99_KEYBOARD_PS2,
        .paddr   = 0,
        .size    = 0,
        .irqs    = my_keyboard_irqs,
        .init_fn = &keyboard_cdev_init
};


static void
init_keyboard(chardev_t* dev) {
    int n = 0;
    int err = 0;
    do {
        //ret = ps_cdev_init(PC99_KEYBOARD_PS2, &opsIO, &dev->dev);
        err = keyboard_cdev_init(&my_keyboard_def, &opsIO, &dev->dev);
        if (n++ == 100) {
            // We retry a couple of times before giving up.
            printf("Failed to initialize PS2 keyboard.\n");
            exit(EXIT_FAILURE);
        }
    } while (err);

    // Loop through all IRQs and get the one device needs to listen to
    // We currently assume there it only needs one IRQ.
    int irq;
    for (irq = 0; irq < 256; irq++) {
        if (ps_cdev_produces_irq(&dev->dev, irq)) {
            break;
        }
    }
    printf ("irq=%d\n", irq);

    //create IRQHandler cap
    get_irqhandler_cap(irq, &dev->handler);

    // create endpoint
    err = vka_alloc_async_endpoint(&vka, &dev->ep);
    assert(err == 0);


    /* Assign AEP to the IRQ handler. */
    err = seL4_IRQHandler_SetEndpoint(dev->handler.capPtr, dev->ep.cptr);
    assert(err == 0);

    //flush and ack keyboard
    keyboard_flush(&opsIO);
    err = seL4_IRQHandler_Ack(dev->handler.capPtr);
    assert(err == 0);

    printf("done init\n");
}


int main()
{
    UNUSED int err;
    setup_system();

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &simple, &vka);

    printf("\n\n>>>>>>>>>> keyboard4 <<<<<<<<<< \n\n");

    chardev_t keyboard;
    init_keyboard(&keyboard);

    for (;;) {
        // :) https://vimeo.com/37714632
        printf("to start press any key\n"); //well, any key except extended keys
        int scanset = keyboard_detect_scanset(&opsIO);
        if (scanset == 1 || scanset == 2) {
            keyboard_set_scanset(scanset);
            printf("scanset %d detected\n", scanset);
            break;
        }
        keyboard_flush(&opsIO);
    }

    for (;;) {
        //test key event
        printf("press some keys; press 'k' to change test\n");
        for (;;) {
            int16_t vkey;
            int pressed = keyboard_poll_keyevent(&vkey);
            if (vkey == 'K' && !pressed) {
                break;
            }
        }

        //test char
        printf("press some keys; press 'l' to change test\n");
        int c;
        do {
            c = ps_cdev_getchar(&keyboard.dev);
            if (c != EOF) {
                printf("%d 0x%x [%c]\n", c, c, c);
            }
        } while (c !='l' && c !='L');
    }
    return 0;
}
