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

#include <cpio/cpio.h>	//jm

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <platsupport/timer.h>

#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/plat/timer.h>
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

//#include "test.h"

struct env {
    /* An initialised vka that may be used by the test. */
    vka_t vka;
    /* virtual memory management interface */
    vspace_t vspace;
    /* abtracts over kernel version and boot environment */
    simple_t simple;
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

#include <sel4test/test.h>

extern char _cpio_archive[]; //linked in via archive.o

//libsel4test
struct testcase* __start__test_case;
struct testcase* __stop__test_case;

#define TESTS_APP "sel4test-tests"

/* ammount of untyped memory to reserve for the driver (32mb) */
#define DRIVER_UNTYPED_MEMORY (1 << 25)
/* Number of untypeds to try and use to allocate the driver memory.
 * if we cannot get 32mb with 16 untypeds then something is probably wrong */
#define DRIVER_NUM_UNTYPEDS 16

/* dimensions of virtual memory for the allocator to use */
#define ALLOCATOR_VIRTUAL_POOL_SIZE ((1 << seL4_PageBits) * 100)

/* static memory for the allocator to bootstrap with */
#define ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 10)
static char allocator_mem_pool[ALLOCATOR_STATIC_POOL_SIZE];

/* static memory for virtual memory bootstrapping */
static sel4utils_alloc_data_t data;

/* environment encapsulating allocation interfaces etc */
static struct env env;
/* the number of untyped objects we have to give out to processes */
//static int num_untypeds;
/* list of untypeds to give out to test processes */
//static vka_object_t untypeds[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
/* list of sizes (in bits) corresponding to untyped */
//static uint8_t untyped_size_bits_list[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];


/*
 * Test cases are defined in test_cases.c, an autogenerated
 * file that is  built by extracting the test symbols
 * from the sel4test-tests application binary
 */
extern testcase_t *test_cases[];

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
    //sets: vka->data = allocman and a bunch of fun pointers;
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


//=======================================================================
//=======================================================================
//=======================================================================

void processFile(const char* filename) {
    unsigned long filesize;
    char* file = cpio_get_file(_cpio_archive, filename, &filesize);
    if (file == NULL) {
        printf("file [%s] not found :(\n", filename);
    } else {
        printf("file [%s] filesize=%ld, content=[", filename, filesize);
        for (int i = 0; i < filesize; i++) {
            printf("%c", file[i]);
        }
        printf("]\n");
    }
}


int main(void)
{
    seL4_BootInfo *info = seL4_GetBootInfo();


    /* initialize libsel4simple, which abstracts away which kernel version
     * we are running on */
#ifdef CONFIG_KERNEL_STABLE
    //assign:  simple->data = info;
    // and a bunch of function pointers, e.g: simple->print = &simple_stable_print;
    //usage example: simple_print(&env.simple);
    simple_stable_init_bootinfo(&env.simple, info);
#else
    simple_default_init_bootinfo(&env.simple, info);
#endif

    /*  initialize the test environment - allocator, cspace manager, vspace manager, timer */
    init_env(&env);

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &env.simple, &env.vka);


    unsigned int addr = (unsigned int) _cpio_archive;
    printf("Address of _cpio_archive: 0x%x, addr mod 4 = %d\n", addr, addr % 4);
    processFile("file1.txt");
    processFile("file2.txt");

    return 0;
}
