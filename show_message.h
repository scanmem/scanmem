/*
    Message printing helper functions.

    Copyright (C) 2010 WANG Lu  <coolwanglu(a)gmail.com>

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

/*
 * This file declares all types of output functions, in order to provide well-formatted messages that a front-end can understand.
 *
 * Basically, all data goes through stdout, and all messages (to user or front-end) go through stderr.
 *
 * In stderr:
 *  all messages prefixed with 'error:' will be considered a fatal error; front-end may notify user to restart backend
 *  all messages prefixed with 'warn:' will be considered a nonfatal error.
 *  all messages prefixed with 'info:' will be ignored (by the front-end)
 *
 *  To display messages to user only, use show_user; nothing will be prepended, and the message will be ignored if scanmem is running as a backend.
 */

#ifndef SHOW_MESSAGE_H
#define SHOW_MESSAGE_H

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

#endif /* SHOW_MESSAGE_H */
