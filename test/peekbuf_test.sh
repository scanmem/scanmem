#!/bin/sh
# The library gets built with sanitizers in CI while the test programs are
# built by `make check` without them, and asan refuses to start when its
# runtime is not first in the library list. Load it ahead of time when the
# library we are linked against turns out to be instrumented.

set -e

lib=../.libs/libscanmem.so
if [ -f "$lib" ] && nm -D "$lib" 2>/dev/null | grep -q __asan_init; then
    rt=`${CC:-gcc} -print-file-name=libasan.so 2>/dev/null`
    if [ -f "$rt" ]; then
        LD_PRELOAD="$rt${LD_PRELOAD:+:$LD_PRELOAD}"
        export LD_PRELOAD
    fi
fi

exec ./peekbuf_test
