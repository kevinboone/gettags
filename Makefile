# This is the Makefile for gettags. It intended to work on modern-ish 
# Linux systems, and on Windows with MinGW and Msys installed, or under Cygwin. 
# To build a debug version, set the environment variable DEBUG. Otherwise
# you'll get a stripped version with no console output
#
# NB: The actuall dependencies are in dependencies.mak.
#
# To build:
# make
#

UNAME := $(shell uname -o)
BINDIR=/usr/bin

.SUFFIXES: .o .c

APPNAME=gettags
PROJNAME=$(APPNAME)

# MinGW is taken as a win32 platform; Cygwin builds the same as Linux
PLATFORM=dummy
ifeq ($(UNAME),Msys)
        PLATFORM=win32
	APPBIN=$(APPNAME).exe
else  
        PLATFORM=linux
	APPBIN=$(APPNAME)
endif

OBJS=main.o tag_reader.o


APPS=$(APPBIN)
VERSION=0.2a

DEBUG=1

ifeq ($(DEBUG),1)
	DEBUG_CFLAGS=-g -DDEBUG=1
else
  ifeq ($(PLATFORM),win32)
        PROD_LDFLAGS=-s 
  else
        PROD_LDFLAGS=-s
  endif
endif


all: $(APPS)

include dependencies.mak

CFLAGS=-Wall $(DEBUG_CFLAGS) $(PLATFORM_CFLAGS) -DVERSION=\"$(VERSION)\"
INCLUDES=$(PLATFORM_INCLUDES) 
LIBS=$(PLATFORM_LIBS)


.c.o:
	gcc $(CFLAGS) $(INCLUDES) -DPIXMAPDIR=\"$(PIXMAPDIR)\" -DAPPNAME=\"$(APPNAME)\" -c $*.c -o $*.o

$(APPBIN): $(OBJS) 
	gcc $(PROD_LDFLAGS) $(DEBUG_LDFLAGS) $(LDFLAGS) -o $(APPNAME) $(OBJS) $(LIBS)

install: 
	cp -p $(APPBIN) $(BINDIR)

clean:
	rm -f $(APPBIN) *.o

