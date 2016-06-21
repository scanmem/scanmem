#!/usr/bin/env python
"""
    GameConquerorBackend: communication with scanmem
    
    Copyright (C) 2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>

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

import sys
import os
import ctypes
import tempfile
from misc import u

class GameConquerorBackend():
    BACKEND_FUNCS = {
        'init' : (ctypes.c_bool, ),
        'set_backend' : (None, ),
        'backend_exec_cmd' : (None, ctypes.c_char_p),
        'get_num_matches' : (ctypes.c_long, ),
        'get_version' : (ctypes.c_char_p, ),
        'get_scan_progress' : (ctypes.c_double, ),
        'reset_scan_progress' : (None,)
    }

    def __init__(self):
        self.lib = ctypes.CDLL('libscanmem.so')
        self.init_lib_functions()
        self.lib.set_backend()
        self.lib.init()
        self.send_command('reset')
        self.version = ''

    def init_lib_functions(self):
        for k,v in GameConquerorBackend.BACKEND_FUNCS.items():
            f = getattr(self.lib, k)
            f.restype = v[0]
            f.argtypes = v[1:]

    # for scan command, we don't want get_output immediately
    def send_command(self, cmd, get_output = False):
        if get_output:
            with tempfile.TemporaryFile() as directed_file:
                backup_stdout_fileno = os.dup(sys.stdout.fileno())
                os.dup2(directed_file.fileno(), sys.stdout.fileno())

                self.lib.backend_exec_cmd(ctypes.c_char_p(cmd.encode()))

                os.dup2(backup_stdout_fileno, sys.stdout.fileno())
                os.close(backup_stdout_fileno)
                directed_file.seek(0)
                return directed_file.readlines()
        else:
            self.lib.backend_exec_cmd(ctypes.c_char_p(cmd.encode()))

    def get_match_count(self):
        return self.lib.get_num_matches()

    def get_version(self):
        return u(self.lib.get_version())

    def get_scan_progress(self):
        return self.lib.get_scan_progress()

    def reset_scan_progress(self):
        self.lib.reset_scan_progress()

