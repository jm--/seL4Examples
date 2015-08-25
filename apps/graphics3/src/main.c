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
#include <sel4/sel4.h>
#include <sel4/arch/bootinfo.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <sel4platsupport/io.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/vspace.h>
#include <vka/object_capops.h>
#include <sel4utils/stack.h>


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

/* root task's IA32_BootInfo */
seL4_IA32_BootInfo *bootinfo2;

/* amount of virtual memory for the allocator to use */
#define VIRT_POOL_SIZE (BIT(seL4_PageBits) * 200)

/* static memory for the allocator to bootstrap with */
#define POOL_SIZE (BIT(seL4_PageBits) * 10)
static char memPool[POOL_SIZE];

/* for virtual memory bootstrapping */
static sel4utils_alloc_data_t allocData;

/* platsupport IO */
static ps_io_ops_t opsIO;


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
    assert(bootinfo);

    bootinfo2 = seL4_IA32_GetBootInfo();
    assert(bootinfo2); // boot kernel in graphics mode

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

    /* initialize env->io_ops */
    err = sel4platsupport_new_io_ops(simple, vspace, vka, &opsIO);
    assert(err == 0);
}


static void*
mapVideoRam(ps_io_ops_t* ops, seL4_VBEModeInfoBlock* mib) {
    assert(ops);
    assert(mib);
    assert(mib->physBasePtr);
    int numpages = 2;
    assert(mib->imagePages >= numpages);
    /* size of one page */
    size_t size = mib->yRes * mib->linBytesPerScanLine;
    assert(size);
    void* vram = ps_io_map(&ops->io_mapper,
            mib->physBasePtr,
            size * numpages,
            false,
            PS_MEM_NORMAL);
    assert(vram != NULL);
    return vram;
}


/*
 * @param vram base address of frame buffer
 *             (this virtual address is not mapped 1:1)
 */
static void
writeVideoRam(void* vram, seL4_VBEModeInfoBlock* mib) {
    uint8_t* p = (uint8_t*) vram;
    int numpages = 2;
    size_t size = mib->yRes * mib->linBytesPerScanLine;
    for (int i = 0; i < size * numpages; i++) {
        /* paint first page white (0xff) and second page gray (0x66) */
        *(p + i) = i < size ? 0xff : 0x66;
    }
}


static void
printVBE(seL4_IA32_BootInfo* bootinfo2) {
    seL4_VBEInfoBlock* ib      = &bootinfo2->vbeInfoBlock;
    seL4_VBEModeInfoBlock* mib = &bootinfo2->vbeModeInfoBlock;

    printf("\n");
    printf("vbeMode: 0x%x\n", bootinfo2->vbeMode);
    printf("  VESA mode=%d; linear frame buffer=%d\n",
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
    printf("ib->version: %x\n", ib->version); //BCD

    printf ("===seL4_VBEModeInfoBlock===========\n");
    printf ("modeAttr: 0x%x\n", mib->modeAttr);
    printf ("winAAttr: 0x%x\n", mib->winAAttr);
    printf ("winBAttr: 0x%x\n", mib->winBAttr);
    printf ("winGranularity: %d\n", mib->winGranularity);
    printf ("winSize: %d\n", mib->winSize);
    printf ("winASeg: 0x%x\n", mib->winASeg);
    printf ("winBSeg: 0x%x\n", mib->winBSeg);
    printf ("winFuncPtr: 0x%x\n", mib->winFuncPtr);
    printf ("bytesPerScanLine: %d\n", mib->bytesPerScanLine);
    /* VBE 1.2+ */
    printf ("xRes: %d\n", mib->xRes);
    printf ("yRes: %d\n", mib->yRes);
    printf ("xCharSize: %d\n", mib->xCharSize);
    printf ("yCharSize: %d\n", mib->yCharSize);
    printf ("planes: %d\n", mib->planes);
    printf ("bitsPerPixel: %d\n", mib->bitsPerPixel);
    printf ("banks: %d\n", mib->banks);
    printf ("memoryModel: 0x%x\n", mib->memoryModel);
    printf ("bankSize: %d\n", mib->bankSize);
    printf ("imagePages: %d\n", mib->imagePages);
    printf ("reserved1: 0x%x\n", mib->reserved1);

    printf ("redLen: %d\n", mib->redLen);
    printf ("redOff: %d\n", mib->redOff);
    printf ("greenLen: %d\n", mib->greenLen);
    printf ("greenOff: %d\n", mib->greenOff);
    printf ("blueLen: %d\n", mib->blueLen);
    printf ("blueOff: %d\n", mib->blueOff);
    printf ("rsvdLen: %d\n", mib->rsvdLen);
    printf ("rsvdOff: %d\n", mib->rsvdOff);
    printf ("directColorInfo: %d\n", mib->directColorInfo);  /* direct color mode attributes */

    /* VBE 2.0+ */
    printf ("physBasePtr: %x\n", mib->physBasePtr);
    //printf ("reserved2: %d\n", mib->reserved2[6]);

    /* VBE 3.0+ */
    printf ("linBytesPerScanLine: %d\n", mib->linBytesPerScanLine);
    printf ("bnkImagePages: %d\n", mib->bnkImagePages);
    printf ("linImagePages: %d\n", mib->linImagePages);
    printf ("linRedLen: %d\n", mib->linRedLen); // bit mask for pixel
    printf ("linRedOff: %d\n", mib->linRedOff);
    printf ("linGreenLen: %d\n", mib->linGreenLen);
    printf ("linGreenOff: %d\n", mib->linGreenOff);
    printf ("linBlueLen: %d\n", mib->linBlueLen);
    printf ("linBlueOff: %d\n", mib->linBlueOff);
    printf ("linRsvdLen: %d\n", mib->linRsvdLen);
    printf ("linRsvdOff: %d\n", mib->linRsvdOff); // bit mask for pixel
    printf ("maxPixelClock: %d\n", mib->maxPixelClock);
    printf ("modeId: %d\n", mib->modeId);
    printf ("depth: %d\n", mib->depth);
}


void dumpMem(unsigned short *p, int len) {
    printf("======\r\n");
    for (int i = 0; i < len/2; i++) {
        printf("(%d)[%x]  ", i, p[i]);
    }
    printf("======\r\n");
}


int callVBE7(unsigned int);
int tstport1();
unsigned char vbeTable[4096];


void map_protectedModeInterface() {
    /* print again pertinent info */
    printf("VBE:\n");
    printf("vbeInterfaceSeg: 0x%x\n", bootinfo2->vbeInterfaceSeg);
    printf("vbeInterfaceOff: 0x%x\n", bootinfo2->vbeInterfaceOff);
    printf("vbeInterfaceLen: 0x%x\n", bootinfo2->vbeInterfaceLen);

    /* Combine real mode segment of table with real mode offset
     * of table to 32 bit address.
     * This is the physical address of protected mode interface*/
    unsigned int paddr = ((bootinfo2->vbeInterfaceSeg << 4)
            + bootinfo2->vbeInterfaceOff);
    printf("paddr          : 0x[%x]\n", (unsigned int)paddr);

    /* map physical address */
    void* vaddr = ps_io_map(&opsIO.io_mapper,
            paddr,
            bootinfo2->vbeInterfaceLen,
            false,
            PS_MEM_NORMAL);
    assert(vaddr != NULL);

    /* print current content */
    dumpMem( vaddr, bootinfo2->vbeInterfaceLen);

    /* save table (to a place where we have execute permissions) */
    unsigned char *pmIface = (unsigned char*) vaddr;
    for (int i = 0; i < bootinfo2->vbeInterfaceLen; i++) {
      vbeTable[i] = pmIface[i];
    }

    /* print copy too (should be same as above) */
    dumpMem((unsigned short*)vbeTable, bootinfo2->vbeInterfaceLen);
}


void *main_continued(void *arg UNUSED)
{
    printVBE(bootinfo2);

    seL4_VBEModeInfoBlock* mib = &bootinfo2->vbeModeInfoBlock;
    void* vid = mapVideoRam(&opsIO, mib);
    printf("frame buffer mapped to vaddr: 0x%x \n", (unsigned int) vid);

    /* draw an all white followed by an all gray page */
    writeVideoRam(vid, mib);

    map_protectedModeInterface();

    /* size in bytes of one page */
    size_t size = mib->yRes * mib->linBytesPerScanLine;
    /* function 7 wants div by 4 (always?) */
    size = size / 4;
    /*set new start address at 1/2 of first page */
    size = size / 2;

    unsigned int ret = callVBE7(size);
    /* expected result: top half of screen white (bottom from page 1) and
    * bottom half of screen gray (top of page 2) */
    printf("\n\n\n\r\nRET 0x[%x]\n", ret);

    printf("\ndone\n");
    for(;;);
    return NULL;
}

int main()
{
    UNUSED int err;
    setup_system();

    /* enable serial driver */
    platsupport_serial_setup_simple(NULL, &simple, &vka);

    printf("\n\n>>>>>>>>>> graphics3 <<<<<<<<<< \n\n");

    int res = (int)sel4utils_run_on_stack(&vspace, main_continued, NULL);
    assert(res == 0);
    return 0;
}
