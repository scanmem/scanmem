TESTS = sm_test.sh peekbuf_test
check_PROGRAMS = memfake peekbuf_test

memfake_SOURCES = memfake.c
memfake_CFLAGS = -std=gnu99 -Wall

peekbuf_test_SOURCES = peekbuf_test.c
peekbuf_test_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir)
peekbuf_test_LDADD = $(top_builddir)/libscanmem.la
