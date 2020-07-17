# -*- Mode: makefile-gmake -*-

.PHONY: all debug release

#
# Required packages
#

PKGS = gio-unix-2.0 glib-2.0 libwbxml2 libgofono libglibutil
LIB_PKGS = $(PKGS)

#
# Default target
#

all: debug release

#
# Sources
#

SRC = \
 log.c \
 main.c \
 provisioning-decoder.c \
 provisioning-ofono.c
GEN_SRC = \
 org.nemomobile.provisioning.c

#
# Directories
#

SRC_DIR = src
BUILD_DIR = build
SPEC_DIR = .
GEN_DIR = $(BUILD_DIR)
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Code coverage
#

ifndef GCOV
GCOV = 0
endif

ifneq ($(GCOV),0)
CFLAGS += --coverage
LDFLAGS += --coverage
endif

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall -Wno-unused-parameter
INCLUDES = -I. -I$(GEN_DIR)
BASE_FLAGS = -fPIC $(CFLAGS)
FULL_CFLAGS = $(BASE_FLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(BASE_FLAGS)
DEBUG_FLAGS = -g
RELEASE_FLAGS =

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

ifdef FILEWRITE
FULL_CFLAGS += -DFILEWRITE='"$(FILEWRITE)"'
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
DEBUG_LDFLAGS = $(LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(LDFLAGS) $(RELEASE_FLAGS)

LIBS = $(shell pkg-config --libs $(LIB_PKGS))
DEBUG_LIBS = $(LIBS)
RELEASE_LIBS = $(LIBS)

#
# Files
#

DEBUG_OBJS = \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
GEN_FILES = $(GEN_SRC:%=$(GEN_DIR)/%)
.PRECIOUS: $(GEN_FILES)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(GEN_FILES): | $(GEN_DIR)
$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

EXE = provisioning-service
DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

generate: $(GEN_FILES)

debug: $(DEBUG_DEPS) $(DEBUG_EXE)

release: $(RELEASE_DEPS) $(RELEASE_EXE)

clean:
	make -C test clean
	rm -fr test/coverage/results test/coverage/*.gcov
	rm -fr $(BUILD_DIR) RPMS installroot
	rm *~ $(SRC_DIR)/*~

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_EXE_DEPS) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_EXE_DEPS) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

#
# Install
#

install : $(RELEASE_EXE)
	mkdir -p $(DESTDIR)/usr/libexec
	mkdir -p $(DESTDIR)/etc/dbus-1/system.d
	mkdir -p $(DESTDIR)/usr/lib/systemd/system
	mkdir -p $(DESTDIR)/usr/share/dbus-1/system-services
	mkdir -p $(DESTDIR)/etc/ofono/push_forwarder.d
	cp $(RELEASE_EXE) $(DESTDIR)/usr/libexec/
	cp $(SRC_DIR)/provisioning.conf $(DESTDIR)/etc/dbus-1/system.d/
	cp $(SRC_DIR)/org.nemomobile.provisioning.service $(DESTDIR)/usr/share/dbus-1/system-services/
	cp ofono-provisioning.conf $(DESTDIR)/etc/ofono/push_forwarder.d/
	cp $(SRC_DIR)/dbus-org.nemomobile.provisioning.service $(DESTDIR)/usr/lib/systemd/system/
