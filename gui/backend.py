"""
    GameConquerorBackend: communication with libscanmem
    
    Copyright (C) 2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2018 Sebastian Parschauer <s.parschauer(a)gmx.de>

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

import misc

class GameConquerorBackend():
    BACKEND_FUNCS = {
        'sm_init' : (ctypes.c_bool, ),
        'sm_cleanup' : (None, ),
        'sm_set_backend' : (None, ),
        'sm_backend_exec_cmd' : (None, ctypes.c_char_p),
        'sm_get_num_matches' : (ctypes.c_ulong, ),
        'sm_get_version' : (ctypes.c_char_p, ),
        'sm_get_scan_progress' : (ctypes.c_double, ),
        'sm_set_stop_flag' : (None, ctypes.c_bool),
        'sm_process_is_dead' : (ctypes.c_bool, ctypes.c_int32)
    }

    def __init__(self, libpath='libscanmem.so'):
        self.lib = ctypes.CDLL(libpath)
        self.init_lib_functions()
        self.lib.sm_set_backend()
        self.lib.sm_init()
        self.send_command('reset')
        self.version = ''

    def init_lib_functions(self):
        for k,v in GameConquerorBackend.BACKEND_FUNCS.items():
            f = getattr(self.lib, k)
            f.restype = v[0]
            f.argtypes = v[1:]

    # `get_output` will return in a string what libscanmem would print to stdout
    def send_command(self, cmd, get_output = False):
        if get_output:
            with tempfile.TemporaryFile() as directed_file:
                backup_stdout_fileno = os.dup(sys.stdout.fileno())
                os.dup2(directed_file.fileno(), sys.stdout.fileno())

                self.lib.sm_backend_exec_cmd(ctypes.c_char_p(misc.encode(cmd)))

                os.dup2(backup_stdout_fileno, sys.stdout.fileno())
                os.close(backup_stdout_fileno)
                directed_file.seek(0)
                return directed_file.read()
        else:

            self.lib.sm_backend_exec_cmd(ctypes.c_char_p(misc.encode(cmd)))

    def get_match_count(self):
        return self.lib.sm_get_num_matches()

    def get_version(self):
        return misc.decode(self.lib.sm_get_version())

    def get_scan_progress(self):
        return self.lib.sm_get_scan_progress()

    def set_stop_flag(self, stop_flag):
        self.lib.sm_set_stop_flag(stop_flag)

    def exit_cleanup(self):
        self.lib.sm_cleanup()

    def process_is_dead(self, pid):
        return self.lib.sm_process_is_dead(pid)
