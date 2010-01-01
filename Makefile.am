# $Id: Makefile.am,v 1.5 2007-04-14 18:47:39+01 taviso Exp $
bin_PROGRAMS = scanmem
scanmem_SOURCES = main.c maps.c ptrace.c target_memory_info_array.c list.c menu.c commands.c handlers.c value.c
scanmem_SOURCES += commands.h handlers.h interrupt.h list.h scanmem.h value.h licence.h maps.h target_memory_info_array.h
scanmem_SOURCES += scanroutines.h scanroutines.c
scanmem_CFLAGS = -O1 -g -Wall
dist_man_MANS = scanmem.1
EXTRA_DIST = gui/GameConqueror_128x128.png gui/GameConqueror_72x72.png gui/GameConqueror.py gui/GameConqueror.xml gui/README gui/TODO
EXTRA_DIST += test/smtest.c test/smtest.dat
EXTRA_DIST += test/config/unix.exp
EXTRA_DIST += test/scanmem/dregion.exp test/scanmem/iterchar.exp test/scanmem/lregions.exp test/scanmem/popchar.exp test/scanmem/shell.exp test/scanmem/testrun.log test/scanmem/testrun.sum test/scanmem/version.exp
