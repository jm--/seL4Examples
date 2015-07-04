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


// creates irqhandler cap
// this code is modeled after init_timer_caps(), in orig driver
static void
init_keyboard_caps(env_t env, int irq)
{
    /* get the timer irq cap */
    seL4_CPtr cap;
    UNUSED int error = vka_cspace_alloc(&env->vka, &cap);
    assert(error == 0);

    vka_cspace_make_path(&env->vka, cap, &env->keyboardirq_path);
    error = simple_get_IRQ_control(&env->simple, irq, env->keyboardirq_path);
    assert(error == 0);
}

struct conserv_state {
    //srv_common_t commonState;
    //dev_irq_state_t irqState;

    /* Main console server data structures. */
    //dev_io_ops_t devIO;
    //ps_chardevice_t devSerial;
    //struct input_state devInput;
    //struct device_screen_state devScreen;

    //#ifdef PLAT_PC99
    ps_chardevice_t devKeyboard;
    //bool keyboardEnabled;
    //#endif

    //seL4_CPtr serialBadgeEP;
    //seL4_CPtr screenBadgeEP;
};
struct conserv_state conServ;



//#define REFOS_CDEPTH 32
//seL4_CPtr
//srv_mint(int badge, seL4_CPtr ep)
//{
//    assert(ep);
//    seL4_CPtr mintEP = ep+1;
//    int error = seL4_CNode_Mint (
//                   seL4_CapInitThreadCNode, mintEP, REFOS_CDEPTH,
//                   seL4_CapInitThreadCNode, ep, REFOS_CDEPTH,
//        seL4_CanWrite | seL4_CanGrant,
//        seL4_CapData_Badge_new(badge)
//    );
//    if (error != seL4_NoError) {
//        printf("Could not mint badge.");
//        exit(1);
//    }
//    return mintEP;
//}


seL4_CPtr
dev_handle_irq(ps_chardevice_t* dev, uint32_t irq, env_t env)
{
	printf ("irq=%d\n", irq);

	//create IRQHandler cap
	init_keyboard_caps(env, irq);
	seL4_CPtr handler    = env->keyboardirq_path.capPtr;

	//create end point
	seL4_BootInfo *info  = env->simple.data;
	seL4_CPtr cnodeCap   = seL4_CapInitThreadCNode; //the CPtr of the root task's CNode
	seL4_CPtr memoryCap  = info->untyped.start;
	seL4_CPtr irqEP      = info->empty.start + 5;
	int error = seL4_Untyped_RetypeAtOffset(memoryCap,
			seL4_AsyncEndpointObject, 0, 0, // type, newparam, size_bits,
			cnodeCap, 0, 0,                 // root, index, depth
			irqEP, 1);                      // offset, num
	if (error) {
		printf("seL4_Untyped_RetypeAtOffse failed:  %x\n", error);
		exit(1);
	}

	/* Assign AEP to the IRQ handler. */
	error = seL4_IRQHandler_SetEndpoint(handler, irqEP);
	if (error) {
		printf("dev_handle_irq : could not set notify aep for irq %u.\n", irq);
		exit(1);
	}

	//read once: not sure why, but it does not work without, it seems
	ps_cdev_getchar(dev);
	error = seL4_IRQHandler_Ack(handler);
	if (error) {
		printf("seL4_IRQHandler_Ack : failed %u.\n", error);
		exit(1);
	}

	return irqEP;	//return the async endpoint
}

//=============================================================================
int main(int argc, char *argv[])
{
    seL4_BootInfo *info = seL4_GetBootInfo();

//#ifdef SEL4_DEBUG_KERNEL
//    seL4_DebugNameThread(seL4_CapInitThreadTCB, "keyboard");
//#endif


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

    /* switch to a bigger, safer stack with a guard page
     * before starting the tests */
    //printf("Switching to a safer, bigger stack... ");
    // int res = (int)sel4utils_run_on_stack(&env.vspace, main_continued, NULL);
    // test_assert_fatal(res == 0);

    simple_print(&env.simple);
    printf("\n\n>>>>>>>>>> keyboard2 - keyboard test <<<<<<<<<< \n\n");
    printf("%d %s\n", argc, argv[0]);	//from libsel4platsupport

    /* Set up keyboard device. */
    printf("ps_cdev_init keyboard...\n");
    struct ps_io_ops    opsIO;
    sel4platsupport_get_io_port_ops(&opsIO.io_port_ops, &env.simple);
    ps_chardevice_t *devKeyboardRet;
    devKeyboardRet = ps_cdev_init(PC99_KEYBOARD_PS2, &opsIO, &conServ.devKeyboard);
    if (!devKeyboardRet) {
        printf("ERROR: could not initialize keyboard device.\n");
        assert(!"ps_cdev_init failed.");
        exit(1);
    }

    //Loop through every possible IRQ, and get the ones that the
    //input device needs to listen to. (keyboard is at IRQ 1)
    seL4_CPtr irqEP = seL4_CapNull;
    for (uint32_t i = 0; i < 256; i++) {
        if (ps_cdev_produces_irq(&conServ.devKeyboard, i)) {
            irqEP = dev_handle_irq(&conServ.devKeyboard, i, &env);
        }
    }

    for (;;) {
        seL4_MessageInfo_t msg = seL4_Wait(irqEP, NULL);
        printf("seL4_Wait: %d\n", (seL4_MessageInfo_get_label(msg) == seL4_NoFault));
        fflush(stdout);

        for (;;) {
            //int c = __arch_getchar();
            int c = ps_cdev_getchar(&conServ.devKeyboard);
            if (c == EOF) {
                //read till we get EOF
                break;
            }
            printf("You typed [%c]\n", c);
        }

        seL4_CPtr handler = env.keyboardirq_path.capPtr;
        int error = seL4_IRQHandler_Ack(handler);
        if (error) {
            printf("seL4_IRQHandler_Ack : failed %u.\n", error);
            exit(1);
        }
    }
}
