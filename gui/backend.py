#!/usr/bin/env python
"""
    GameConquerorBackend: communication with scanmem
    
    Copyright (C) 2010 Wang Lu <coolwanglu(a)gmail.com>

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

import subprocess
import tempfile
import re
import gobject

BACKEND = ['scanmem', '-b']
BACKEND_END_OF_OUTPUT_PATTERN = re.compile(r'(\d+)>\s*')
STDERR_MONITOR_INTERVAL = 100

class GameConquerorBackend():
    def __init__(self):
        self.match_count = 0
        self.backend = None
        self.stderr_monitor_id = None
        self.error_listeners = []
        self.progress_listeners = []
        self.version = ''
        self.restart()

    def add_error_listener(self, listener):
        self.error_listeners.append(listener)

    def add_progress_listener(self, listener):
        self.progress_listeners.append(listener)

    def restart(self):
        if self.backend is not None:
            self.backend.kill()
        self.last_pos = 0;
        self.stderrfile = tempfile.TemporaryFile()
        self.backend = subprocess.Popen(BACKEND, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderrfile.fileno())
        if self.stderr_monitor_id is not None:
            gobject.source_remove(self.stderr_monitor_id)
        self.stderr_monitor_id = gobject.timeout_add(STDERR_MONITOR_INTERVAL, self.stderr_monitor)
        # read initial info
        l = self.get_output_lines()
        if len(l) > 0:
            self.version = l[0].strip()

    def stderr_monitor(self):
        while True:
            self.stderrfile.seek(0, 2) # to the end
            if self.stderrfile.tell() == self.last_pos:
                break

            self.stderrfile.seek(self.last_pos, 0)
            msg = self.stderrfile.readline()
            self.last_pos = self.stderrfile.tell()
           
            if msg.startswith('error:'): # error happened
                msg = msg[len('error:'):]
                for listener in self.error_listeners:
                    listener(msg)
            elif msg.startswith('info:'):
                pass
            elif msg.startswith('warn:'):
                pass
            elif msg.startswith('scan_progress:'):
                try:
                    l = msg.split(' ')
                    if len(l) == 3:
                        cur = int(l[1])
                        total = int(l[2])
                        for listener in self.progress_listeners:
                            listener(cur, total)
                    else: # invalid scan_progress text
                        pass
                except:
                    pass
            else: # unknown type of message, maybe log it?
                pass
        return True

    def get_output_lines(self):
        lines = []
        while True:
            line = self.backend.stdout.readline()
            match = BACKEND_END_OF_OUTPUT_PATTERN.match(line)
            if match is None:
                lines.append(line)
            else:
                self.match_count = int(match.groups()[0])
                break
        return lines

    # for scan command, we don't want get_output immediately
    def send_command(self, cmd, get_output = True):
        # for debug
#        print 'Send Command:',cmd
        self.backend.stdin.write(cmd+'\n')
        if get_output:
            output_lines = self.get_output_lines()
            # for debug
#            print 'Output:\n'+'\n'.join(output_lines)
            return output_lines
        else:
            return []

    # for test only
    def run(self):
        while True:
            print '\n'.join(self.get_output_lines())
            l = sys.stdin.readline()
            self.backend.stdin.write(l)



