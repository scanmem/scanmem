#!/usr/bin/env python
"""
    Game Conqueror: a graphical game cheating tool, using scanmem as its backend
    
    Copyright (C) 2009-2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2010 Bryan Cain
    Copyright (C) 2013 Mattias Muenster <mattiasmun(a)gmail.com>
    Copyright (C) 2014-2018 Sebastian Parschauer <s.parschauer(a)gmx.de>
    Copyright (C) 2016-2017 Andrea Stacchiotti <andreastacchiotti(a)gmail.com>
    
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
import argparse
import struct
import platform
import threading
import json

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GObject
from gi.repository import GLib

from consts import *
from hexview import HexView
from backend import GameConquerorBackend
import misc

import locale
import gettext

# In some locale, ',' is used in float numbers
locale.setlocale(locale.LC_NUMERIC, 'C')
locale.bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
gettext.install(GETTEXT_PACKAGE, LOCALEDIR, names=('_'));

CLIPBOARD = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
WORK_DIR = os.path.dirname(sys.argv[0])
PROGRESS_INTERVAL = 100 # for scan progress updates
DATA_WORKER_INTERVAL = 500 # for read(update)/write(lock)
HEXEDIT_SPAN = 1024 # hexview half-height
SCAN_RESULT_LIST_LIMIT = 10000 # maximal number of entries that can be displayed

SCAN_VALUE_TYPES = ['int', 'int8', 'int16', 'int32', 'int64', 'float', 'float32', 'float64', 'number', 'bytearray', 'string']

LOCK_FLAG_TYPES = misc.build_simple_str_liststore(['=', '+', '-'])

MEMORY_TYPES = ['int8', 'uint8',
                'int16', 'uint16',
                'int32', 'uint32',
                'int64', 'uint64',
                'float32', 'float64',
                'bytearray', 'string']

SEARCH_SCOPE_NAMES = [_('Basic'), _('Normal'), _('Full')]

# convert type names used by scanmem into ours
TYPENAMES_S2G = {'I64':'int64'
                ,'I64s':'int64'
                ,'I64u':'uint64'
                ,'I32':'int32'
                ,'I32s':'int32'
                ,'I32u':'uint32'
                ,'I16':'int16'
                ,'I16s':'int16'
                ,'I16u':'uint16'
                ,'I8':'int8'
                ,'I8s':'int8'
                ,'I8u':'uint8'
                ,'F32':'float32'
                ,'F64':'float64'
                ,'bytearray':'bytearray'
                ,'string':'string'
                }   

# convert our typenames into struct format characters
TYPENAMES_G2STRUCT = {'int8':'b'
                     ,'uint8':'B'
                     ,'int16':'h'
                     ,'uint16':'H'
                     ,'int32':'i'
                     ,'uint32':'I'
                     ,'int64':'q'
                     ,'uint64':'Q'
                     ,'float32':'f'
                     ,'float64':'d'
                     }
        
# sizes in bytes of integer and float types
TYPESIZES = {'int8':1
            ,'uint8':1
            ,'int16':2
            ,'uint16':2
            ,'int32':4
            ,'uint32':4
            ,'int64':8
            ,'uint64':8
            ,'float32':4
            ,'float64':8
            }

class GameConqueror():
    def __init__(self):
        ##################################
        # init GUI
        self.builder = Gtk.Builder()
        self.builder.set_translation_domain(GETTEXT_PACKAGE)
        self.builder.add_from_file(os.path.join(WORK_DIR, 'GameConqueror.ui'))

        self.main_window = self.builder.get_object('MainWindow')
        self.main_window.set_title('GameConqueror %s' % (VERSION,))
        self.about_dialog = self.builder.get_object('AboutDialog')
        # set version
        self.about_dialog.set_version(VERSION)

        self.process_list_dialog = self.builder.get_object('ProcessListDialog')
        self.addcheat_dialog = self.builder.get_object('AddCheatDialog')

        # init memory editor
        self.memoryeditor_window = self.builder.get_object('MemoryEditor_Window')
        self.memoryeditor_hexview = HexView()
        self.memoryeditor_window.get_child().pack_start(self.memoryeditor_hexview, True, True, 0)
        self.memoryeditor_hexview.show_all()
        self.memoryeditor_address_entry = self.builder.get_object('MemoryEditor_Address_Entry')
        self.memoryeditor_hexview.connect('char-changed', self.memoryeditor_hexview_char_changed_cb)

        self.found_count_label = self.builder.get_object('FoundCount_Label')
        self.process_label = self.builder.get_object('Process_Label')
        self.value_input = self.builder.get_object('Value_Input')
        
        self.scanoption_frame = self.builder.get_object('ScanOption_Frame')
        self.scanprogress_progressbar = self.builder.get_object('ScanProgress_ProgressBar')
        self.input_box = self.builder.get_object('Value_Input')

        self.scan_button = self.builder.get_object('Scan_Button')
        self.stop_button = self.builder.get_object('Stop_Button')
        self.reset_button = self.builder.get_object('Reset_Button')

        ###
        # Set scan data type
        self.scan_data_type_combobox = self.builder.get_object('ScanDataType_ComboBoxText')
        for entry in SCAN_VALUE_TYPES:
            self.scan_data_type_combobox.append_text(entry)
        # apply setting
        misc.combobox_set_active_item(self.scan_data_type_combobox, SETTINGS['scan_data_type'])

        ###
        # set search scope
        self.search_scope_scale = self.builder.get_object('SearchScope_Scale')
        # apply setting
        self.search_scope_scale.set_value(SETTINGS['search_scope'])



        # init scanresult treeview
        # we may need a cell data func here
        # create model
        self.scanresult_tv = self.builder.get_object('ScanResult_TreeView')
        # liststore contents:                     addr,                value, type, valid, offset,              region type, match_id
        self.scanresult_liststore = Gtk.ListStore(GObject.TYPE_UINT64, str,   str,  bool,  GObject.TYPE_UINT64, str,         int)
        self.scanresult_tv.set_model(self.scanresult_liststore)
        # init columns
        misc.treeview_append_column(self.scanresult_tv, _('Address'), 0, hex_col=0,
                                    attributes=(('text',0),),
                                    properties = (('family', 'monospace'),)
                                   )
        misc.treeview_append_column(self.scanresult_tv, _('Value'), 1,
                                    attributes=(('text',1),),
                                    properties = (('family', 'monospace'),)
                                   )
        misc.treeview_append_column(self.scanresult_tv, _('Offset'), 4, hex_col=4,
                                    attributes=(('text',4),),
                                    properties = (('family', 'monospace'),)
                                   )
        misc.treeview_append_column(self.scanresult_tv, _('Region Type'), 5,
                                    attributes=(('text',5),),
                                    properties = (('family', 'monospace'),)
                                   )

        # init CheatList TreeView
        self.cheatlist_tv = self.builder.get_object('CheatList_TreeView')
        # cheatlist contents:                    lockflag, locked, description, addr,                type, value, valid
        self.cheatlist_liststore = Gtk.ListStore(str,      bool,   str,         GObject.TYPE_UINT64, str,  str,   bool)
        self.cheatlist_tv.set_model(self.cheatlist_liststore)
        self.cheatlist_editing = False
        # Lock Flag
        misc.treeview_append_column(self.cheatlist_tv, '', 0
                                        ,renderer_class = Gtk.CellRendererCombo
                                        ,attributes = (('text',0),)
                                        ,properties = (('editable', True)
                                                      ,('has-entry', False)
                                                      ,('model', LOCK_FLAG_TYPES)
                                                      ,('text-column', 0)
                                        )
                                        ,signals = (('edited', self.cheatlist_toggle_lock_flag_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )
        # Lock
        misc.treeview_append_column(self.cheatlist_tv, _('Lock'), 1
                                        ,renderer_class = Gtk.CellRendererToggle
                                        ,attributes = (('active',1),)
                                        ,properties = (('activatable', True)
                                                      ,('radio', False)
                                                      ,('inconsistent', False)) 
                                        ,signals = (('toggled', self.cheatlist_toggle_lock_cb),)
                                   )
        # Description
        misc.treeview_append_column(self.cheatlist_tv, _('Description'), 2
                                        ,attributes = (('text',2),)
                                        ,properties = (('editable', True),)
                                        ,signals = (('edited', self.cheatlist_edit_description_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )
        # Address
        misc.treeview_append_column(self.cheatlist_tv, _('Address'), 3, hex_col=3
                                        ,attributes = (('text',3),)
                                        ,properties = (('family', 'monospace'),)
                                   )
        # Type
        misc.treeview_append_column(self.cheatlist_tv, _('Type'), 4
                                        ,renderer_class = Gtk.CellRendererCombo
                                        ,attributes = (('text',4),)
                                        ,properties = (('editable', True)
                                                      ,('has-entry', False)
                                                      ,('model', misc.build_simple_str_liststore(MEMORY_TYPES))
                                                      ,('text-column', 0))
                                        ,signals = (('edited', self.cheatlist_edit_type_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )
        # Value 
        misc.treeview_append_column(self.cheatlist_tv, _('Value'), 5
                                        ,attributes = (('text',5),)
                                        ,properties = (('editable', True)
                                                      ,('family', 'monospace'))
                                        ,signals = (('edited', self.cheatlist_edit_value_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )

        # init ProcessList
        self.processfilter_input = self.builder.get_object('ProcessFilter_Input')
        self.userfilter_input = self.builder.get_object('UserFilter_Input')
        # init ProcessList_TreeView
        self.processlist_tv = self.builder.get_object('ProcessList_TreeView')
        self.processlist_liststore = Gtk.ListStore(int, str, str)
        self.processlist_filter = self.processlist_liststore.filter_new(root=None)
        self.processlist_filter.set_visible_func(self.processlist_filter_func, data=None)
        self.processlist_tv.set_model(Gtk.TreeModelSort(model=self.processlist_filter))
        self.processlist_tv.set_search_column(2)

        # first col
        misc.treeview_append_column(self.processlist_tv, 'PID', 0
                                        ,attributes = (('text',0),)
                                   )
        # second col
        misc.treeview_append_column(self.processlist_tv, _('User'), 1
                                        ,attributes = (('text',1),)
                                   )

        # third col
        misc.treeview_append_column(self.processlist_tv, _('Process'), 2
                                        ,attributes = (('text',2),)
                                   )


        # get list of things to be disabled during scan
        self.disablelist = [self.cheatlist_tv,
                            self.scanresult_tv,
                            self.builder.get_object('processGrid'),
                            self.value_input,
                            self.reset_button,
                            self.builder.get_object('buttonGrid'),
                            self.memoryeditor_window]


        # init AddCheatDialog
        self.addcheat_address_input = self.builder.get_object('Address_Input')
        self.addcheat_address_input.override_font(gi.repository.Pango.FontDescription("Monospace"))

        self.addcheat_description_input = self.builder.get_object('Description_Input')
        self.addcheat_length_spinbutton = self.builder.get_object('Length_SpinButton')

        self.addcheat_type_combobox = self.builder.get_object('Type_ComboBoxText')
        for entry in MEMORY_TYPES:
            self.addcheat_type_combobox.append_text(entry)
        misc.combobox_set_active_item(self.addcheat_type_combobox, SETTINGS['lock_data_type'])
        self.Type_ComboBoxText_changed_cb(self.addcheat_type_combobox)


        # init popup menu for scanresult
        self.scanresult_popup = Gtk.Menu()
        misc.menu_append_item(self.scanresult_popup, _('Add to cheat list'), self.scanresult_popup_cb, 'add_to_cheat_list')
        misc.menu_append_item(self.scanresult_popup, _('Browse this address'), self.scanresult_popup_cb, 'browse_this_address')
        misc.menu_append_item(self.scanresult_popup, _('Scan for this address'), self.scanresult_popup_cb, 'scan_for_this_address')
        misc.menu_append_item(self.scanresult_popup, _('Remove this match'), self.scanresult_delete_selected_matches)
        self.scanresult_popup.show_all()

        # init popup menu for cheatlist
        self.cheatlist_popup = Gtk.Menu()
        misc.menu_append_item(self.cheatlist_popup, _('Browse this address'), self.cheatlist_popup_cb, 'browse_this_address')
        misc.menu_append_item(self.cheatlist_popup, _('Copy address'), self.cheatlist_popup_cb, 'copy_address')
        misc.menu_append_item(self.cheatlist_popup, _('Remove this entry'), self.cheatlist_popup_cb, 'remove_entry')
        self.cheatlist_popup.show_all()

        self.builder.connect_signals(self)
        self.main_window.connect('destroy', self.exit)

        ###########################
        # init others (backend, flag...)
        self.pid = 0 # target pid
        self.maps = []
        self.is_scanning = False
        self.exit_flag = False # currently for data_worker only, other 'threads' may also use this flag

        self.backend = GameConquerorBackend(os.path.join(LIBDIR, 'libscanmem.so.1'))
        self.check_backend_version()
        self.is_first_scan = True
        GLib.timeout_add(DATA_WORKER_INTERVAL, self.data_worker)
        self.command_lock = threading.RLock()
        self.progress_watcher_id = None


    ###########################
    # GUI callbacks

    # Memory editor

    def MemoryEditor_Button_clicked_cb(self, button, data=None):
        if self.pid == 0:
            self.show_error(_('Please select a process'))
            return
        self.browse_memory()
        return True

    def MemoryEditor_Handle_Address_cb(self, widget, data=None):
        txt = self.memoryeditor_address_entry.get_text().strip()
        if txt == '':
            return
        try:
            addr = int(txt, 16)
            self.browse_memory(addr)
        except:
            self.show_error(_('Invalid address'))

    # Manually add cheat

    def ConfirmAddCheat_Button_clicked_cb(self, button, data=None):
        addr = self.addcheat_address_input.get_text()
        try:
            addr = int(addr, 16)
            addr = GObject.Value(GObject.TYPE_UINT64, addr)
        except (ValueError, OverflowError):
            self.show_error(_('Please enter a valid address.'))
            return False

        description = self.addcheat_description_input.get_text()
        if not description: description = _('No Description')

        typestr = self.addcheat_type_combobox.get_active_text()
        length = self.addcheat_length_spinbutton.get_value_as_int()
        if 'int' in typestr: value = 0
        elif 'float' in typestr: value = 0.0
        elif typestr == 'string': value = ' ' * length
        elif typestr == 'bytearray': value = '00 ' * length
        else: value = None

        self.add_to_cheat_list(addr, value, typestr, description)
        self.addcheat_dialog.hide()
        return True

    def CloseAddCheat_Button_clicked_cb(self, button, data=None):
        self.addcheat_dialog.hide()
        return True

    # Main window

    def ManuallyAddCheat_Button_clicked_cb(self, button, data=None):
        self.addcheat_dialog.show()
        return True

    def RemoveAllCheat_Button_clicked_cb(self, button, data=None):
        self.cheatlist_liststore.clear()
        return True

    def LoadCheat_Button_clicked_cb(self, button, data=None):
        dialog = Gtk.FileChooserDialog(title=_("Open.."),
                                       transient_for=self.main_window,
                                       action=Gtk.FileChooserAction.OPEN,
                                       buttons=(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                                                Gtk.STOCK_OPEN, Gtk.ResponseType.OK))
        dialog.set_default_response(Gtk.ResponseType.OK)

        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            try:
                with open(dialog.get_filename(), 'r') as f:
                    obj = json.load(f)
                    for row in obj['cheat_list']:
                        self.add_to_cheat_list(row[3],row[5],row[4],row[2],True)
            except:
                pass
        dialog.destroy()
        return True

    def SaveCheat_Button_clicked_cb(self, button, data=None):
        dialog = Gtk.FileChooserDialog(title=_("Save.."),
                                       transient_for=self.main_window,
                                       action=Gtk.FileChooserAction.SAVE,
                                       buttons=(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                                                Gtk.STOCK_SAVE, Gtk.ResponseType.OK))
        dialog.set_default_response(Gtk.ResponseType.OK)
        dialog.set_do_overwrite_confirmation(True)

        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            try:
                with open(dialog.get_filename(), 'w') as f:
                    obj = {'cheat_list' : [list(i) for i in self.cheatlist_liststore]}
                    json.dump(obj, f);
            except:
                pass
        dialog.destroy()
        return True

    def SearchScope_Scale_format_value_cb(self, scale, value, data=None):
        return SEARCH_SCOPE_NAMES[int(value)]

    def Value_Input_activate_cb(self, entry, data=None):
        self.do_scan()
        return True

    def ScanResult_TreeView_popup_menu_cb(self, widget, data=None):
        pathlist = self.scanresult_tv.get_selection().get_selected_rows()[1]
        if len(pathlist):
            self.scanresult_popup.popup(None, None, None, None, 0, 0)
            return True
        return False

    def ScanResult_TreeView_button_press_event_cb(self, widget, event, data=None):
        # add to cheat list
        (model, pathlist) = self.scanresult_tv.get_selection().get_selected_rows()
        if event.button == 1 and event.get_click_count()[1] > 1: # left double click
            for path in pathlist:
                (addr, value, typestr) = model.get(model.get_iter(path), 0, 1, 2)
                self.add_to_cheat_list(addr, value, typestr)
        elif event.button == 3: # right click
            path = self.scanresult_tv.get_path_at_pos(int(event.x),int(event.y))
            if path is not None:
                self.scanresult_popup.popup(None, None, None, None, event.button, event.get_time())
                return path[0] in pathlist
        return False

    def CheatList_TreeView_button_press_event_cb(self, widget, event, data=None):
        if event.button == 3: # right click
            pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
            path = self.cheatlist_tv.get_path_at_pos(int(event.x),int(event.y))
            if path is not None:
                self.cheatlist_popup.popup(None, None, None, None, event.button, event.get_time())
                return path[0] in pathlist
        return False

    def CheatList_TreeView_popup_menu_cb(self, widget, data=None):
        pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
        if len(pathlist):
            self.cheatlist_popup.popup(None, None, None, None, 0, 0)
            return True
        return False

    def Scan_Button_clicked_cb(self, button, data=None):
        self.do_scan()
        return True

    def Stop_Button_clicked_cb(self, button, data=None):
        self.backend.set_stop_flag(True)
        return True

    def Reset_Button_clicked_cb(self, button, data=None):
        self.reset_scan()
        return True

    def Logo_EventBox_button_release_event_cb(self, widget, data=None):
        self.about_dialog.run()
        self.about_dialog.hide()
        return True

    # Process list

    def ProcessFilter_Input_changed_cb(self, widget, data=None):
        self.ProcessList_Refilter_Generic()

    def UserFilter_Input_changed_cb(self, widget, data=None):
        self.ProcessList_Refilter_Generic()

    def ProcessList_Refilter_Generic(self):
        self.processlist_filter.refilter()
        self.processlist_tv.set_cursor(0)

    def ProcessList_TreeView_row_activated_cb(self, treeview, path, view_column, data=None):
        (model, iter) = self.processlist_tv.get_selection().get_selected()
        if iter is not None:
            (pid, process) = model.get(iter, 0, 2)
            self.select_process(pid, process)
            self.process_list_dialog.response(Gtk.ResponseType.CANCEL)
            return True
        return False

    def SelectProcess_Button_clicked_cb(self, button, data=None):
        self.processlist_liststore.clear()
        pl = self.get_process_list()
        for p in pl:
            self.processlist_liststore.append([p[0], (p[1] if len(p) > 1 else _('<unknown>')), (p[2] if len(p) > 2 else _('<unknown>'))])
        self.process_list_dialog.show()
        while True:
            res = self.process_list_dialog.run()
            if res == Gtk.ResponseType.OK: # -5
                (model, iter) = self.processlist_tv.get_selection().get_selected()
                if iter is None:
                    self.show_error(_('Please select a process'))
                    continue
                else:
                    (pid, process) = model.get(iter, 0, 2)
                    self.select_process(pid, process)
                    break
            else: # for None and Cancel
                break
        self.process_list_dialog.hide()
        return True

    #######################
    # customed callbacks
    # (i.e. not standard event names are used)

    # Callback to hide window when 'X' button is pressed
    def hide_window_on_delete_event_cb(self, widget, event, data=None):
        widget.hide()
        return True

    # Memory editor

    def memoryeditor_hexview_char_changed_cb(self, hexview, offset, charval):
        addr = hexview.base_addr + offset
        self.write_value(addr, 'int8', charval)
        # return False such that the byte the default handler will be called, and will be displayed correctly 
        return False

    def memoryeditor_key_press_event_cb(self, window, event, data=None):
        keycode = event.keyval
        pressedkey = Gdk.keyval_name(keycode)
        if pressedkey == 'w' and (event.state & Gdk.ModifierType.CONTROL_MASK):
            self.memoryeditor_window.hide()
        elif pressedkey == 'Escape':
            self.memoryeditor_window.hide()

    def MemoryEditor_Refresh_Button_clicked_cb(self, button, data=None):
        dlength = len(self.memoryeditor_hexview.payload)
        data = self.read_memory(self.memoryeditor_hexview.base_addr, dlength)
        if data is None:
            self.memoryeditor_window.hide()
            self.show_error(_('Cannot read memory'))
            return
        old_addr = self.memoryeditor_hexview.get_current_addr()
        self.memoryeditor_hexview.payload = misc.str2bytes(data)
        self.memoryeditor_hexview.show_addr(old_addr)


    # Manually add cheat

    def focus_on_next_widget_cb(self, widget, data=None):
        widget.get_toplevel().child_focus(Gtk.DirectionType.TAB_FORWARD)
        return True

    def Type_ComboBoxText_changed_cb(self, combo_box):
        data_type = combo_box.get_active_text()
        if data_type in TYPESIZES:
            self.addcheat_length_spinbutton.set_value(TYPESIZES[data_type])
            self.addcheat_length_spinbutton.set_sensitive(False)
        else:
            self.addcheat_length_spinbutton.set_sensitive(True)

    # Main window

    def cheatlist_edit_start(self, a, b, c):
        self.cheatlist_editing = True
    def cheatlist_edit_cancel(self, a):
        self.cheatlist_editing = False

    def scanresult_delete_selected_matches(self, menuitem, data=None):
        (model, pathlist) = self.scanresult_tv.get_selection().get_selected_rows()
        match_id_list = ','.join(str(model.get_value(model.get_iter(path), 6)) for path in pathlist)
        self.command_lock.acquire()
        self.backend.send_command('delete {}'.format(match_id_list))
        self.update_scan_result()
        self.command_lock.release()

    def scanresult_popup_cb(self, menuitem, data=None):
        (model, pathlist) = self.scanresult_tv.get_selection().get_selected_rows()
        if data == 'add_to_cheat_list':
            for path in reversed(pathlist):
                (addr, value, typestr) = model.get(model.get_iter(path), 0, 1, 2)
                self.add_to_cheat_list(addr, value, typestr)
            return True
        addr = model.get_value(model.get_iter(pathlist[0]), 0)
        if data == 'browse_this_address':
            self.browse_memory(addr)
            return True
        elif data == 'scan_for_this_address':
            self.scan_for_addr(addr)
            return True
        return False

    def value_input_key_press_event_cb(self, main_window, event, data=None):
        keycode = event.keyval
        pressedkey = Gdk.keyval_name(keycode)
        if pressedkey == 'j' and (event.state & Gdk.ModifierType.CONTROL_MASK):
            if self.cheatlist_tv.is_focus() == self.scanresult_tv.is_focus():
                self.scanresult_tv.grab_focus()
                self.scanresult_tv.set_cursor(0)
            else:
                self.value_input.grab_focus()

    def ScanResult_TreeView_key_press_event_cb(self, scanresult_tv, event, data=None):
        keycode = event.keyval
        pressedkey = Gdk.keyval_name(keycode)
        if pressedkey == 'Return':
            (model, pathlist) = self.scanresult_tv.get_selection().get_selected_rows()
            for path in reversed(pathlist):
                (addr, value, typestr) = model.get(model.get_iter(path), 0, 1, 2)
                self.add_to_cheat_list(addr, value, typestr)
        elif pressedkey in {'Delete', 'BackSpace'}:
            self.scanresult_delete_selected_matches(None)
        elif pressedkey == 'j' and (event.state & Gdk.ModifierType.CONTROL_MASK):
            self.cheatlist_tv.grab_focus()
            if self.cheatlist_tv.get_cursor()[0] is not None:
                curpos = self.cheatlist_tv.get_cursor()[0]
                valcol = self.cheatlist_tv.get_column(5)
                self.cheatlist_tv.set_cursor(curpos, valcol)

    def CheatList_TreeView_key_press_event_cb(self, cheatlist_tv, event, data=None):
        keycode = event.keyval
        pressedkey = Gdk.keyval_name(keycode)
        if pressedkey in {'Delete', 'BackSpace'}:
            (model, pathlist) = self.cheatlist_tv.get_selection().get_selected_rows()
            for path in reversed(pathlist):
                self.cheatlist_liststore.remove(model.get_iter(path))
        elif pressedkey == 'j' and (event.state & Gdk.ModifierType.CONTROL_MASK):
            self.scanresult_tv.grab_focus()
            if self.scanresult_tv.get_cursor()[0] is not None:
                self.scanresult_tv.set_cursor(0)

    def cheatlist_popup_cb(self, menuitem, data=None):
        self.cheatlist_editing = False
        (model, pathlist) = self.cheatlist_tv.get_selection().get_selected_rows()
        if data == 'remove_entry':
            for path in reversed(pathlist):
                self.cheatlist_liststore.remove(model.get_iter(path)) 
            return True
        addr = model.get_value(model.get_iter(pathlist[0]), 3)
        if data == 'browse_this_address':
            self.browse_memory(addr)
            return True
        elif data == 'copy_address':
            addr = '%x' %(addr,)
            CLIPBOARD.set_text(addr, len(addr))
            return True
        return False

    def cheatlist_toggle_lock(self, row):
        if self.cheatlist_liststore[row][6]: # valid
            locked = self.cheatlist_liststore[row][1]
            locked = not locked
            self.cheatlist_liststore[row][1] = locked
        if locked:
            #TODO: check value(valid number & not overflow), if failed, unlock it and do nothing
            pass
        else:
            #TODO: update its value?
            pass
        return True

    def cheatlist_toggle_lock_cb(self, cellrenderertoggle, row_str, data=None):
        pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
        if not row_str:
            return True
        cur_row = int(row_str)
        # check if the current row is part of the selection
        found = False
        for path in pathlist:
            row = path[0]
            if row == cur_row:
                found = True
                break
        if not found:
            self.cheatlist_toggle_lock(cur_row)
            return True
        # the current row is part of the selection
        for path in pathlist:
            row = path[0]
            self.cheatlist_toggle_lock(row)
        return True

    def cheatlist_toggle_lock_flag_cb(self, cell, path, new_text, data=None):
        self.cheatlist_editing = False
        # currently only one lock flag is supported
        return True
        row = int(path)
        self.cheatlist_liststore[row][0] = new_text
        # data_worker will handle this later
        return True

    def cheatlist_edit_description_cb(self, cell, path, new_text, data=None):
        self.cheatlist_editing = False
        pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
        for path in pathlist:
            row = path[0]
            self.cheatlist_liststore[row][2] = new_text
        return True

    def cheatlist_edit_value_cb(self, cell, path, new_text, data=None):
        self.cheatlist_editing = False
        # ignore empty value
        if new_text == '':
            return True
        pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
        for path in pathlist:
            row = path[0]
            if not self.cheatlist_liststore[row][6]: #not valid
                continue
            self.cheatlist_liststore[row][5] = new_text
            if self.cheatlist_liststore[row][1]: # locked
                # data_worker will handle this
                pass
            else:
                (addr, typestr, value) = self.cheatlist_liststore[row][3:6]
                self.write_value(addr, typestr, value)
        return True

    def cheatlist_edit_type_cb(self, cell, path, new_text, data=None):
        self.cheatlist_editing = False
        pathlist = self.cheatlist_tv.get_selection().get_selected_rows()[1]
        for path in pathlist:
            row = path[0]
            (addr, typestr, value) = self.cheatlist_liststore[row][3:6]
            if new_text == typestr:
                continue
            if new_text in {'bytearray', 'string'}:
                self.cheatlist_liststore[row][5] = self.bytes2value(new_text, self.read_memory(addr, self.get_type_size(typestr, value)))
            self.cheatlist_liststore[row][4] = new_text
            self.cheatlist_liststore[row][1] = False # unlock
        return True

    # Process list

    def processlist_filter_func(self, model, iter, data=None):
        (user, process) = model.get(iter, 1, 2)
        return process is not None and \
                self.processfilter_input.get_text().lower() in process.lower() and \
                user is not None and \
                self.userfilter_input.get_text().lower() in user.lower()
        


    ############################
    # core functions
    def show_error(self, msg):
        dialog = Gtk.MessageDialog(transient_for=self.main_window,
                                   modal=True,
                                   message_type=Gtk.MessageType.ERROR,
                                   buttons=Gtk.ButtonsType.OK,
                                   text=msg)
        dialog.run()
        dialog.destroy()

    # return None if unknown
    def get_pointer_width(self):
        bits = platform.architecture()[0]
        if not bits.endswith('bit'):
            return None
        try:
            bitn = int(bits[:-len('bit')])
            if bitn not in {8,16,32,64}:
                return None
            else:
                return bitn
        except:
            return None
        
    # return the size in bytes of the value in memory
    def get_type_size(self, typename, value):
        if typename in TYPESIZES: # int or float type; fixed length
            return TYPESIZES[typename]
        elif typename == 'bytearray':
            return (len(value.strip())+1)/3
        elif typename == 'string':
            return len(misc.encode(value))
        return None

    # parse bytes dumped by scanmem into number, string, etc.
    def bytes2value(self, typename, databytes):
        if databytes is None:
            return None
        if typename in TYPENAMES_G2STRUCT:
            return struct.unpack(TYPENAMES_G2STRUCT[typename], databytes)[0]
        elif typename == 'string':
            return misc.decode(databytes, 'replace')
        elif typename == 'bytearray':
            databytes = misc.str2bytes(databytes)
            return ' '.join(['%02x' %(i,) for i in databytes])
        else:
            return databytes

    def scan_for_addr(self, addr):
        bits = self.get_pointer_width()
        if bits is None:
            self.show_error(_('Unknown architecture, you may report to developers'))
            return
        self.reset_scan()
        self.value_input.set_text('%#x'%(addr,))
        misc.combobox_set_active_item(self.scan_data_type_combobox, 'int%d'%(bits,))
        self.do_scan()

    def browse_memory(self, addr=None):
        # select a region contains addr
        try:
            self.read_maps()
        except:
            self.show_error(_('Cannot retrieve memory maps of that process, maybe it has '
                              'exited (crashed), or you don\'t have enough privileges'))
            return
        selected_region = None
        if addr is not None:
            for m in self.maps:
                if m['start_addr'] <= addr and addr < m['end_addr']:
                    selected_region = m
                    break
            if selected_region:
                if selected_region['flags'][0] != 'r': # not readable
                    self.show_error(_('Address %x is not readable') % (addr,))
                    return
            else:
                self.show_error(_('Address %x is not valid') % (addr,))
                return
        else:
            # just select the first readable region
            for m in self.maps:
                if m['flags'][0] == 'r':
                    selected_region = m
                    break
            if selected_region is None:
                self.show_error(_('Cannot find a readable region'))
                return
            addr = selected_region['start_addr']

        # read region if possible
        start_addr = max(addr - HEXEDIT_SPAN, selected_region['start_addr'])
        end_addr = min(addr + HEXEDIT_SPAN, selected_region['end_addr'])
        data = self.read_memory(start_addr, end_addr - start_addr)
        if data is None:
            self.show_error(_('Cannot read memory'))
            return
        self.memoryeditor_hexview.payload = misc.str2bytes(data)
        self.memoryeditor_hexview.base_addr = start_addr
        
        # set editable flag
        self.memoryeditor_hexview.editable = (selected_region['flags'][1] == 'w')

        if addr is not None:
            self.memoryeditor_hexview.show_addr(addr)
        self.memoryeditor_window.show()

    # this callback will be called from other thread
    def progress_watcher(self):
        Gdk.threads_enter()
        self.scanprogress_progressbar.set_fraction(self.backend.get_scan_progress())
        Gdk.threads_leave()
        return True

    def add_to_cheat_list(self, addr, value, typestr, description=_('No Description'), at_end=False):
        # determine longest possible type
        types = typestr.split()
        vt = typestr
        for t in types:
            if t in TYPENAMES_S2G:
                vt = TYPENAMES_S2G[t]
                break
        if at_end:
            self.cheatlist_liststore.append(['=', False, description, addr, vt, str(value), True])
        else:
            self.cheatlist_liststore.prepend(['=', False, description, addr, vt, str(value), True])

    def get_process_list(self):
        plist = []
        for proc in os.popen('ps -wweo pid=,user:16=,command= --sort=-pid').readlines():
            (pid, user, pname) = [tok.strip() for tok in proc.split(None, 2)]
            plist.append((int(pid), user, pname))
        return plist

    def select_process(self, pid, process_name):
        # ask backend for attaching the target process
        # update 'current process'
        # reset flags
        # for debug/log
        self.pid = pid
        try:
            self.read_maps()
        except:
            self.pid = 0
            self.process_label.set_text(_('No process selected'))
            self.process_label.set_property('tooltip-text', _('Select a process'))
            self.show_error(_('Cannot retrieve memory maps of that process, maybe it has '
                              'exited (crashed), or you don\'t have enough privileges'))
        else:
            self.process_label.set_text('%d - %s' % (pid, process_name))
            self.process_label.set_property('tooltip-text', process_name)

        self.command_lock.acquire()
        self.backend.send_command('pid %d' % (pid,))
        self.reset_scan()
        self.command_lock.release()

        # unlock all entries in cheat list
        for i in range(len(self.cheatlist_liststore)):
            self.cheatlist_liststore[i][1] = False

    def read_maps(self):
        lines = open('/proc/%d/maps' % (self.pid,)).readlines()
        self.maps = []
        for l in lines:
            item = {}
            info = l.split(' ', 5)
            addr = info[0]
            idx = addr.index('-')
            item['start_addr'] = int(addr[:idx],16)
            item['end_addr'] = int(addr[idx+1:],16)
            item['size'] = item['end_addr'] - item['start_addr']
            item['flags'] = info[1]
            item['offset'] = info[2]
            item['dev'] = info[3]
            item['inode'] = int(info[4])
            if len(info) < 6:
                item['pathname'] = ''
            else:
                item['pathname'] = info[5].lstrip() # don't use strip
            self.maps.append(item)

    def reset_scan(self):
        # reset search type and value type
        self.scanresult_liststore.clear()
        
        self.command_lock.acquire()
        self.backend.send_command('reset')
        self.update_scan_result()
        self.command_lock.release()

        self.scanprogress_progressbar.set_fraction(0.0)
        self.scanoption_frame.set_sensitive(True)
        self.is_first_scan = True
        self.value_input.grab_focus()

    def apply_scan_settings (self):
        # scan data type
        assert(self.scan_data_type_combobox.get_active() >= 0)
        datatype = self.scan_data_type_combobox.get_active_text()

        # Tell the scanresult sort function if a numeric cast is needed
        isnumeric = ('int' in datatype or 'float' in datatype or 'number' in datatype)
        self.scanresult_liststore.set_sort_func(1, misc.value_compare, (1, isnumeric))

        self.command_lock.acquire()
        self.backend.send_command('option scan_data_type %s' % (datatype,))
        # search scope
        self.backend.send_command('option region_scan_level %d' %(1 + int(self.search_scope_scale.get_value()),))
        # TODO: ugly, reset to make region_scan_level taking effect
        self.backend.send_command('reset')
        self.command_lock.release()

    
    # perform scanning through backend
    # set GUI if needed
    def do_scan(self):
        if self.pid == 0:
            self.show_error(_('Please select a process'))
            return
        assert(self.scan_data_type_combobox.get_active() >= 0)
        data_type = self.scan_data_type_combobox.get_active_text()
        cmd = self.value_input.get_text()
   
        try:
            cmd = misc.check_scan_command(data_type, cmd, self.is_first_scan)
        except Exception as e:
            # this is not quite good
            self.show_error(e.args[0])
            return

        # disable the window before perform scanning, such that if result come so fast, we won't mess it up
        self.scanoption_frame.set_sensitive(False)

        # disable set of widgets interfering with the scan
        for wid in self.disablelist:
            wid.set_sensitive(False)
        
        # Replace scan_button with stop_button
        self.scan_button.set_visible(False)
        self.stop_button.set_visible(True)

        self.is_scanning = True
        # set scan options only when first scan, since this will reset backend
        if self.is_first_scan:
            self.apply_scan_settings()
            self.is_first_scan = False
        self.progress_watcher_id = GLib.timeout_add(PROGRESS_INTERVAL,
            self.progress_watcher, priority=GLib.PRIORITY_DEFAULT_IDLE)
        threading.Thread(target=self.scan_thread_func, args=(cmd,)).start()

    def scan_thread_func(self, cmd):
        self.command_lock.acquire()
        self.backend.send_command(cmd)

        GLib.source_remove(self.progress_watcher_id)
        Gdk.threads_enter()

        self.scanprogress_progressbar.set_fraction(1.0)

        # enable set of widgets interfering with the scan
        for wid in self.disablelist:
            wid.set_sensitive(True)

        # Replace stop_button with scan_button
        self.stop_button.set_visible(False)
        self.scan_button.set_visible(True)

        self.value_input.grab_focus()

        self.is_scanning = False
        self.update_scan_result()

        Gdk.threads_leave()
        self.command_lock.release()

    def update_scan_result(self):
        match_count = self.backend.get_match_count()
        self.found_count_label.set_text(_('Found: %d') % (match_count,))
        if (match_count > SCAN_RESULT_LIST_LIMIT) or (self.backend.process_is_dead(self.pid)):
            self.scanresult_liststore.clear()
        else:
            self.command_lock.acquire()
            list_bytes = self.backend.send_command('list', get_output=True)
            self.command_lock.release()
            lines = filter(None, misc.decode(list_bytes).split('\n'))

            self.scanresult_tv.set_model(None)
            # temporarily disable model for scanresult_liststore for the sake of performance
            self.scanresult_liststore.clear()
            if misc.PY3K:
                addr = GObject.Value(GObject.TYPE_UINT64)
                off = GObject.Value(GObject.TYPE_UINT64)
            for line in lines:
                (mid, line) = line.split(']', 1)
                mid = int(mid.strip(' []'))
                (addr_str, off_str, rt, val, t) = list(map(str.strip, line.split(',')[:5]))
                t = t.strip(' []')
                if t == 'unknown':
                    continue
                # `insert_with_valuesv` has the same function of `append`, but it's 7x faster
                # PY3 has problems with int's, so we need a forced guint64 conversion
                # See: https://bugzilla.gnome.org/show_bug.cgi?id=769532
                # Still 5x faster even with the extra baggage
                if misc.PY3K:
                    addr.set_uint64(int(addr_str, 16))
                    off.set_uint64(int(off_str.split('+')[1], 16))
                else:
                    addr = long(addr_str, 16)
                    off = long(off_str.split('+')[1], 16)
                self.scanresult_liststore.insert_with_valuesv(-1, [0, 1, 2, 3, 4, 5, 6], [addr, val, t, True, off, rt, mid])
                # self.scanresult_liststore.append([addr, val, t, True, off, rt, mid])
            self.scanresult_tv.set_model(self.scanresult_liststore)

    # return range(r1, r2) where all rows between r1 and r2 (EXCLUSIVE) are visible
    # return range(0, 0) if no row visible
    def get_visible_rows(self, treeview):
        _range = treeview.get_visible_range()
        try:
            r1 = _range[0][0]
            r2 = _range[1][0] + 1
        except:
            r1 = r2 = 0
        return range(r1, r2)

    # read/write data periodically
    def data_worker(self):
        if (self.is_scanning) or (self.pid == 0) or (self.backend.process_is_dead(self.pid)):
            return not self.exit_flag
        if self.command_lock.acquire(0): # non-blocking
            Gdk.threads_enter()

            # Write to memory locked values in cheat list
            for i in self.cheatlist_liststore:
                if i[1] and i[6]: # locked and valid
                    self.write_value(i[3], i[4], i[5]) # addr, typestr, value
            # Update visible (and unlocked) cheat list rows
            rows = self.get_visible_rows(self.cheatlist_tv)
            for i in rows:
                lockflag, locked, desc, addr, typestr, value, valid = self.cheatlist_liststore[i]
                if valid and not locked:
                    newvalue = self.read_value(addr, typestr, value)
                    if newvalue is None:
                        self.cheatlist_liststore[i] = (lockflag, False, desc, addr, typestr, '??', False)
                    elif newvalue != value and not self.cheatlist_editing:
                        self.cheatlist_liststore[i] = (lockflag, locked, desc, addr, typestr, str(newvalue), valid)
            # Update visible scanresult rows
            rows = self.get_visible_rows(self.scanresult_tv)
            for i in rows:
                row = self.scanresult_liststore[i]
                addr, cur_value, scanmem_type, valid = row[:4]
                if valid:
                    new_value = self.read_value(addr, TYPENAMES_S2G[scanmem_type.split(' ', 1)[0]], cur_value)
                    if new_value is not None:
                        row[1] = str(new_value)
                    else:
                        row[1] = '??'
                        row[3] = False

            Gdk.threads_leave()
            self.command_lock.release()
        return not self.exit_flag

    def read_value(self, addr, typestr, prev_value):
        return self.bytes2value(typestr, self.read_memory(addr, self.get_type_size(typestr, prev_value)))
    
    # addr could be int or str
    def read_memory(self, addr, length):
        if not isinstance(addr,str):
            addr = '%x'%(addr,)

        self.command_lock.acquire()
        data = self.backend.send_command('dump %s %d' %(addr, length), get_output=True)
        self.command_lock.release()

        # TODO raise Exception here isn't good
        if len(data) != length:
            # self.show_error('Cannot access target memory')
            data = None
        return data
            
    # addr could be int or str
    def write_value(self, addr, typestr, value):
        if not isinstance(addr,str):
            addr = '%x'%(addr,)

        self.command_lock.acquire()
        self.backend.send_command('write %s %s %s'%(typestr, addr, value))
        self.command_lock.release()

    def exit(self, object, data=None):
        self.exit_flag = True
        self.backend.exit_cleanup()
        Gtk.main_quit()

    def check_backend_version(self):
        if self.backend.get_version() != VERSION:
            self.show_error(_('Version of scanmem mismatched, you may encounter problems. Please make sure you are using the same version of GameConqueror as scanmem.'))


if __name__ == '__main__':
    # Parse cmdline arguments
    parser = argparse.ArgumentParser(prog='GameConqueror',
                                     description=_("A GUI for scanmem, a game hacking tool"),
                                     epilog=_('Report bugs to ' + PACKAGE_BUGREPORT + '.'))
    parser.add_argument('-s', '--search', metavar='val', dest='search_value',
                        help=_('prefill the search box'))
    parser.add_argument('-v', '--version', action='version', version='%(prog)s ' + VERSION)
    parser.add_argument("pid", nargs='?', type=int, help=_("PID of the process"))
    args = parser.parse_args()

    # Init application
    GObject.threads_init()
    Gdk.threads_init()
    gc_instance = GameConqueror()

    # Attach to given pid (if any)
    if (args.pid is not None) :
        process_name = os.popen('ps -p ' + str(args.pid) + ' -o command=').read().strip()
        gc_instance.select_process(args.pid, process_name)

    # Prefill the search box (if asked)
    if (args.search_value is not None) :
        gc_instance.input_box.set_text(args.search_value)

    # Start
    Gtk.main()
