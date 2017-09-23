/*
    Provide a simple program to run test scans on

    Copyright (C) 2017 Andrea Stacchiotti  <andreastacchiotti(a)gmail.com>

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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    uint MB_to_allocate = 1;
    bool add_randomness = false;

    if (argc >= 2) MB_to_allocate = atoi(argv[1]);
    if (argc >= 3) add_randomness = atoi(argv[2]);
    if (argc >= 4) return 1;

    size_t array_size = MB_to_allocate * 1024 * 1024 / sizeof(int);

    int* array = calloc(array_size, sizeof(int));

    // Fill half with random values and leave an half of zeroes, if asked to
    if (add_randomness) {
        srand(time(NULL));
        for (size_t i = 0; i < array_size/2; i++) {
            array[i] = rand();
        }
    }

    pause();

    free(array);
    return 0;
}

