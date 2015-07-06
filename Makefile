#
# Copyright 2014, NICTA
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(NICTA_BSD)
#

lib-dirs:=libs

# The main target we want to generate
all: app-images cdrom

-include .config

include tools/common/project.mk

# Some example qemu invocations

# note: this relies on qemu after version 2.0
simulate-kzm:
	qemu-system-arm -nographic -M kzm \
		-kernel images/sel4test-driver-image-arm-imx31

# This relies on a helper script to build a bootable image
simulate-beagle:
	beagle_run_elf images/sel4test-driver-image-arm-omap3

simulate-ia32:
	qemu-system-i386 \
		-m 512 -nographic -kernel images/kernel-ia32-pc99 \
		-initrd images/sel4test-driver-image-ia32-pc99

run-nographics:
	qemu-system-i386 \
		-m 512 -nographic -kernel images/kernel-ia32-pc99 \
		-initrd images/$(apps)-image-ia32-pc99

debug run:
	qemu-system-i386 $(if $(subst run,,$@), -s -S) \
		-m 512 -serial stdio -kernel images/kernel-ia32-pc99 \
		-initrd images/$(apps)-image-ia32-pc99 

gdb:
	gdb -ex 'file $(IMAGE_ROOT)/$(apps)-image-ia32-pc99' \
		-ex 'target remote localhost:1234' \
		-ex 'break main' \
		-ex c

debug-cdrom run-cdrom: $(IMAGE_ROOT)/cdrom.iso
	qemu-system-i386 $(if $(subst run-cdrom,,$@), -s -S) \
		-m 512 -serial stdio \
		-cdrom $(IMAGE_ROOT)/cdrom.iso \
		-boot order=d	\
		-vga std

#$(IMAGE_ROOT)/cdrom.iso: cdrom

# make a basic cdrom image
cdrom: grub.cfg app-images
	@echo " [CD-ROM]"
	$(Q)mkdir -p $(IMAGE_ROOT)/cdrom/boot/grub
	$(Q)cp grub.cfg $(IMAGE_ROOT)/cdrom/boot/grub
	$(Q)cp $(IMAGE_ROOT)/kernel-ia32-pc99        $(IMAGE_ROOT)/cdrom/kernel
	$(Q)cp $(IMAGE_ROOT)/$(apps)-image-ia32-pc99 $(IMAGE_ROOT)/cdrom/app-image
	$(Q)grub-mkrescue -o $(IMAGE_ROOT)/cdrom.iso $(IMAGE_ROOT)/cdrom

clean-cdrom:
	@echo "[CLEAN] cdrom"
	$(Q)rm -rf $(IMAGE_ROOT)/cdrom.iso $(IMAGE_ROOT)/cdrom



.PHONY: help
help:
	@echo "sel4test - unit and regression tests for seL4"
	@echo " make menuconfig      - Select build configuration via menus."
	@echo " make <defconfig>     - Apply one of the default configurations. See"
	@echo "                        below for valid configurations."
	@echo " make silentoldconfig - Update configuration with the defaults of any"
	@echo "                        newly introduced settings."
	@echo " make                 - Build with the current configuration."
	@echo ""
	@echo "Valid default configurations are:"
	@ls -1 configs | sed -e 's/\(.*\)/\t\1/g'
