#!/bin/sh
libtoolize && aclocal -I m4 && automake --add-missing && autoconf && intltoolize -f
