"""
    Misc funtions for Game Conqueror
    
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

# check syntax, data range etc.
# translate if neccesary
# return (success, msg)
# if success == True, msg is converted command
# else msg is error message
def check_scan_command (data_type, cmd):
    if cmd == '':
        return (False, 'No value provided')
    if data_type == 'string':
        return (True, '" ' + cmd)
    elif data_type == 'bytearray':
        cmd = cmd.strip() 
        bytes = cmd.split(' ')
        for byte in bytes:
            if byte.strip() == '':
                continue
            if len(byte) != 2:
                return (False, 'Bad value: %s' % (byte, ))
            if byte == '??':
                continue
            try:
               _ = int(byte,16)
            except:
                return (False, 'Bad value: %s' % (byte, ))
        return (True, cmd)
    else: # for numbers
        cmd = cmd.strip() 
        # hack for snapshot
        if cmd == '?':
            return (True, 'snapshot')
        
        # these two commands has no parameters
        if cmd == '=' or cmd == '!=':
            return (True, cmd)

        cmdl = cmd.split()
        if len(cmdl) > 2:
            return (False, 'Too many parameters')
        
        if cmdl[0] in ['>', '<', '+', '-']:
            if len(cmdl) == 1:
                return (True, cmd)
            else:
                num_to_check = cmdl[1]
        else:
            if len(cmdl) > 1:
                return (False, 'Bad command %s' % (cmd,))
            else:
                num_to_check = cmdl[0]

        (cbn, iv, fv) = test_number(num_to_check)
        if data_type.startswith('int'):
            if iv is None:
                return (False, '%s is not an integer' % (num_to_check,))
            if data_type == 'int':
                width = 64
            else:
                width = int(data_type[len('int'):])
            if iv > ((1<<width)-1):
                return (False, '%s is too large for %s' % (num_to_check, data_type))
            if iv < -(1<<(width-1)):
                return (False, '%s is too small for %s' % (num_to_check, data_type))


        if data_type.startswith('float') and (cbf is None):
            return (False, '%s is not a float' % (num_to_check,))
        if data_type == 'number' and (not cbn):
            return (False, '%s is not a number' % (num_to_check,))

        # finally
        return (True, cmd)



# test if given string is a number
# return (could_be_number, int_value, float_value)
def test_number (string):
    int_value = None
    float_value = None
    try:
        int_value = int(string)
    except:
        pass
    if int_value is None:
        try:
            int_value = int(string, 16)
        except:
            pass
    try:
        float_value = float(string)
    except:
        pass
    return (((int_value is not None) or (float_value is not None)), int_value, float_value)



