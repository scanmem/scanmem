# ![](https://raw.githubusercontent.com/scanmem/scanmem/master/gui/GameConqueror_72x72.png)scanmem & GameConqueror

[![Build Status](https://travis-ci.org/scanmem/scanmem.svg?branch=master)](https://travis-ci.org/scanmem/scanmem)
[![Coverity Status](https://scan.coverity.com/projects/8565/badge.svg?flat=1")](https://scan.coverity.com/projects/scanmem)

scanmem is a debugging utility designed to isolate the address of an arbitrary
variable in an executing process. scanmem simply needs to be told the pid of
the process and the value of the variable at several different times.

After several scans of the process, scanmem isolates the position of the
variable and allows you to modify its value.

## GUI

GameConqueror is a GUI front-end for scanmem, providing more features, such as:
  * Flexible syntax for searching
  * Easier and multiple variable locking
  * Better process finder
  * Memory browser/editor

See [gui/README](gui/README) for more details.

## Requirements

scanmem requires libreadline to read commands interactively, and `/proc` must be
mounted. GameConqueror requirements are documented in [gui/README](gui/README).

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
