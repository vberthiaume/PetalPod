# Project Name
TARGET = PetalPod

USE_DAISYSP_LGPL = 1
USE_FATFS = 1

# Sources
CPP_SOURCES = source/PetalPod.cpp

# optimization level. O0 is nothing, so use this for debug.
# -Os is optimize for space, -O1 to -O3 optimize level 1 to 3.
# See: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
# OPT = -O0

APP_TYPE = BOOT_SRAM
LDSCRIPT = STM32H750IB_sram_petalPod.lds

# Library Locations
LIBDAISY_DIR = submodules/libDaisy
DAISYSP_DIR = submodules/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
