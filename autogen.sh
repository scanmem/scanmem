#!/bin/sh
libtoolize -c && aclocal -I m4 && automake -c --add-missing && autoconf && intltoolize -f -c
