/*
    Prompt and command completion.

    Copyright (C) 2017 Andrea Stacchiotti  <andreastacchiotti(a)gmail.com>

    This file is part of scanmem.

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

#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

#include "scanmem.h"

/*
 * getcommand() reads in a command using readline, and places a pointer to
 * the read string into *line, _which must be free'd by caller_.
 * returns true on success, or false on error.
 */
bool getcommand(globals_t *vars, char **line);

#endif /* MENU_H */
