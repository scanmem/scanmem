/*
    show_message.h

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

/*
 * This file declares all types of output functions, in order to provide well-formatted messages that a front-end can understand
 *
 * Basically, all data go through stdout, and all message (to user or to front-end) go through stderr
 *
 * In stderr:
 *  all messages prefixed with 'error:' will be considered fatal error, front-end may notify user to re-start backend
 *  all messages prefixed with 'warn:' will be considered nonfatal error
 *  all messages prefixed with 'info:' will be ignored (by the front-end)
 *
 *  DO NOT output multi-line messages with above prefix, use multiple show_xxx instead
 *  And you may or may not append a '\n' to the message.
 *
 *  To display message to user only, use show_user, nothing will be prepended, and the message will be ignore if scanmem is running as a backend
 */

#ifndef SHOW_MESSAGE_H__
#define SHOW_MESSAGE_H__

/* prepend 'info: ', output to stderr */
void show_info(const char *fmt, ...);
/* prepend 'error: ', output to stderr */
void show_error(const char *fmt, ...);
/* prepend 'warn: ', output to stderr */
void show_warn(const char *fmt, ...);

/* display message only when in debug mode */
void show_debug(const char *fmt, ...);

/* display message only when not running as a backend */
void show_user(const char *fmt, ...);

/* display progress of scan */
void show_scan_progress(unsigned long cur, unsigned long total);

#endif /* SHOW_MESSAGE_H__ */
