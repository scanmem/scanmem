#!/bin/bash

export mdir=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
qmake -o ${mdir}/Makefile ${mdir}/scanmem.pro
unset mdir

