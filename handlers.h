/*
    Specific command handling with help texts.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2014-2016      Sebastian Parschauer <s.parschauer@gmx.de>

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

#ifndef HANDLERS_H
#define HANDLERS_H

#include <stdbool.h>

#include "scanmem.h"

/* Common documentations */

#define SET_FORMAT_DOC \
    "The Set Format:\n" \
    "[!][..a](,b..c | d, ...)[e..]\n" \
    "An optional `!` at the beginning inverts the defined set.\n" \
    "The beginning and end of the set may include an optional \"open-ended\" range:\n" \
    "`..a` means a range starting at `0` to `a`.\n" \
    "`e..` means a range starting at `e` to the last valid value.\n" \
    "The rest is the same as the standard format.\n\n"

/*
 * SHRTDOC's are one line descriptions of the supported command (shown with `help`).
 * LONGDOC's are detailed descriptions (shown with `help command`) (wrap them before column 80).
 * 
 * The DOC's are passed to the registercommand() routine, and are read by the help
 * command. You can define SHRTDOC as NULL and help will not print it.
 * However, a NULL longdoc will print "missing documentation" if specific help is requested.
 * If you really don't want it to print anything, use "".
 *
 * Each handler is passed an argv and argc, argv[0] being the command called. Also passed is
 * a pointer to the global settings structure, which contains various settings and lists
 * that handlers are allowed to change. One handler can handle multiple commands, but
 * you need register each command with its own documentation.
 */

#define SET_SHRTDOC "change known matches to specified value"
#define SET_LONGDOC "usage: set <[match-id set=]n [...]>\n" \
               "Inject the value `n` into all the matches in `match-id set`, or if just `n` is\n" \
               "specified, all known matches. `n` can be specified in standard C language\n" \
               "notation, a leading 0x for hexadecimal, leading 0 for octal, and everything\n" \
               "else is assumed to be decimal. All known matches, along with their match-ids\n" \
               "can be displayed with the `list` command. Multiple match-ids can be specified,\n" \
               "as a set data-type, terminated with a '=' sign. See below for a description of\n" \
               "the set data-type.\n" \
               "To set a value continually, for example to prevent a counter from decreasing,\n" \
               "suffix the command with '/', followed by the number of seconds to wait between\n" \
               "sets. Interrupt scanmem with ^C to stop the setting.\n\n" \
               "Note that this command cannot work for bytearray or string.\n\n" \
               SET_FORMAT_DOC \
               "Examples:\n" \
               "\tset 10 - set all known matches to 10\n" \
               "\tset 0=0x03 - set match 0 to 0x03.\n" \
               "\tset ..7=0x32 - set matches 0 through 7 to 0x32\n" \
               "\tset 0,3=42/8 - set matches 0 and 3 to 42 every 8 seconds\n" \
               "\tset !4..12=5 - set all matches except 4 through 12 to 5\n" \
               "\tset 12,13,14=0x23/2 19..23=0/8 6=4 0 - complex example, can be combined" \

bool handler__set(globals_t *vars, char **argv, unsigned argc);

#define LIST_SHRTDOC "list currently known matches"
#define LIST_LONGDOC "usage: list [max_to_print]\n" \
               "Print currently known matches, along with details about the\n" \
               "match, such as its type, location, and last known value. The number in\n" \
               "the left column is the `match-id`, this can be passed to other commands\n" \
               "such as `set`, `delete`, etc.\n" \
               "By default `list` prints up to 10k matches, a numerical parameter" \
               "can be given to change this limit.\n" \
               "The flags displayed indicate the possible types of the variable.\n" \
               "Also the region id, an offset and the region type belonging to a match\n" \
               "are displayed. The offset is used from the code load address or region start.\n" \
               "This helps bypassing address space layout randomization (ASLR).\n"

bool handler__list(globals_t *vars, char **argv, unsigned argc);

#define DELETE_SHRTDOC "delete known matches by match-id"
#define DELETE_LONGDOC "usage: delete <match-id set>\n" \
                "Remove a set of match-id's from the match list. The set format\n" \
                "is described below. The list of match-id's can\n" \
                "be found using the `list` command. To delete all matches, see\n" \
                "the `reset` command.\n" \
                "To delete all matches associated with a particular library, see the\n" \
                "`dregion` command, which will also remove any associated matches.\n\n" \
                SET_FORMAT_DOC \
                "Examples:\n" \
                "\tdelete ..5,10..15  - delete matches 0 through 5 and 10 through 15\n" \
                "\tdelete ..5,14,20.. - delete matches 0 through 5, 14, and 20 through the last match\n" \
                "\tdelete !4..8,11    - delete all matches except for 4 through 8 and 11\n\n" \
                "NOTE: Match-ids may be recalculated after matches are removed or added. However, the set\n" \
                "      of matches is guaranteed to be deleted properly."


bool handler__delete(globals_t *vars, char **argv, unsigned argc);

#define RESET_SHRTDOC "forget all matches, and reinitialise regions"
#define RESET_LONGDOC "usage: reset\n" \
                "Forget all matches and regions, and reread regions from the relevant\n" \
                "maps file. Useful if you have made an error, or want to find a new\n" \
                "variable.\n"

bool handler__reset(globals_t *vars, char **argv, unsigned argc);

#define PID_SHRTDOC "print current pid, or attach to a new process"
#define PID_LONGDOC "usage: pid [pid]\n" \
                "If `pid` is specified, reset current session and then attach to new\n" \
                "process `pid`. If `pid` is not specified, print information about\n" \
                "current process."

bool handler__pid(globals_t *vars, char **argv, unsigned argc);

#define SNAPSHOT_SHRTDOC "take a snapshot of the current process state"
#define SNAPSHOT_LONGDOC "usage: snapshot\n" \
                "Take a snapshot of the entire process in its current state. This is useful\n" \
                "if you don't know the exact value of the variable you are searching for, but\n" \
                "can describe it in terms of higher, lower or equal (see commands `>`,`<` and\n" \
                "`=`).\n\n" \
                "NOTE: This can use a lot of memory with large processes."

bool handler__snapshot(globals_t *vars, char **argv, unsigned argc);

#define DREGION_SHRTDOC "delete a known region by region-id"
#define DREGION_LONGDOC "usage: dregion <region-id set>\n" \
                "Remove a set of region-id from the regions list,\n" \
                "along with any matches affected from the match list.\n" \
                "The `region-id` can be found using the `lregions` command.\n\n" \
                SET_FORMAT_DOC

bool handler__dregion(globals_t *vars, char **argv, unsigned argc);

#define LREGIONS_SHRTDOC "list all known regions"
#define LREGIONS_LONGDOC "usage: lregions\n" \
                "Print all the currently known regions, along with details such as the\n" \
                "start address, size, region type, load address, permissions and associated\n" \
                "filename. The number in the left column is the `region-id`, this can be\n" \
                "passed to other commands that process regions, such as `dregion`.\n" \
                "The load address is the start of the .text region for the executable\n" \
                "or libraries. Otherwise, it is the region start.\n"

bool handler__lregions(globals_t *vars, char **argv, unsigned argc);

#define GREATERTHAN_SHRTDOC "match values that have increased or greater than some number"
#define LESSTHAN_SHRTDOC    "match values that have decreased or less than some number"
#define NOTCHANGED_SHRTDOC  "match values that have not changed or equal to some number"
#define CHANGED_SHRTDOC     "match values that have changed or different from some number"
#define INCREASED_SHRTDOC   "match values that have increased at all or by some number"
#define DECREASED_SHRTDOC   "match values that have decreased at all or by some number"

#define GREATERTHAN_LONGDOC "usage: > [n]\n" \
                "If n is given, match values that are greater than n.\n" \
                "Otherwise match all values that have increased. (same as `+`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."
#define LESSTHAN_LONGDOC "usage: < [n]\n" \
                "If n is given, match values that are less than n.\n" \
                "Otherwise match all values that have decreased. (same as `-`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."

#define NOTCHANGED_LONGDOC "usage: = [n]\n" \
                "If n is given, match values that are equal to n. (same as `n`)\n" \
                "Otherwise match all values that have not changed since the last scan.\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."
#define CHANGED_LONGDOC "usage: != [n]\n" \
                "If n is given, match values that are different from n.\n" \
                "Otherwise match all values that have changed since the last scan.\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."

#define INCREASED_LONGDOC "usage: + [n]\n" \
                "If n is given, match values that have been increased by n\n" \
                "Otherwise match all values that have increased. (same as `>`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."

#define DECREASED_LONGDOC "usage: - [n]\n" \
                "If n is given, match values that have been decreased by n\n" \
                "Otherwise match all values that have decreased. (same as `<`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."


bool handler__operators(globals_t *vars, char **argv, unsigned argc);

#define VERSION_SHRTDOC "print current version"
#define VERSION_LONGDOC "usage: version\n" \
                "Display the current version of scanmem in use."

bool handler__version(globals_t *vars, char **argv, unsigned argc);

#define EXIT_SHRTDOC "exit the program immediately"
#define EXIT_LONGDOC "usage: exit\n" \
                "Exit scanmem immediately, zero will be returned."

bool handler__exit(globals_t *vars, char **argv, unsigned argc);

/*
 * Completion string syntax:
 * Separate words in the first level of the sub-command list with ',' without
 * any spaces in between. Example: "word1,word2,word3".
 *
 * Add another list right behind a word in the first level surrounded by
 * '{' and '}'. Place the ',' to mark the end of the first level completion
 * word right behind '}'. More levels than two are currently not supported.
 * Example: "word1{word1.1,word1.2},word2{word2.1,word2.2}"
 *
 * Special words are surrounded by '<' and '>'. The completer reacts on them
 * by filling in the requested data. Currently, only "<command>" is supported
 * to fill in the commands as a parameter for the "help" command. No further
 * sub-commands are allowed for this. For the help command it is actually
 * easier to compare the command name with "help" to do this trick.
 */
#define HELP_COMPLETE "<command>"
#define HELP_SHRTDOC "access online documentation, use `help command` for specific help"
#define HELP_LONGDOC "usage: help [command]\n" \
                "If `command` is specified, print detailed information about command `command`\n" \
                "including options and usage summary. If `command` is not specified, print a\n" \
                "one line description of each command supported."

bool handler__help(globals_t *vars, char **argv, unsigned argc);

#define DEFAULT_SHRTDOC NULL
#define DEFAULT_LONGDOC "When searching for a number, use any notation in standard C language (leading 0x for\n" \
                "hexadecimal, leading 0 for octal, everything else is assumed to be decimal).\n" \
                "Float numbers are also acceptable, but will be rounded if scanning integers.\n" \
                "Use \'..\' for a range, e.g. \'1..3\' searches between 1 and 3 inclusive.\n" \
                "\n" \
                "When searching for an array of byte, use 2-byte hexadecimal notation, \n" \
                "separated by spaces, wildcard '?\?' is also supported. E.g. FF ?\? EE ?\? 02 01\n" \
                "\n" \
                "When searching for strings, use the \" command\n" \
                "\n" \
                "Scan the current process for variables with given value.\n" \
                "By entering the value of the variable as it changes multiple times, scanmem can\n" \
                "eliminate matches, eventually identifying where the variable is located.\n" \
                "Once the variable is found, use `set` to change its value.\n"

bool handler__default(globals_t *vars, char **argv, unsigned argc);

#define STRING_SHRTDOC "match a given string"
#define STRING_LONGDOC "usage \" <text>\n" \
                "<text> is counted since the 2nd character following the leading \"\n" \
                "This can only be used when scan_data_type is set to be string\n" \
                "Example:\n" \
                "\t\" Scan for string, spaces and ' \" are all acceptable.\n"

bool handler__string(globals_t *vars, char **argv, unsigned argc);

#define UPDATE_SHRTDOC "update match values without culling list"
#define UPDATE_LONGDOC "usage: update\n" \
                "Scans the current process, getting the current values of all matches.\n" \
                "These values can be viewed with `list`, and are also the old values that\n" \
                "scanmem compares to when using `>`, `<`, or `=`. This command is equivalent\n" \
                "to a search command that all current results match.\n"

bool handler__update(globals_t *vars, char **argv, unsigned argc);

bool handler__eof(globals_t *vars, char **argv, unsigned argc);

#define SHELL_SHRTDOC "execute a shell command without leaving scanmem"
#define SHELL_LONGDOC "usage: ! | shell [shell-command]\n" \
                "Execute `shell-command` using /bin/sh then return, useful for reading man\n" \
                "pages, checking top, or making notes with an external editor.\n" \
                "Examples:\n" \
                "\t! vi notes.txt\n" \
                "\tshell man scanmem\n" \
                "\tshell cat > notes.txt\n"

bool handler__shell(globals_t *vars, char **argv, unsigned argc);

#define WATCH_SHRTDOC "monitor the value of a memory location as it changes"
#define WATCH_LONGDOC "usage: watch [match-id]\n" \
                "Monitors the match `match-id`, by testing its value every second. When the\n" \
                "value changes, its new value is printed along with an timestamp. Interrupt\n" \
                "with ^C to stop monitoring.\n" \
                "Examples:\n" \
                "\twatch 12 - watch match 12 for any changes.\n"

bool handler__watch(globals_t *vars, char **argv, unsigned argc);

/*XXX: improve this */
#define SHOW_COMPLETE "copying,warranty,version"
#define SHOW_SHRTDOC "display information about scanmem."
#define SHOW_LONGDOC "usage: show <info>\n" \
                "Display information relating to <info>.\n" \
                "Possible <info> values: `copying`, `warranty` or `version`\n"

bool handler__show(globals_t *vars, char **argv, unsigned argc);

#define DUMP_SHRTDOC "dump a memory region to screen or a file" 
#define DUMP_LONGDOC "usage: dump <address> <length> [<filename>]\n" \
                "\n" \
                "If <filename> is given, save the region of memory to the file \n" \
                "Otherwise display it in a human-readable format.\n"
    
bool handler__dump(globals_t *vars, char **argv, unsigned argc);

#define VALUE_TYPES "int8,int16,int32,int64,float32,float64,bytearray,string"
#define WRITE_COMPLETE VALUE_TYPES
#define WRITE_SHRTDOC "change the value of a specific memory location"
#define WRITE_LONGDOC "usage: write <value_type> <address> <value>\n" \
                "\n" \
                "Write <value> into <address>\n" \
                "<value_type> should be one of:\n" \
                "\tint{8|16|32|64} (or i{8|16|32|64} for short)\n" \
                "\tfloat{32|64} (or f{32|64} for short)\n" \
                "\tbytearray\n" \
                "\tstring\n" \
                "\n" \
                "Example:\n" \
                "\twrite i16 60103e 0\n" \
                "\twrite float32 60103e 0\n" \
                "\twrite bytearray 60103e ff 01 32\n" \
                "\twrite string 60103e cheating\n"

bool handler__write(globals_t *vars, char **argv, unsigned argc);

#define OPTION_COMPLETE "scan_data_type{number,int,float," VALUE_TYPES \
    "},region_scan_level{1,2,3},dump_with_ascii{0,1},endianness{0,1,2}"
#define OPTION_SHRTDOC "set runtime options of scanmem, see `help option`"
#define OPTION_LONGDOC "usage: option <option_name> <option_value>\n" \
                 "\n" \
                 "Here are all options and their possible values\n" \
                 "\n" \
                 "scan_data_type\t\tspecify what type of data should be considered\n" \
                 "\t\t\tDefault:int\n" \
                 "\tMOST OF TIME YOU MUST EXECUTE `reset' IMMEDIATELY AFTER CHANGING scan_data_type\n" \
                 "\n" \
                 "\tPossible Values:\n" \
                 "\tnumber:\t\t\tinteger or float\n" \
                 "\tint:\t\t\tinteger of any width\n" \
                 "\tfloat:\t\t\tfloat of any width\n" \
                 "\tint{8|16|32|64}:\tinteger of given width\n" \
                 "\tfloat{32|64}:\t\tfloat of given width\n" \
                 "\tbytearray:\t\tan array of bytes\n" \
                 "\tstring:\t\t\tstring\n" \
                 "\n" \
                 "region_scan_level\tspecify which regions should be scanned\n" \
                 "\t\t\tDefault:2\n" \
                 "\n" \
                 "\tPossible Values:\n" \
                 "\t1:\theap, stack and executable only\n" \
                 "\t2:\theap, stack executable and bss only\n" \
                 "\t3:\teverything(e.g. other libs)\n" \
                 "\n" \
                 "dump_with_ascii\twhether to print ascii characters with a memory dump\n" \
                 "\t\t\tDefault:1\n" \
                 "\n" \
                 "\tpossible values:\n" \
                 "\t0:\tdisabled\n" \
                 "\t1:\tenabled\n" \
                 "\n" \
                 "endianness\tendianness of data (used by: set, write and comparisons)\n" \
                 "\t\t\tDefault:0\n" \
                 "\n" \
                 "\tpossible values:\n" \
                 "\t0:\thost endian\n" \
                 "\t1:\tlittle endian\n" \
                 "\t2:\tbig endian\n" \
                 "\n" \
                 "Example:\n" \
                 "\toption scan_data_type int32\n"

bool handler__option(globals_t *vars, char **argv, unsigned argc);

#endif /* HANDLERS_H */
