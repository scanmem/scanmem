# $Id: Makefile.am,v 1.5 2007-04-14 18:47:39+01 taviso Exp $
SUBDIRS = test
if ENABLE_GUI
  SUBDIRS += gui
else
  SUBDIRS += 
endif

bin_PROGRAMS = scanmem
scanmem_SOURCES = main.c maps.c ptrace.c target_memory_info_array.c list.c menu.c commands.c handlers.c value.c
scanmem_SOURCES += commands.h handlers.h interrupt.h list.h scanmem.h value.h licence.h maps.h target_memory_info_array.h
scanmem_SOURCES += scanroutines.h scanroutines.c
scanmem_CFLAGS = -O2 -g -Wall
dist_man_MANS = scanmem.1
dist_doc_DATA = README

EXTRA_DIST = gpl-2.0.txt gpl-3.0.txt
