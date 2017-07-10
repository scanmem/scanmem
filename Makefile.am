ACLOCAL_AMFLAGS = -I m4

if ENABLE_GUI
  SUBDIRS = po gui
endif

AM_CFLAGS = -std=gnu99 -O2 -g -Wall

lib_LTLIBRARIES = libscanmem.la

libscanmem_la_includedir = $(includedir)/scanmem

libscanmem_la_include_HEADERS = commands.h \
    list.h \
    maps.h \
    scanmem.h \
    scanroutines.h \
    show_message.h \
    targetmem.h \
    value.h

libscanmem_la_SOURCES = commands.c \
    ptrace.c \
    menu.c \
    handlers.h \
    handlers.c \
    interrupt.h \
    list.c \
    licence.h \
    maps.c \
    scanmem.c \
    scanroutines.c \
    show_message.c \
    targetmem.c \
    value.c 

if !HAVE_GETLINE
  libscanmem_la_SOURCES += getline.h \
      getline.c
endif

if !WITH_READLINE
  libscanmem_la_SOURCES += readline.h \
      readline.c
endif

libscanmem_la_LDFLAGS = -version-info 1:0:0

bin_PROGRAMS = scanmem

scanmem_SOURCES = main.c
scanmem_LDADD = libscanmem.la

dist_man_MANS = scanmem.1
dist_doc_DATA = README

EXTRA_DIST = gpl-3.0.txt lgpl-3.0.txt
