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
    /* endpoint cap - waiting for IRQ */
    vka_object_t ep;
} chardev_t;


//////////////////////////////////////////////////////
/*  Types and prototypes are from
 *  keyboard_chardev.c and keyboard_ps2.h in the platsupport library.
 *  I guess one is not supposed to use them directly. :->
 */

#define KEYBOARD_PS2_STATE_NORMAL 0x1
#define KEYBOARD_PS2_STATE_IGNORE 0x2
#define KEYBOARD_PS2_STATE_EXTENDED_MODE 0x4
#define KEYBOARD_PS2_STATE_RELEASE_KEY 0x8
#define KEYBOARD_PS2_EVENTCODE_RELEASE 0xF0
#define KEYBOARD_PS2_EVENTCODE_EXTENDED 0xE0
#define KEYBOARD_PS2_EVENTCODE_EXTENDED_PAUSE 0xE1

typedef struct keycode_state {
    bool keystate[256];

    bool scroll_lock;
    bool num_lock;
    bool caps_lock;
    bool led_state_changed;

    /* Optional callback, called when a vkey has been pressed or released. */
    void (*handle_keyevent_callback)(int16_t vkey, bool pressed, void *cookie);
    /* Optional callback, called when a character has been typed. */
    void (*handle_chartyped_callback)(int c, void *cookie);
    /* Optional callback, called when num/scroll/caps lock LED state has changed. */
    void (*handle_led_state_changed_callback)(void *cookie);

} keycode_state_t;

typedef struct keyboard_key_event {
    int16_t vkey;
    bool pressed;
} keyboard_key_event_t;

struct keyboard_state {
    ps_io_ops_t ops;
    int state;
    int num_ignore;
    void (*handle_event_callback)(keyboard_key_event_t ev, void *cookie);
};

static struct keyboard_state my_kb_state;
static keycode_state_t my_kc_state;

/* prototypes of platsupport internal functions */
keyboard_key_event_t
keyboard_poll_ps2_keyevent(struct keyboard_state *state);

int16_t
keycode_process_vkey_event_to_char (keycode_state_t *s,
        int32_t vkey, bool pressed, void* cookie);

//////////////////////////////////////////////////////


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


static void
init_keyboard_state () {
    //mirroring platsupport's internal keyboard state; yikes
    my_kb_state.state = KEYBOARD_PS2_STATE_NORMAL;
    my_kb_state.ops = opsIO;   // keyboard.dev.ioops
    my_kb_state.num_ignore = 0;
    my_kb_state.handle_event_callback = NULL;

    memset(&my_kc_state, 0, sizeof(keycode_state_t));
    my_kc_state.num_lock = true;
}


static void
flush_keyboard(struct ps_chardevice *device);

static void
init_keyboard(chardev_t* dev) {
    int n = 0;
    ps_chardevice_t *ret;
    do {
        //ps_cdev_init() tries to set the keyboard to "scan code set 2"
        //(unfortunately, my laptop and external usb keyboard refuse and
        //operate in "scan code set 1" mode;
        //I blame buggy "legacy PS2" mode in BIOS)
        ret = ps_cdev_init(PC99_KEYBOARD_PS2, &opsIO, &dev->dev);
        // The code to initialize the keyboard in platsupport
        // does not return PS2_CONTROLLER_SELF_TEST_OK when a key is pressed
        // before or during initialization or something?
        if (n++ == 100) {
            // We retry a couple of times before giving up.
            printf("Failed to initialize PS2 keyboard.\n");
            exit(EXIT_FAILURE);
        }
    } while (ret == NULL);

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
    UNUSED int err = vka_alloc_async_endpoint(&vka, &dev->ep);
    assert(err == 0);


    /* Assign AEP to the IRQ handler. */
    err = seL4_IRQHandler_SetEndpoint(dev->handler.capPtr, dev->ep.cptr);
    assert(err == 0);

    init_keyboard_state();

    //flush and ack keyboard
    flush_keyboard(&dev->dev);
    err = seL4_IRQHandler_Ack(dev->handler.capPtr);
    assert(err == 0);

    printf("done init\n");
}


/*
 * Just print scan code; don't interpret state or anything
 * Pressing (and releasing) the 'a' key will generate:
 * 0x1e 0x9e      when in scan code set 1 (0x9e == 0x80 + 0x1e), and
 * 0x1c 0xf0 0x1c when in scan code set 2
 */
static int
check_keyboard(struct ps_chardevice *device) {
    static int n = 0;
    static const uint32_t PORT_STATUS = 0x64;
    static const uint32_t PORT_DATA = 0x60;

    ps_io_port_ops_t *port_ops = &my_kb_state.ops.io_port_ops;

    //check status register
    uint32_t res = 0;
    int error = ps_io_port_in(port_ops, PORT_STATUS, 1, &res);
    assert(!error);
    if ((res & 0x1) == 0) {
        //buffer is empty
        return -1;
    }

    //fetch one byte
    res = 0;
    error = ps_io_port_in(port_ops, PORT_DATA, 1, &res);
    assert(!error);
    printf("key: code = %3d = 0x%2x   (%d)\n", res, res, ++n);
    return res;
}


UNUSED static void
flush_keyboard(struct ps_chardevice *device) {
    int c = check_keyboard(device);
    while (c != -1) {
        printf("flush_keyboard\n");
        c = check_keyboard(device);
    }
    printf("done with flush_keyboard()\n");
}


int main()
{
    UNUSED int err;
    setup_system();

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &simple, &vka);

    printf("\n\n>>>>>>>>>> keyboard3 <<<<<<<<<< \n\n");

    chardev_t keyboard;
    init_keyboard(&keyboard);

    printf("press some keys\n");
    for (;;) {
        check_keyboard(&keyboard.dev);
    }
    return 0;
}
