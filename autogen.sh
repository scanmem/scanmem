#!/bin/sh

LIBTOOLIZE=libtoolize
if [ "$(uname -s)" = "Darwin" ]; then
    LIBTOOLIZE=glibtoolize  # install via brew
fi

echo "+ running $LIBTOOLIZE ..."
$LIBTOOLIZE -c || {
    echo
    echo "$LIBTOOLIZE failed - check that it is present on system"
    exit 1
}
echo "+ running aclocal ..."
aclocal -I m4 || {
    echo
    echo "aclocal failed - check that all needed development files"\
         "are present on system"
    exit 1
}
echo "+ running autoheader ... "
autoheader || {
    echo
    echo "autoheader failed"
    exit 1
}
echo "+ running autoconf ... "
autoconf || {
    echo
    echo "autoconf failed"
    exit 1
}
echo "+ running intltoolize ..."
intltoolize -f -c --automake || {
    echo
    echo "intltoolize failed - check that it is present on system"
    exit 1
}
echo "+ running automake ... "
automake -c --add-missing || {
    echo
    echo "automake failed"
    exit 1
}
