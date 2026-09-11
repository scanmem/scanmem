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
#include <assert.h>

int main(int argc, char **argv)
{
    size_t MB_to_allocate = 1;
    bool add_randomness = false;

    if (argc >= 2) MB_to_allocate = strtoul(argv[1], NULL, 10);
    if (argc >= 3) add_randomness = strtoul(argv[2], NULL, 10);
    if (argc >= 4) return 1;

    size_t array_size = MB_to_allocate * 1024 * 1024 / sizeof(int);

    int* array = calloc(array_size, sizeof(int));
    assert(array != NULL);

    // Fill half with random values and leave an half of zeroes, if asked to
    if (add_randomness) {
        srand(time(NULL));
        for (size_t i = 0; i < array_size/2; i++) {
            array[i] = rand();
        }
    }

    // 
    printf("select xor pattern:\n");
    printf("1) ...010101\n");
    printf("2) ...101010\n");
    printf("3) ...110011\n");
    printf("4) ...100100\n");
    int patnum;
    scanf("%d", &patnum);
    int pat;
    switch (patnum) {
        case 1: pat = 0b01010101010101010101010101010101; break;
        case 2: pat = 0b10101010101010101010101010101010; break;
        case 3: pat = 0b00110011001100110011001100110011; break;
        case 4: pat = 0b00100100100100100100100100100100; break;
        default: pat = -1;
    }

    int prev_n, cur_n;
    float prev_f, f;
    double prev_d, d;
    while (1) {
        prev_n = cur_n;
        prev_f = (float)prev_n;
        prev_d = (double)prev_n; 
        printf("enter number to xor and update memory:\n");
        scanf("%d", &cur_n);

        // You should search for this number.
        array[17] = cur_n ^ pat;
        f = (float)array[17];
        d = (double)array[17];

        printf("xor's value is now %d, %f, %f. Delta is %d %f %f.\n", array[17], f, d, (cur_n ^ prev_n), (float)(((unsigned int)f) ^ ((unsigned int)prev_f)), (double)(((unsigned long)d) ^ ((unsigned long)prev_d)));  
    }
    
    pause();

    free(array);
    return 0;
}

