#!/bin/sh
rm -f po/Makefile.in.in
libtoolize && aclocal -I m4 && automake --add-missing && autoconf && intltoolize
