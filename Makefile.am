ACLOCAL_AMFLAGS = -I m4

SUBDIRS = test

if ENABLE_GUI
  SUBDIRS += po gui
endif

# '-O2 -g' are added by `configure`, unless the user overrides CFLAGS
AM_CFLAGS = -std=gnu99 -Wall

lib_LTLIBRARIES = libscanmem.la

libscanmem_la_includedir = $(includedir)/scanmem

libscanmem_la_include_HEADERS = commands.h \
    list.h \
    maps.h \
    scanmem.h \
    scanroutines.h \
    sets.h \
    show_message.h \
    targetmem.h \
    value.h

libscanmem_la_SOURCES = commands.c \
    common.h \
    ptrace.c \
    handlers.h \
    handlers.c \
    interrupt.h \
    list.c \
    licence.h \
    maps.c \
    scanmem.c \
    scanroutines.c \
    sets.c \
    show_message.c \
    targetmem.c \
    value.c 

if !HAVE_GETLINE
  libscanmem_la_SOURCES += getline.h \
      getline.c
endif

libscanmem_la_LDFLAGS = -version-info 1:0:0 \
                        -export-symbols-regex '^sm_|^show_'

bin_PROGRAMS = scanmem

scanmem_SOURCES = menu.h \
    menu.c \
    main.c

if !WITH_READLINE
  scanmem_SOURCES += readline.h \
      readline.c
endif

scanmem_LDADD = libscanmem.la

dist_man_MANS = scanmem.1
dist_doc_DATA = README

EXTRA_DIST = gpl-3.0.txt lgpl-3.0.txt
