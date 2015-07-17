#
# Copyright (c) 2015, Josef Mihalits
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
#

# standard seL4 Makefile for apps

# Targets
TARGETS := $(notdir $(SOURCE_DIR)).bin

# Set custom entry point as we are a rootserver and will not be
# started in a standard way
ENTRY_POINT := _sel4_start

# Source files required to build the target
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))

# libraries required by target
LIBS = c sel4 sel4muslcsys sel4vka sel4allocman \
       platsupport sel4platsupport sel4vspace \
       sel4utils sel4simple utils
ifdef CONFIG_KERNEL_STABLE
LIBS += sel4simple-stable
else
LIBS += sel4simple-default
endif

# extra cflag for sel4test
CFLAGS += -Werror  -ggdb -g3
ifdef CONFIG_X86_64
CFLAGS += -mno-sse
endif

include $(SEL4_COMMON)/common.mk
