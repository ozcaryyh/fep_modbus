# 	    Makefile for tcp freemodbus library
#
#	 	Copyright (c) 2007 Steven Guo <gotop167@163.com>
#
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# platform dependencies
# ---------------------------------------------------------------------------
ifneq ($(strip $(shell gcc -v 2>&1 |grep "cygwin")),)
	CYGWIN_BUILD = YES
endif
CC	        = gcc
CXX	        = g++
OBJCOPY	    = objcopy
INSIGHT     = /opt/insight-x86/bin/insight

# ---------------------------------------------------------------------------
# project specifics
# ---------------------------------------------------------------------------
CFLAGS	    =  -O0 -fPIC -g -Wall -Iinclude -Itcp
LDFLAGS     =  -shared
ifeq ($(CYGWIN_BUILD),YES)
else
#LDFLAGS     += -lpthread
LDFLAGS     += -pthread
CFLAGS      += -pthread
endif

LIBRARIES   = libmodbustcp.so
OTHER_CSRC  = 
OTHER_ASRC  = 
CSRC        = mb.c tcp/mbtcp.c \
              functions/mbfunccoils.c \
              functions/mbfuncdiag.c \
              functions/mbfuncholding.c \
              functions/mbfuncinput.c \
              functions/mbfuncother.c \
              functions/mbfuncdisc.c \
              functions/mbutils.c 
ASRC        = 
OBJS        = $(CSRC:.c=.o) $(ASRC:.S=.o)
NOLINK_OBJS = $(OTHER_CSRC:.c=.o) $(OTHER_ASRC:.S=.o)
DEPS        = $(OBJS:.o=.d) $(NOLINK_OBJS:.o=.d)
BIN         = $(LIBRARIES)

.PHONY: clean all

all: $(BIN)

debug:
	$(INSIGHT) --se=$(TGT)

$(BIN): $(OBJS) $(NOLINK_OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

clean:
	rm -f $(DEPS)
	rm -f $(OBJS) $(NOLINK_OBJS)
	rm -f $(BIN)
	rm -f *.so

# ---------------------------------------------------------------------------
# rules for code generation
# ---------------------------------------------------------------------------
%.o:    %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o:    %.S
	$(CC) $(ASFLAGS) -o $@ -c $<

# ---------------------------------------------------------------------------
#  # compiler generated dependencies
# ---------------------------------------------------------------------------
-include $(LWOS_DEPS) $(PORT_DEPS) $(APPL_DEPS)

