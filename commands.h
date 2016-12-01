/*
    Registration and general execution of commands.

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

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

#include "scanmem.h"


typedef struct {
    bool (*handler)(globals_t *vars, char **argv, unsigned argc);
    char *command;
    char *shortdoc;
    char *longdoc;
} command_t;


bool sm_registercommand(const char *command, void *handler, list_t *commands,
                        char *shortdoc, char *longdoc);
bool sm_execcommand(globals_t *vars, const char *commandline);

#endif /* COMMANDS_H */
