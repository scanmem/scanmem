/*
    Replace getline

    Copyright (C) 2015 JianXiong Zhou <zhoujianxiong2@gmail.com>
    Copyright (C) 2015 Jonathan Pelletier <funmungus(a)gmail.com>

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

#include <stdlib.h>

#include "getline.h"

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    char *ptr;
    size_t len = 0;

    ptr = fgetln(stream, &len);
    if (ptr == NULL) {
        return -1;
    }
    /* Add one more space for '\0', possibly empty string */
    ++ len;

    /* Realloc the original ptr
    Lines are usually freed after, so as an optimization we do not
    free here, as long as there is enough space. */
    if (*lineptr == NULL || (n != NULL && n[0] < len))
    {
        /* Conditional not necessary, possible to remove */
        if (*lineptr != NULL)
            free(*lineptr);
        if ((*lineptr = malloc(len)) == NULL)
        {
            if (n != NULL)
              n[0] = 0;
            return -1;
        }
    }

    /* Update the length */
    if (n != NULL)
      n[0] = len;

    /* Copy over the string, without NULL */
    memcpy(*lineptr, ptr, len-1);

    /* Write the NULL character */
    (*lineptr)[len-1] = '\0';

    /* Return the length of the new buffer, not including NULL */
    return len - 1;
}
