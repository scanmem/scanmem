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

# copied from ubuntu-tweak

dnl AS_AC_EXPAND(VAR, CONFIGURE_VAR)
dnl
dnl example
dnl AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
dnl will set SYSCONFDIR to /usr/local/etc if prefix=/usr/local

AC_DEFUN([AS_AC_EXPAND],
[
  EXP_VAR=[$1]
  FROM_VAR=[$2]

  dnl first expand prefix and exec_prefix if necessary
  prefix_save=$prefix
  exec_prefix_save=$exec_prefix

  dnl if no prefix given, then use /usr/local, the default prefix
  if test "x$prefix" = "xNONE"; then
    prefix=$ac_default_prefix
  fi
  dnl if no exec_prefix given, then use prefix
  if test "x$exec_prefix" = "xNONE"; then
    exec_prefix=$prefix
  fi

  full_var="$FROM_VAR"
  dnl loop until it doesn't change anymore
  while true; do
    new_full_var="`eval echo $full_var`"
    if test "x$new_full_var" = "x$full_var"; then break; fi
    full_var=$new_full_var
  done

  dnl clean up
  full_var=$new_full_var
  AC_SUBST([$1], "$full_var")

  dnl restore prefix and exec_prefix
  prefix=$prefix_save
  exec_prefix=$exec_prefix_save
])
# end copy

AS_AC_EXPAND(PKGDATADIR, $datadir/$PACKAGE)

AC_CONFIG_FILES([ Makefile ])

AC_ARG_ENABLE(gui, [AS_HELP_STRING([--enable-gui],
                            [enable gameconqueror, the gui front-end of scanmem])],
                 [enable_gui=true
                  AC_CONFIG_FILES([
                 gui/Makefile
                 gui/consts.py
                 gui/gameconqueror
                 ])],
                 [enable_gui=false]
                 )

AM_CONDITIONAL([ENABLE_GUI], [test x$enable_gui = xtrue])
AC_OUTPUT
