"""
    Misc functions for Game Conqueror

    Copyright (C) 2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2013 Mattias <mattiasmun(a)gmail.com>
    Copyright (C) 2016 Andrea Stacchiotti <andreastacchiotti(a)gmail.com>

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

from gi.repository import Gtk

PY3K = sys.version_info >= (3, 0)

# check command syntax, data range etc.
# return a valid scanmem command
# raise if something is invalid


def check_scan_command(data_type, cmd, is_first_scan):
    if cmd == '':
        raise ValueError(_('No value provided'))
    if data_type == 'string':
        return '" ' + cmd

    cmd = cmd.strip()
    # hack for snapshot/update (TODO: make it possible with string)
    if cmd == '?':
        if is_first_scan:
            return 'snapshot'
        else:
            return 'update'

    if data_type == 'bytearray':
        bytes = cmd.split(' ')
        for byte in bytes:
            if byte.strip() == '':
                continue
            if len(byte) != 2:
                raise ValueError(_('Bad value: %s') % (byte, ))
            if byte == '??':
                continue
            try:
                _tmp = int(byte, 16)
            except BaseException:
                raise ValueError(_('Bad value: %s') % (byte, ))
        return cmd
    else:  # for numbers
        is_operator_cmd = cmd in {'=', '!=', '>', '<', '+', '-'}
        if not is_first_scan and is_operator_cmd:
            return cmd

        if is_first_scan and (is_operator_cmd or cmd[:2] in {'+ ', '- '}):
            raise ValueError(_('Command \"%s\" is not valid for the first scan') % (cmd[:2],))

        # evaluating the command
        range_nums = cmd.split("..")
        if len(range_nums) == 2:
            # range detected
            num_1 = eval_operand(range_nums[0])
            num_2 = eval_operand(range_nums[1])
            cmd = str(num_1) + ".." + str(num_2)
            check_int(data_type, num_1)
            check_int(data_type, num_2)
        else:
            # regular command processing
            if cmd[:2] in {'+ ', '- ', '> ', '< '}:
                num = cmd[2:]
                cmd = cmd[:2]
            elif cmd[:3] == '!= ':
                num = cmd[3:]
                cmd = cmd[:3]
            else:
                num = cmd
                cmd = ''
            num = eval_operand(num)
            cmd += str(num)
            check_int(data_type, num)

        # finally
        return cmd

# evaluate the expression


def eval_operand(s):
    try:
        v = eval(s)
        py2_long = not PY3K and isinstance(v, long)
        if isinstance(v, int) or isinstance(v, float) or py2_long:
            return v
    except BaseException:
        pass

    raise ValueError(_('Bad value: %s') % (s,))

# check if a number is a valid integer
# raise an exception if not


def check_int(data_type, num):
    if data_type.startswith('int'):
        py2_long = not PY3K and isinstance(num, long)
        if not (isinstance(num, int) or py2_long):
            raise ValueError(_('%s is not an integer') % (num,))
        if data_type == 'int':
            width = 64
        else:
            width = int(data_type[len('int'):])
        if num > ((1 << width) - 1) or num < -(1 << (width - 1)):
            raise ValueError(_('%s is too bulky for %s') % (num, data_type))
    return

# convert [a,b,c] into a liststore that [[a],[b],[c]], where a,b,c are strings


def build_simple_str_liststore(l):
    r = Gtk.ListStore(str)
    for e in l:
        r.append([e])
    return r

# set active item of the `combobox`
# such that the value at `col` is `name`


def combobox_set_active_item(combobox, name, col=0):
    model = combobox.get_model()
    iter = model.get_iter_first()
    while iter is not None:
        if model.get_value(iter, col) == name:
            break
        iter = model.iter_next(iter)
    if iter is None:
        raise ValueError(_('Cannot locate item: %s') % (name,))
    combobox.set_active_iter(iter)

# sort column according to datatype (callback for TreeView)


def value_compare(treemodel, iter1, iter2, user_data):
    sort_col, isnumeric = user_data

    string1 = treemodel.get_value(iter1, sort_col)
    string2 = treemodel.get_value(iter2, sort_col)

    if (isnumeric):
        val1 = float(string1)
        val2 = float(string2)
    else:
        val1 = string1
        val2 = string2

    if val1 > val2:
        return 1
    if val1 == val2:
        return 0
    return -1

# format number in base16 (callback for TreeView)


def format16(col, cell, model, iter, hex_col):
    cell.set_property("text", "%x" % model.get_value(iter, hex_col))

# append a column to `treeview`, with given `title`
# keyword parameters
#   renderer_class -- default: Gtk.CellRendererText
#   attributes --  if not None, will be applied to renderer
#   properties -- if not None, will be applied to renderer
#   signals -- if not None, will be connected to renderer
# the latter two should be a list of tuples, i.e.  ((name1, value1), (name2, value2))


def treeview_append_column(treeview, title, sort_id=None, resizable=True, hex_col=None, **kwargs):
    renderer_class = kwargs.get('renderer_class', Gtk.CellRendererText)
    attributes = kwargs.get('attributes')
    properties = kwargs.get('properties')
    signals = kwargs.get('signals')

    column = Gtk.TreeViewColumn(title)
    treeview.append_column(column)
    if sort_id is not None:
        column.set_sort_column_id(sort_id)
    column.set_resizable(resizable)
    renderer = renderer_class()
    column.pack_start(renderer, True)
    if hex_col is not None:
        column.set_cell_data_func(renderer, format16, hex_col)
    if attributes:
        for k, v in attributes:
            column.add_attribute(renderer, k, v)
    if properties:
        for k, v in properties:
            renderer.set_property(k, v)
    if signals:
        for k, v in signals:
            renderer.connect(k, v)

# data is optional data to callback


def menu_append_item(menu, name, callback, data=None):
    item = Gtk.MenuItem(label=name)
    menu.append(item)
    item.connect('activate', callback, data)

# Interface for bytes<>string conversion for py2/3
# Usage is the same you'd do in py3, call `decode` on external raw data
# and `encode` to work with the memory representation


def decode(raw_bytes, errors='strict'):
    if PY3K:
        return raw_bytes.decode(errors=errors)
    else:
        return str(raw_bytes)


def encode(unicode_string, errors='strict'):
    if PY3K:
        return unicode_string.encode(errors=errors)
    else:
        return unicode_string

# Convert codepoints to integers byte by byte


def str2bytes(string):
    if PY3K:
        return bytes(string)
    else:
        return map(ord, string)
