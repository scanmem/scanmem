/*
    show_message.c

    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <stdio.h>
#include <stdarg.h>

#include "show_message.h"
#include "scanmem.h"

void show_info(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "info: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_error(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_warn(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    fprintf(stderr, "warn: ");
    vfprintf(stderr, fmt, args);
    va_end (args);
}

void show_user(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    if (!(globals.options.backend))
    {
        vfprintf(stderr, fmt, args);
    }
    va_end (args);
}

void show_debug(const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    if (globals.options.debug)
    {
        fprintf(stderr, "debug: ");
        vfprintf(stderr, fmt, args);
    }
    va_end (args);
}


void show_scan_progress(unsigned long cur, unsigned long total)
{
    fprintf(stderr, ".");
    fflush(stderr);
}

