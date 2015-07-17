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


typedef struct _chardev_t {
    /* platsupport char device */
    ps_chardevice_t dev;
    /* IRQHandler cap (with cspace path) */
    cspacepath_t handler;
    /* endpoint cap (with cspace path) device is waiting for IRQ */
    cspacepath_t ep;
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


// finalize device setup
// hook up endpoint (dev->ep) with IRQ of char device (dev->dev)
void set_devEp(chardev_t* dev) {
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

    /* Assign AEP to the IRQ handler. */
    UNUSED int err = seL4_IRQHandler_SetEndpoint(
            dev->handler.capPtr, dev->ep.capPtr);
    assert(err == 0);

    //read once: not sure why, but it does not work without it, it seems
    ps_cdev_getchar(&dev->dev);
    err = seL4_IRQHandler_Ack(dev->handler.capPtr);
    assert(err == 0);
}


void handle_cdev_event(char *msg, chardev_t* dev) {
    printf("handle_cdev_event(): %s\n", msg);
    for (;;) {
        //int c = __arch_getchar();
        int c = ps_cdev_getchar(&dev->dev);
        if (c == EOF) {
            //read till we get EOF
            break;
        }
        printf("You typed [%c]\n", c);
    }

    UNUSED int err = seL4_IRQHandler_Ack(dev->handler.capPtr);
    assert(err == 0);
}

int main()
{
    UNUSED int err;
    setup_system();

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &simple, &vka);

    printf("\n\n>>>>>>>>>> multi-irqs <<<<<<<<<< \n\n");
    simple_print(&simple);

    /* TODO: lots of duplicate code here ... */
    chardev_t serial1;
    chardev_t serial2;
    chardev_t keyboard;

    struct ps_io_ops    opsIO;
    sel4platsupport_get_io_port_ops(&opsIO.io_port_ops, &simple);
    ps_chardevice_t *ret;
    ret = ps_cdev_init(PS_SERIAL0, &opsIO, &serial1.dev);
    assert(ret != NULL);
    ret = ps_cdev_init(PS_SERIAL1, &opsIO, &serial2.dev);
    assert(ret != NULL);
    ret = ps_cdev_init(PC99_KEYBOARD_PS2, &opsIO, &keyboard.dev);
    assert(ret != NULL);

    ///////////////////

    /* async endpoint*/
    vka_object_t aep;

    // create endpoint
    err = vka_alloc_async_endpoint(&vka, &aep);
    assert(err == 0);

    seL4_CapData_t badge1 = seL4_CapData_Badge_new (1);
    //mint a badged endpoint with badge value 1
    err = vka_mint_object(&vka, &aep, &serial1.ep, seL4_AllRights, badge1);
    assert(err == 0);

    seL4_CapData_t badge2 = seL4_CapData_Badge_new (2);
    //mint a badged endpoint with badge value 2
    err = vka_mint_object(&vka, &aep, &serial2.ep, seL4_AllRights, badge2);
    assert(err == 0);

    seL4_CapData_t badge3 = seL4_CapData_Badge_new (4);
    //mint a badged endpoint with badge value 4
    err = vka_mint_object(&vka, &aep, &keyboard.ep, seL4_AllRights, badge3);
    assert(err == 0);

    ///////////////////
    set_devEp(&serial1);
    set_devEp(&serial2);
    set_devEp(&keyboard);

    for (;;) {
        seL4_Word sender_badge;
        printf("waiting:\n");
        UNUSED seL4_MessageInfo_t msg = seL4_Wait(aep.cptr, &sender_badge);

        printf("seL4_Wait returned with badge: %d\n", sender_badge);

        if (sender_badge & 1) {
            handle_cdev_event("serial1", &serial1);
        }
        if (sender_badge & 2) {
            handle_cdev_event("serial2", &serial2);
        }
        if (sender_badge & 4) {
            handle_cdev_event("keyboard", &keyboard);
        }
    }

    return 0;
}

