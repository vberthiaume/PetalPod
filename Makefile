# Project Name
TARGET = PetalPod

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = source/PetalPod.cpp

# Library Locations
LIBDAISY_DIR = submodules/libDaisy
DAISYSP_DIR = submodules/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

