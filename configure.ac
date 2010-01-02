# $Id: configure.ac,v 1.1 2007/06/04 22:51:13 taviso Exp $
AC_INIT([scanmem], [0.09], [taviso@sdf.lonestar.org])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADER([config.h])
AC_GNU_SOURCE
AC_FUNC_ALLOCA
AC_HEADER_STDBOOL

# check if termcap is present, sometimes required by readline
AC_CHECK_LIB([termcap], [tgetent], [], [])

# check for libreadline
AC_CHECK_LIB([readline], [readline], [], [
    echo "libreadline could not be found, which is required to continue."
    echo "The libreadline-dev package may be required."
    exit 1
])

# also need to check if the file is zero'ed (some hardened systems)
AC_CHECK_FILE([/proc/self/maps], [], [
    echo "This system does not seem to have /proc/pid/maps files."
    exit 1
])

# also need to check this file works
AC_CHECK_FILE([/proc/self/mem], [
    # LARGEFILE support required for this to work
    AC_SYS_LARGEFILE
    AC_DEFINE(HAVE_PROCMEM, [1], [Enable /proc/pid/mem support])
],[
    # This will hurt performance.
    echo "This system does not seem to have /proc/pid/mem files."
    echo "Falling back to ptrace() only support."
    AC_DEFINE(HAVE_PROCMEM, [0], [Enable /proc/pid/mem support])
])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
