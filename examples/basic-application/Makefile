########################################################################
## Makefile for basic_application
########################################################################

# Usage: make {all | clean} CONF={debug|release} VERBOSE={0|1}
# Default is the "all" target, debug build, and verbose output

VERBOSE ?= 1
ifeq ($(VERBOSE),1)
        ECHO :=
else
        ECHO := @
endif


#Default build type
CONF ?= debug

# the compiler
CXX = g++

# target EXE
TARGET_DIR = .
TARGET = basic_application

# Resource files
RES_TARGET = resources.C
RES_XML = .gresource.xml

# the version of GTK being used
GTK_VERSION_NUM = 3.0

# the base directory of EZGL
EZGL_DIR = ../..

# get the source and header files for the application and from EZGL
SRCS = $(wildcard ./*.cpp ./$(RES_TARGET) $(EZGL_DIR)/src/*.cpp)
HDRS = $(wildcard ./*.h $(EZGL_DIR)/include/ezgl/*.hpp)

# the GTK include directories. Runs "pkg-config --cflags gtk+-3.0" to get the include directories.
GTK_INCLUDE_DIRS := $(shell pkg-config --cflags gtk+-$(GTK_VERSION_NUM) x11)

# the GTK libraries. Runs "pkg-config --libs gtk+-3.0" to get the libraries to include for GTK.
GTK_LIBS := $(shell pkg-config --libs gtk+-$(GTK_VERSION_NUM) x11)

# set the include directories
INC_DIRS = $(EZGL_DIR)/include $(EZGL_DIR)/include/ezgl

# compiler flags
CXX_FLAGS = -g -Wall -std=c++14

ifeq (release, $(CONF))
        CXX_FLAGS += -O3
else ifeq (debug, $(CONF))
# Don't change anything
else
    $(error Invalid value for CONF: '$(CONF)', must be 'release' or 'debug'. Try 'make help' for usage)
endif


# GLIB resource compiler
GLIB_COMPILE_RESOURCES = $(shell pkg-config --variable=glib_compile_resources gio-2.0)

# resource files
resources = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=. --generate-dependencies $(RES_XML))

# 'make all' target
all: $(RES_TARGET) $(TARGET_DIR)/$(TARGET)

# create the exe
$(TARGET_DIR)/$(TARGET) : Makefile $(SRCS)
	$(ECHO) $(CXX) $(CXX_FLAGS) $(foreach D,$(INC_DIRS),-I$D) $(GTK_INCLUDE_DIRS) $(SRCS) $(GTK_LIBS) -o $(TARGET_DIR)/$(TARGET)

# create the resource file
$(RES_TARGET): $(RES_XML) $(resources)
	$(ECHO) $(GLIB_COMPILE_RESOURCES) --sourcedir=. --generate-source $(RES_XML) --target=$(RES_TARGET) 

# clean the EXE 
clean:
	$(ECHO) rm -f $(TARGET_DIR)/$(TARGET)
	$(ECHO) rm -f $(RES_TARGET)

help:
	@echo "Makefile for ezgl example program"
	@echo ""
	@echo "Usage: "
	@echo '    > make -j4'
	@echo "        Call the default make target (all)."
	@echo "        This builds the project executable: '$(TARGET)'."
	@echo "        Use -j4 option to do parallel builds."
	@echo "    > make clean"
	@echo "        Removes any generated files including exectuables "
	@echo "        and object files."
	@echo "    > make help"
	@echo "        Prints this help message."
	@echo ""
	@echo ""
	@echo "Configuration Variables: "
	@echo "    CONF={release | debug}"
	@echo "        Controls whether the build performs compiler optimizations"
	@echo "        to improve performance. Currently set to '$(CONF)'."
	@echo ""
	@echo "        With CONF=release compiler optimization is enabled ($(RELEASE_CXXFLAGS))."
	@echo ""
	@echo "        With CONF=debug compiler optimization is disabled to improve"
	@echo "        interactive debugging ($(DEBUG_CXXFLAGS))."

.PHONY: all clean help
