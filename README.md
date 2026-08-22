# ![](https://raw.githubusercontent.com/scanmem/scanmem/main/gui/GameConqueror_72x72.png)scanmem & GameConqueror

[![Build Status](https://github.com/scanmem/scanmem/actions/workflows/01-build.yml/badge.svg)](https://github.com/scanmem/scanmem/actions/workflows/01-build.yml)
[![Coverity Status](https://scan.coverity.com/projects/8565/badge.svg?flat=1")](https://scan.coverity.com/projects/scanmem)

scanmem is a debugging utility designed to isolate the address of an arbitrary
variable in an executing process. scanmem simply needs to be told the pid of
the process and the value of the variable at several different times.

After several scans of the process, scanmem isolates the position of the
variable and allows you to modify its value.

## Scan speed

The first scan is the slow one. It has to run the comparison at every byte
offset of every scanned region, where later scans only revisit addresses that
already matched. It is split across threads now.

`option threads N` sets the count. The default, `0`, uses one thread per online
CPU. `1` scans serially.

Measured on a 12 core machine, scanning for a value with 1000 matches in the
region, best of 3:

| region | serial | 12 threads |
|--------|--------|------------|
| 64MB   | 204ms  | 84ms       |
| 256MB  | 734ms  | 208ms      |
| 1GB    | 2936ms | 702ms      |

How much this helps depends on how many cores are actually free, and on the
target being big enough for the split to pay for itself. `threads 1` performs
the same as the older serial code.

Reads go through `process_vm_readv` where the kernel allows it, falling back to
`/proc/pid/mem` and then to `ptrace`. On its own that call is roughly 2.8x
faster than `pread` for this access pattern, but end to end it is worth a few
percent at most, because the scan is limited by the comparison work rather than
by reading.

Any thread count returns the same matches. `bench/run.sh` reproduces the numbers
above and `make check` compares a chunked scan against an unchunked one.

## GUI

GameConqueror is a GUI front-end for scanmem, providing more features, such as:
  * Flexible syntax for searching
  * Easier and multiple variable locking
  * Better process finder
  * Memory browser/editor

See [gui/README.md](gui/README.md) for more details.

## Requirements

scanmem requires libreadline to read commands interactively, and `/proc` must be
mounted. GameConqueror requirements are documented in [gui/README.md](gui/README.md).

## Documentation

To read documentation:
  * `man scanmem`
  * `man gameconqueror`
  * `scanmem --help`
  * enter `help` at the scanmem prompt
  * use the interactive help of GameConqueror

## Build Requirements

The build requires autotools-dev, libtool, libreadline-dev, intltool, and python.

## Build and Install

To generate files required for the build:

    ./autogen.sh

To build with GUI:

    ./configure --prefix=/usr --enable-gui && make
    sudo make install

To build without GUI:

    ./configure --prefix=/usr && make
    sudo make install

scanmem and GameConqueror use static paths to libscanmem. So executing
`ldconfig` is not required. Consider setting `--libdir=/usr/lib/scanmem` or
`--libdir=/usr/lib64/scanmem` to avoid that libscanmem is in a library
search path.

Run `./configure --help` for more details.

## Android Build

You need a
[standalone toolchain of Android NDK](https://developer.android.com/ndk/guides/standalone_toolchain.html#itc)
(Advanced method) to build interactive capabilities for Android.
For more information, run:

    ./build_for_android.sh help

## License: 

GPLv3, LGPLv3 for libscanmem
