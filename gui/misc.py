"""
    Misc funtions for Game Conqueror
    
    Copyright (C) 2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2013 Mattias <mattiasmun(a)gmail.com>

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

from gi.repository import Gtk

# check syntax, data range etc.
# translate if neccesary
# return (success, msg)
# if success == True, msg is converted command
# else msg is error message
def check_scan_command (data_type, cmd, is_first_scan):
    if cmd == '':
        raise ValueError('No value provided')
    if data_type == 'string':
        return '" ' + cmd
    elif data_type == 'bytearray':
        cmd = cmd.strip() 
        bytes = cmd.split(' ')
        for byte in bytes:
            if byte.strip() == '':
                continue
            if len(byte) != 2:
                raise ValueError('Bad value: %s' % (byte, ))
            if byte == '??':
                continue
            try:
               _ = int(byte,16)
            except:
                raise ValueError('Bad value: %s' % (byte, ))
        return cmd
    else: # for numbers
        cmd = cmd.strip()
        # hack for snapshot
        if cmd == '?':
            return 'snapshot'

        is_operator_cmd = cmd in ['=', '!=', '>', '<', '+', '-']
        if not is_first_scan and is_operator_cmd:
            return cmd

        if is_first_scan and (is_operator_cmd or cmd[:2] in ['+ ', '- ']):
            raise ValueError('Command \"%s\" is not valid for the first scan' % (cmd[:2],))

        # evaluating the command
        if cmd[:2] in ['+ ', '- ', '> ', '< ']:
            num = cmd[2:]
            cmd = cmd[:2]
        elif cmd[:3] ==  '!= ':
            num = cmd[3:]
            cmd = cmd[:3]
        else:
            num = cmd
            cmd = ''
        num = eval_operand(num)
        cmd += str(num)

        if data_type.startswith('int'):
            if not (isinstance(num, int) or isinstance(num, long)):
                raise ValueError('%s is not an integer' % (num,))
            if data_type == 'int':
                width = 64
            else:
                width = int(data_type[len('int'):])
            if num > ((1<<width)-1) or num < -(1<<(width-1)):
                raise ValueError('%s is too bulky for %s' % (num, data_type))

        # finally
        return cmd

# evaluate the expression
def eval_operand(s):
    try:
        v = eval(s)
        if isinstance(v, (int,long,float)):
            return v
    except:
        pass

    raise ValueError('Bad value: %s' % (s,))

# convert [a,b,c] into a liststore that [[a],[b],[c]], where a,b,c are strings
def build_simple_str_liststore(l):
    r = Gtk.ListStore(str)
    for e in l:
        r.append([e])
    return r

# create a renderer for the combobox, set the text to `col`
def build_combobox(combobox, model, col=0):
    combobox.set_model(model)
    renderer = Gtk.CellRendererText()
    combobox.pack_start(renderer, True)
    combobox.add_attribute(renderer, 'text', col)

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
        raise ValueError('Cannot locate the item for %s'%(name,))
    combobox.set_active_iter(iter)

# append a column to `treeview`, with given `title`
# keyword parameters
#   renderer_class -- default: Gtk.CellRendererText
#   attributes --  if not None, will be applied to renderer
#   properties -- if not None, will be applied to renderer
#   signals -- if not None, will be connected to renderer
# the latter two should be a list of tuples, i.e.  ((name1, value1), (name2, value2))
def treeview_append_column(treeview, title, **kwargs):
    renderer_class = kwargs.get('renderer_class', Gtk.CellRendererText)
    attributes = kwargs.get('attributes')
    properties = kwargs.get('properties')
    signals = kwargs.get('signals')

    column = Gtk.TreeViewColumn(title)
    treeview.append_column(column)
    renderer = renderer_class()
    column.pack_start(renderer, True)
    if attributes:
        for k,v in attributes:
            column.add_attribute(renderer, k, v)
    if properties:
        for k,v in properties:
            renderer.set_property(k,v)
    if signals:
        for k,v in signals:
            renderer.connect(k,v)

# data is optional data to callback
def menu_append_item(menu, name, callback, data):
    item = Gtk.MenuItem(name)
    menu.append(item)
    item.connect('activate', callback, data)
