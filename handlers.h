/*
  $Id: handlers.h,v 1.13 2007-06-05 01:45:35+01 taviso Exp taviso $
*/

#ifndef _INC_HANDLERS
#define _INC_HANDLERS           /* include guard */

/*
 * SHRTDOC's are one line descriptions of the supported command (shown with `help`).
 * LONGDOC's are detailed descriptions (shown with `help command`) (wrap them before column 80).
 * 
 * The DOC's are passed to the registercommand() routine, and are read by the help
 * command. You can define SHRTDOC to NULL and help will not print it.
 * However, a NULL longdoc will print "missing documentation" if specific help is requested,
 * if you really dont want it to print anything, use "".
 *
 * Each handler is passed an argv and argc, argv[0] is the command called, along with
 * a pointer to the global settings structure, which contains various settings and lists
 * that handlers are allowed to change. One handler can handle multiple commands, but
 * you need register each command with its own documentation.
 */

#define SET_SHRTDOC "change known matches to specified value"
#define SET_LONGDOC "usage: set [match-id[,match-id,...]=]n[/d] [...]\n" \
					"Inject the value `n` into the match numbers `match-id`, or if just `n` is\n" \
               "specified, all known matches. `n` can be specified in standard C language\n" \
               "notation, a leading 0x for hexadecimal, leading 0 for octal, and everything\n" \
               "else is assumed to be decimal. All known matches, along with their match-ids\n" \
               "can be displayed with the `list` command. Multiple match-ids can be specified,\n" \
               "separated with commas and terminated with an '=' sign.\n" \
               "To set a value continually, for example to prevent a counter from decreasing,\n" \
               "suffix the command with '/', followed by the number of seconds to wait between\n" \
               "sets. Interrupt scanmem with ^C to stop the setting.\n\n" \
               "Note that this command cannot work for bytearray or string.\n\n" \
               "Examples:\n" \
               "\tset 10 - set all known matches to 10\n" \
               "\tset 0=0x03 - set match 0 to 0x03.\n" \
               "\tset 0,1,2,7=0x32 - set matches 0,1,2 and 7 to 0x32\n" \
               "\tset 0,3=42/8 - set matches 0 and 3 to 42 every 8 seconds\n" \
               "\tset 12,13,14=0x23/2 23,19=0/8 6=4 0 - complex example, can be combined" \

bool handler__set(globals_t * vars, char **argv, unsigned argc);

#define LIST_SHRTDOC "list all currently known matches"
#define LIST_LONGDOC "usage: list\n" \
               "Print all the currently known matches, along with details about the\n" \
               "match, such as its type, location, and last known value. The number in\n" \
               "the left column is the `match-id`, this can be passed to other commands\n" \
               "such as `set`, `delete`, etc.\n" \
               "The flags displayed indicate the possible types of the variable.\n" \
               "Also the region id, an offset and the region type belonging to a match\n" \
               "are displayed. The offset is used from the code load address or region start.\n" \
               "This helps bypassing address space layout randomization (ASLR).\n"

bool handler__list(globals_t * vars, char **argv, unsigned argc);

#define DELETE_SHRTDOC "delete a known match by match-id"
#define DELETE_LONGDOC "usage: delete match-id\n" \
                "Remove the match `match-id` from the match list. The `match-id` can\n" \
                "be found using the `list` command. To delete all matches, see\n" \
                "the `reset` command.\n" \
                "To delete all matches associated with a particular library, see the\n" \
                "`dregion` command, which will also remove any associated matches.\n" \
                "Example:\n" \
                "\tdelete 0 - delete match 0\n" \
                "NOTE: match-ids may be recalculated after matches are removed or added."

bool handler__delete(globals_t * vars, char **argv, unsigned argc);

#define RESET_SHRTDOC "forget all matches, and reinitialise regions"
#define RESET_LONGDOC "usage: reset\n" \
                "Forget all matches and regions, and reread regions from the relevant\n" \
                "maps file. Useful if you have made an error, or want to find a new\n" \
                "variable.\n"

bool handler__reset(globals_t * vars, char **argv, unsigned argc);

#define PID_SHRTDOC "print current pid, or attach to a new process"
#define PID_LONGDOC "usage: pid [pid]\n" \
                "If `pid` is specified, reset current session and then attach to new\n" \
                "process `pid`. If `pid` is not specified, print information about\n" \
                "current process."

bool handler__pid(globals_t * vars, char **argv, unsigned argc);

#define SNAPSHOT_SHRTDOC "take a snapshot of the current process state"
#define SNAPSHOT_LONGDOC "usage: snapshot\n" \
                "Take a snapshot of the entire process in its current state, this is useful\n" \
                "If you don't know the exact value of the variable you are searching for, but\n" \
                "can describe it in terms of higher, lower or equal (see commands `>`,`<` and\n" \
                "`=`).\n\n" \
                "NOTE: this can use a lot of memory with large processes."

bool handler__snapshot(globals_t * vars, char **argv, unsigned argc);

#define DREGION_SHRTDOC "delete a known region by region-id"
#define DREGION_LONGDOC "usage: dregion [!]region-id[,region-id[,...]]\n" \
                "Remove the region `region-id` from the regions list, along with any matches\n" \
                "affected from the match list. The `region-id` can be found using the `lregions`\n" \
                "command. A leading `!` indicates the list should be inverted.\n" 

bool handler__dregion(globals_t * vars, char **argv, unsigned argc);

#define LREGIONS_SHRTDOC "list all known regions"
#define LREGIONS_LONGDOC "usage: lregions\n" \
                "Print all the currently known regions, along with details such as the\n" \
                "start address, size, region type, load address, permissions and associated\n" \
                "filename. The number in the left column is the `region-id`, this can be\n" \
                "passed to other commands that process regions, such as `dregion`.\n" \
                "The load address is the start of the .text region for the executable\n" \
                "or libraries. Otherwise, it is the region start.\n"

bool handler__lregions(globals_t * vars, char **argv, unsigned argc);

#define GREATERTHAN_SHRTDOC "match values that have increased or greater than some number"
#define LESSTHAN_SHRTDOC "match values that have decreased or less than some number"
#define NOTCHANGED_SHRTDOC "match all variables that have changed since last scan"
#define CHANGED_SHRTDOC "match all variables that have not changed since last scan"
#define INCREASED_SHRTDOC "match values that have increased by some given number"
#define DECREASED_SHRTDOC "match values that have decreased by some given number"

#define GREATERTHAN_LONGDOC "usage: > [n]\n" \
                "If n is given, match values that are greater than n.\n" \
                "Otherwise all current matches that have not increased since the last scan are discarded. (same as `+`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."
#define LESSTHAN_LONGDOC "usage: < [n]\n" \
                "If n is given, match values that are less than n.\n" \
                "Otherwise all current matches that have not decreased since the last scan are discarded.(same as `-`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."
#define NOTCHANGED_LONGDOC "usage: =\n" \
                "All current matches that have changed since the last scan are discarded.\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."
#define CHANGED_LONGDOC "usage: !=\n" \
                "All current matches that have not changed since the last scan are discarded.\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."

#define INCREASED_LONGDOC "usage: + [n]\n" \
                "If n is given, match values that have been increased by n\n" \
                "Otherwise match all values that have increased. (same as `>`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."

#define DECREASED_LONGDOC "usage: - [n]\n" \
                "If n is given, match values that have been decreased by n\n" \
                "Otherwise match all values that have decreased. (same as `<`)\n" \
                "You can use this in conjunction with `snapshot` if you never know its value."


bool handler__decinc(globals_t * vars, char **argv, unsigned argc);

#define VERSION_SHRTDOC "print current version"
#define VERSION_LONGDOC "usage: version\n" \
                "Display the current version of scanmem in use."

bool handler__version(globals_t * vars, char **argv, unsigned argc);

#define EXIT_SHRTDOC "exit the program immediately"
#define EXIT_LONGDOC "usage: exit\n" \
                "Exit scanmem immediately, zero will be returned."

bool handler__exit(globals_t * vars, char **argv, unsigned argc);

#define HELP_SHRTDOC "access online documentation, use `help command` for specific help"
#define HELP_LONGDOC "usage: help [command]\n" \
                "If `command` is specified, print detailed information about command `command`\n" \
                "including options and usage summary. If `command` is not specified, print a\n" \
                "one line description of each command supported."

bool handler__help(globals_t * vars, char **argv, unsigned argc);

#define DEFAULT_SHRTDOC NULL
#define DEFAULT_LONGDOC "When searching for a number, use any notation in standard C language (leading 0x for\n" \
                "hexadecimal, leading 0 for octal, everything else is assumed to be decimal)\n" \
                "float numbers are also acceptable, but will be rounded if scanning integers\n" \
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

bool handler__default(globals_t * vars, char **argv, unsigned argc);

#define STRING_SHRTDOC "match a given string"
#define STRING_LONGDOC "usage \" <text>\n" \
                "<text> is counted since the 2nd character following the leading \"\n" \
                "This can only be used when scan_data_type is set to be string\n" \
                "Example:\n" \
                "\t\" Scan for string, spaces and ' \" are all acceptable.\n"

bool handler__string(globals_t * vars, char **argv, unsigned argc);

#define UPDATE_SHRTDOC "update match values without culling list"
#define UPDATE_LONGDOC "usage: update\n" \
                "Scans the current process, getting the current values of all matches.\n" \
                "These values can be viewed with `list`, and are also the old values that\n" \
                "scanmem compares to when using `>`, `<`, or `=`. This command is equivalent\n" \
                "to a search command that all current results match.\n"

bool handler__update(globals_t * vars, char **argv, unsigned argc);

bool handler__eof(globals_t * vars, char **argv, unsigned argc);

#define SHELL_SHRTDOC "execute a shell command without leaving scanmem"
#define SHELL_LONGDOC "usage: shell [shell-command]\n" \
                "Execute `shell-command` using /bin/sh then return, useful for reading man\n" \
                "pages, checking top, or making notes with an external editor.\n" \
                "Examples:\n" \
                "\tshell vi notes.txt\n" \
                "\tshell man scanmem\n" \
                "\tshell cat > notes.txt\n"

bool handler__shell(globals_t * vars, char **argv, unsigned argc);

#define WATCH_SHRTDOC "monitor the value of a memory location as it changes"
#define WATCH_LONGDOC "usage: watch [match-id]\n" \
                "Monitors the match `match-id`, by testing its value every second. When the\n" \
                "value changes, its new value is printed along with an timestamp. Interrupt\n" \
                "with ^C to stop monitoring.\n" \
                "Examples:\n" \
                "\twatch 12 - watch match 12 for any changes.\n"

bool handler__watch(globals_t * vars, char **argv, unsigned argc);

/*XXX: improve this */
#define SHOW_SHRTDOC "display information about scanmem."
#define SHOW_LONGDOC "usage: show <info>\n" \
                "Display information relating to <info>.\n"

bool handler__show(globals_t * vars, char **argv, unsigned argc);

#define DUMP_SHRTDOC "dump a memory region to screen or a file" 
#define DUMP_LONGDOC "usage: dump <address> <length> [<filename>]\n" \
                "\n" \
                "If <filename> is given, save the region of memory to the file \n" \
                "Otherwise display it in a human-readable format.\n"
    
bool handler__dump(globals_t * vars, char **argv, unsigned argc);

#define WRITE_SHRTDOC "change the value of a specific memory location"
#define WRITE_LONGDOC "usage: write <value_type> <address> <value>\n" \
                "\n" \
                "Write <value> into <address>\n" \
                "<value_type> should be one of:\n" \
                "\tint{8|16|32|64} (or i{8|16|32|64} for short)\n" \
                "\tfloat{32|64} (or f{32|64} for short)\n" \
                "\tbytearray\n" \
                "\n" \
                "Example:\n" \
                "\twrite i16 60103e 0\n" \
                "\twrite float32 60103e 0\n" \
                "\twrite bytearray 60103e ff 01 32\n"

bool handler__write(globals_t * vars, char **argv, unsigned argc);

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
                 "detect_reverse_change\twhether to (also) search for values that changes oppositely as given order\n" \
                 "\t\t\tDefault:0\n" \
                 "\tIf you want to use this feature, you can only search for INCREASED or DECREASED after initial search\n" \
                 "\n" \
                 "\tpossible values:\n" \
                 "\t0:\tdisabled\n" \
                 "\t1:\tenabled\n" \
                 "\n" \
                 "dump_with_ascii\twhether to print ascii characters with a memory dump\n" \
                 "\t\t\tDefault:1\n" \
                 "\n" \
                 "\tpossible values:\n" \
                 "\t0:\tdisabled\n" \
                 "\t1:\tenabled\n" \
                 "\n" \
                 "Example:\n" \
                 "\toption scan_data_type int32\n"

bool handler__option(globals_t * vars, char **argv, unsigned argc);
#endif
