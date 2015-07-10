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


struct env {
    /* An initialized vka that may be used by the test. */
    vka_t vka;
    /* virtual memory management interface */
    vspace_t vspace;
    /* abstracts over kernel version and boot environment */
    simple_t simple;
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

    /* fill the allocator with virtual memory */
    void *vaddr;
    virtual_reservation = vspace_reserve_range(&env->vspace,
                                               ALLOCATOR_VIRTUAL_POOL_SIZE, seL4_AllRights, 1, &vaddr);
    assert(virtual_reservation.res);
    bootstrap_configure_virtual_pool(allocman, vaddr,
                                     ALLOCATOR_VIRTUAL_POOL_SIZE, simple_get_pd(&env->simple));
}
//******************************************************************
//******************************************************************




// The code below is based on
// code and instructions provided by Mark Jones, PSU.

//================================================ thread 1
void thread0() {
  for (int i=0; i<10; i++) {
      printf("Thread0: %d\n", i);
      seL4_Yield();
  }
  printf("Thread0 is done\n");
  for (;;);
}

#define thread0SIZE 1024
seL4_Word thread0stack[thread0SIZE];
#define thread0stackend     (&thread0stack[thread0SIZE])


//================================================ thread 2
void thread1() {
  for (int i=0; i<10; i++) {
      printf("Thread1: %d\n", i);
      seL4_Yield();
  }
  printf("Thread1 is done\n");
  for (;;);
}

seL4_Word thread1stack[thread0SIZE];
#define thread1stackend     (&thread1stack[thread0SIZE])



void run_threads(seL4_BootInfo *info) {

  seL4_CPtr cnodeCap   = seL4_CapInitThreadCNode; //the CPtr of the root task's CNode
  seL4_Word emptyStart = info->empty.start;       //start of the empty region in cnodeCap
  seL4_Word emptyEnd   = info->empty.end;         //end of the empty region in cnodeCap
  seL4_CPtr memoryCap  = seL4_CapNull;            //CPtr to largest untyped memory object

  for (int maxbits = -1, i = 0; i < info->untyped.end - info->untyped.start; i++) {
      if (info->untypedSizeBitsList[i] > maxbits) {
          //we found a new largest memory object
          memoryCap = info->untyped.start + i;
          maxbits = info->untypedSizeBitsList[i];  //current bits become max
      }
  }

  printf("cnodeCap  : 0x%x\n", cnodeCap);
  printf("emptyStart: 0x%x\n", emptyStart);
  printf("emptyEnd  : 0x%x\n", emptyEnd);
  printf("memoryCap : 0x%x\n", memoryCap);


  int res;
  seL4_Word tcbs = emptyStart + 2;
#ifdef CONFIG_KERNEL_STABLE
  res = seL4_Untyped_RetypeAtOffset(memoryCap,
                            seL4_TCBObject, 0, 0, // type, ,size_bits
                            cnodeCap, 0, 0,       // root, index, depth
                            tcbs, 2);             // offset, num
#else
//  res = seL4_Untyped_Retype(memoryCap,
//                            seL4_TCBObject, 0, // type, size_bits
//                            cnodeCap, 0, 0,    // root, index, depth
//                            tcbs, 2);          // offset, num
#endif

  printf("Retyping result is %x\n", res);

  //============================================== setup thread0
  // configure TCB
  res = seL4_TCB_Configure(
      tcbs+0,
      0,                           // fault_ep
      255,                         // priority
      cnodeCap, seL4_NilData,      // cspace, data
      seL4_CapInitThreadVSpace, seL4_NilData, // vspace
      0, 0);                       // IPCBuffer
  printf("Configure 0 result: %x\n", res);

  // Read registers from this TCB:
  seL4_UserContext regs;
  res = seL4_TCB_ReadRegisters(tcbs+0, 0, 0, 2, &regs);
  printf("Read registers 0 result: %x\n", res);

  //And now we can write back those same register values,
  //modulo a couple of tweaks, in to our first new TCB:
  regs.eip = (seL4_Word) thread0;
  regs.esp = (seL4_Word) thread0stackend;

  res = seL4_TCB_WriteRegisters(tcbs+0,
                                0,       // resume_target
                                0,       // arch flags
                                2,       // count
                                &regs);
  printf("Write registers 0 result: %x\n", res);


  //============================================== setup thread1
  // configure TCB
  res = seL4_TCB_Configure(
      tcbs+1,
      0,                           // fault_ep
      255,                         // priority
      cnodeCap, seL4_NilData,      // cspace, data
      seL4_CapInitThreadVSpace, seL4_NilData, // vspace
      0, 0);                       // IPCBuffer
  printf("Configure 1 result: %x\n", res);

  // Read registers from this TCB:
  res = seL4_TCB_ReadRegisters(tcbs+1, 0, 0, 2, &regs);
  printf("Read registers 1 result: %x\n", res);

  //And now we can write back those same register values,
  //modulo a couple of tweaks, in to our first new TCB:
  regs.eip = (seL4_Word) thread1;
  regs.esp = (seL4_Word) thread1stackend;

  res = seL4_TCB_WriteRegisters(tcbs+1,
                                0,       // resume_target
                                0,       // arch flags
                                2,       // count
                                &regs);
  printf("Write registers 1 result: %x\n", res);


  //==============================================
  // start threads
  res = seL4_TCB_Resume(tcbs+0);
  printf("Resume 0 result: %x\n", res);
  res = seL4_TCB_Resume(tcbs+1);
  printf("Resume 1 result: %x\n", res);

  for (;;);

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

    printf("\n\n>>>>>>>>>> threads - start 2 threads <<<<<<<<<< \n\n");

    simple_print(&env.simple);
    run_threads(info);


    return 0;
}

