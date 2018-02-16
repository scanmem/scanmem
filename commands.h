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
#include "list.h"


typedef bool (*handler_ptr)(globals_t *vars, char **argv, unsigned argc);

typedef struct {
    char *word;
    list_t *list;
    unsigned index;
} completion_t;

typedef struct {
    handler_ptr handler;
    char *command;
    char *shortdoc;
    char *longdoc;
    list_t *completions;
    unsigned complidx;
} command_t;


bool sm_registercommand(const char *command, handler_ptr handler, list_t *commands,
                        char *shortdoc, char *longdoc, const char *complstr);
bool sm_execcommand(globals_t *vars, const char *commandline);

void sm_free_all_completions(list_t *commands);

#endif /* COMMANDS_H */
