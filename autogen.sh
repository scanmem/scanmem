#!/bin/sh

LIBTOOLIZE=libtoolize
if [ "$(uname -s)" = "Darwin" ]; then
  LIBTOOLIZE=glibtoolize # install via brew
fi

$LIBTOOLIZE -c && aclocal -I m4 && automake -c --add-missing && autoconf && intltoolize -f -c
