ACLOCAL_AMFLAGS = -I m4

SUBDIRS = po
if ENABLE_GUI
  SUBDIRS += gui
endif

AM_CFLAGS = -O2 -g -Wall

lib_LTLIBRARIES = libscanmem.la

libscanmem_la_SOURCES = commands.h \
    commands.c \
    ptrace.c \
    menu.c \
    handlers.h \
    handlers.c \
    interrupt.h \
    list.h \
    list.c \
    licence.h \
    maps.h \
    maps.c \
    scanmem.h \
    scanmem.c \
    scanroutines.h \
    scanroutines.c \
    show_message.h \
    show_message.c \
    target_memory_info_array.h \
    target_memory_info_array.c \
    value.h \
    value.c 

libscanmem_la_LDFLAGS = -version-info 1:0:0

bin_PROGRAMS = scanmem

scanmem_SOURCES = main.c
scanmem_LDADD = libscanmem.la

dist_man_MANS = scanmem.1
dist_doc_DATA = README

EXTRA_DIST = gpl-2.0.txt gpl-3.0.txt
