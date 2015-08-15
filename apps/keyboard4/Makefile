#
# Copyright (c) 2015, Josef Mihalits
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#

TARGETS := $(notdir $(SOURCE_DIR)).bin

ENTRY_POINT := _sel4_start

# required source files
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/local_libplatsupport/*.c))
ASMFILES := 

# required libraries
LIBS = c sel4 cpio elf sel4muslcsys sel4vka sel4allocman \
       platsupport sel4platsupport sel4vspace \
       sel4utils sel4simple utils
ifdef CONFIG_KERNEL_STABLE
LIBS += sel4simple-stable
else
LIBS += sel4simple-default
endif

# extra flags
CFLAGS += -Werror -ggdb -g3
ifdef CONFIG_X86_64
CFLAGS += -mno-sse
endif

include $(SEL4_COMMON)/common.mk
