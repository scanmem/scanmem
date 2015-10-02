/*
    Registration and general execution of commands.
*/

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

#include "scanmem.h"

/*lint -esym(534,registercommand) okay to ignore return value */

typedef struct {
    bool(*handler) (globals_t * vars, char **argv, unsigned argc);
    char *command;
    char *shortdoc;
    char *longdoc;
} command_t;


bool registercommand(const char *command, void *handler, list_t * commands,
                     char *shortdoc, char *longdoc);
bool execcommand(globals_t * vars, const char *commandline);

#endif /* COMMANDS_H */
