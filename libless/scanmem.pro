TEMPLATE = app
TARGET = scanmem
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += thread
CONFIG += debug

HEADERS += \
  commands.h \
  interrupt.h \
  maps.h \
  show_message.h \
  endianness.h \
  licence.h \
  scanmem.h \
  target_memory_info_array.h \
  handlers.h \
  list.h \
  scanroutines.h \
  value.h

SOURCES += \
  commands.c \
  list.c \
  menu.c \
  scanroutines.c \
  value.c \
  endianness.c \
  ptrace.c \
  show_message.c \
  handlers.c \
  maps.c \
  scanmem.c \
  target_memory_info_array.c \
  main.c

