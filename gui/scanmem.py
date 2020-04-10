"""
    scanmem.py: python wrapper for libscanmem

    Copyright (C) 2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2018 Sebastian Parschauer <s.parschauer(a)gmx.de>
    Copyright (C) 2020 Andrea Stacchiotti <andreastacchiotti(a)gmail.com>

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
"""

import ctypes
import os
import re
import sys
import tempfile

import misc


class Scanmem():
    """Wrapper for libscanmem."""

    LIBRARY_FUNCS = {
        'sm_init': (ctypes.c_bool, ),
        'sm_cleanup': (None, ),
        'sm_set_backend': (None, ),
        'sm_backend_exec_cmd': (None, ctypes.c_char_p),
        'sm_get_num_matches': (ctypes.c_ulong, ),
        'sm_get_version': (ctypes.c_char_p, ),
        'sm_get_scan_progress': (ctypes.c_double, ),
        'sm_set_stop_flag': (None, ctypes.c_bool),
        'sm_process_is_dead': (ctypes.c_bool, ctypes.c_int32)
    }

    def __init__(self, libpath='libscanmem.so'):
        self._lib = ctypes.CDLL(libpath)
        self._init_lib_functions()
        self._lib.sm_set_backend()
        self._lib.sm_init()
        self.send_command('reset')
        self.version = misc.decode(self._lib.sm_get_version())

    def _init_lib_functions(self):
        for k, v in Scanmem.LIBRARY_FUNCS.items():
            f = getattr(self._lib, k)
            f.restype = v[0]
            f.argtypes = v[1:]

    def send_command(self, cmd, get_output=False):
        """
        Execute command using libscanmem.
        This function is NOT thread safe, send only one command at a time.

        cmd: command to run
        get_output: if True, return in a string what libscanmem would print to stdout
        """
        if get_output:
            with tempfile.TemporaryFile() as directed_file:
                backup_stdout_fileno = os.dup(sys.stdout.fileno())
                os.dup2(directed_file.fileno(), sys.stdout.fileno())

                self._lib.sm_backend_exec_cmd(ctypes.c_char_p(misc.encode(cmd)))

                os.dup2(backup_stdout_fileno, sys.stdout.fileno())
                os.close(backup_stdout_fileno)
                directed_file.seek(0)
                return directed_file.read()
        else:
            self._lib.sm_backend_exec_cmd(ctypes.c_char_p(misc.encode(cmd)))

    def get_match_count(self):
        return self._lib.sm_get_num_matches()

    def get_scan_progress(self):
        return self._lib.sm_get_scan_progress()

    def set_stop_flag(self, stop_flag):
        """
        Sets the flag to interrupt the current scan at the next opportunity
        """
        self._lib.sm_set_stop_flag(stop_flag)

    def exit_cleanup(self):
        """
        Frees resources allocated by libscanmem, should be called before disposing of this instance
        """
        self._lib.sm_cleanup()

    def process_is_dead(self, pid):
        return self._lib.sm_process_is_dead(pid)

    def matches(self):
        """
        Returns a generator of (match_id_str, addr_str, off_str, region_type,
        value, types_str) for each match, all strings.
        The function executes commands internally, it is NOT thread safe
        """
        list_bytes = self.send_command('list', get_output=True)
        lines = filter(None, misc.decode(list_bytes).split('\n'))

        line_regex = re.compile(
            r'^\[ *(\d+)\] +([\da-f]+), +\d+ \+ +([\da-f]+), +(\w+), (.*), +\[([\w ]+)\]$')
        for line in lines:
            yield line_regex.match(line).groups()
