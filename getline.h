/*
    Replace getline() with BSD specific fgetln().

    Copyright (C) 2015 JianXiong Zhou <zhoujianxiong2@gmail.com>
    Copyright (C) 2015 Jonathan Pelletier <funmungus(a)gmail.com>

    This file is part of libscanmem.

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GETLINE_H
#define GETLINE_H

#include "config.h"

#ifndef HAVE_GETLINE
#include <stdio.h>

ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif

#endif /* GETLINE_H */
