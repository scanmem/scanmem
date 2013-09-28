#!/usr/bin/env python
"""
    Game Conqueror: a graphical game cheating tool, using scanmem as its backend
    
    Copyright (C) 2009,2010,2011,2013 Wang Lu <coolwanglu(a)gmail.com>
    Copyright (C) 2010 Bryan Cain
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


import sys
import os
import re
import struct
import tempfile
import platform
import threading
import time
import json

import gi
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GObject

from consts import *
from hexview import HexView
from backend import GameConquerorBackend
import misc

import locale
# In some locale, ',' is used in float numbers
locale.setlocale(locale.LC_NUMERIC, 'C')

CLIPBOARD = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
WORK_DIR = os.path.dirname(sys.argv[0])
DATA_WORKER_INTERVAL = 500 # for read(update)/write(lock)
SCAN_RESULT_LIST_LIMIT = 1000 # maximal number of entries that can be displayed

SCAN_VALUE_TYPES = misc.build_simple_str_liststore(['int', 'int8', 'int16', 'int32', 'int64', 'float', 'float32', 'float64', 'number', 'bytearray', 'string' ])

LOCK_FLAG_TYPES = misc.build_simple_str_liststore(['=', '+', '-'])

LOCK_VALUE_TYPES = misc.build_simple_str_liststore(['int8', 'int16', 'int32', 'int64', 'float32', 'float64', 'bytearray', 'string' ])

SEARCH_SCOPE_NAMES = ['Basic', 'Normal', 'Full']

# convert type names used by scanmem into ours
TYPENAMES_S2G = {'I64':'int64'
                ,'I64s':'int64'
                ,'I64u':'int64'
                ,'I32':'int32'
                ,'I32s':'int32'
                ,'I32u':'int32'
                ,'I16':'int16'
                ,'I16s':'int16'
                ,'I16u':'int16'
                ,'I8':'int8'
                ,'I8s':'int8'
                ,'I8u':'int8'
                ,'F32':'float32'
                ,'F64':'float64'
                ,'bytearray':'bytearray'
                ,'string':'string'
                }   

# convert our typenames into struct format characters
TYPENAMES_G2STRUCT = {'int8':'b'
                     ,'int16':'h'
                     ,'int32':'i'
                     ,'int64':'q'
                     ,'float32':'f'
                     ,'float64':'d'
                     }
        
# sizes in bytes of integer and float types
TYPESIZES = {'int8':1
            ,'int16':2
            ,'int32':4
            ,'int64':8
            ,'float32':4
            ,'float64':8
            }

class GameConqueror():
    def __init__(self):
        Gtk.Settings.get_default().set_long_property('gtk-tooltip-timeout', 0, '')
        ##################################
        # init GUI
        self.builder = Gtk.Builder()
        self.builder.add_from_file(os.path.join(WORK_DIR, 'GameConqueror.xml'))

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

        self.scan_button = self.builder.get_object('Scan_Button')
        self.reset_button = self.builder.get_object('Reset_Button')
        self.is_first_scan = True

        ###
        # Set scan data type
        self.scan_data_type_combobox = self.builder.get_object('ScanDataType_ComboBox')
        misc.build_combobox(self.scan_data_type_combobox, SCAN_VALUE_TYPES)
        # apply setting
        misc.combobox_set_active_item(self.scan_data_type_combobox, SETTINGS['scan_data_type'])

        ###
        # set search scope
        self.search_scope_scale = self.builder.get_object('SearchScope_Scale')
        self.search_scope_scale_adjustment = Gtk.Adjustment(lower=0, upper=2, step_incr=1, page_incr=1, page_size=0)
        self.search_scope_scale.set_adjustment(self.search_scope_scale_adjustment)
        # apply setting
        self.search_scope_scale.set_value(SETTINGS['search_scope'])



        # init scanresult treeview
        # we may need a cell data func here
        # create model
        self.scanresult_tv = self.builder.get_object('ScanResult_TreeView')
        self.scanresult_liststore = Gtk.ListStore(str, str, str, bool) #addr, value, type, valid
        self.scanresult_tv.set_model(self.scanresult_liststore)
        # init columns
        misc.treeview_append_column(self.scanresult_tv, 'Address', attributes=(('text',0),), properties = (('family', 'monospace'),))
        misc.treeview_append_column(self.scanresult_tv, 'Value', attributes=(('text',1),), properties = (('family', 'monospace'),))

        # init CheatList TreeView
        self.cheatlist_tv = self.builder.get_object('CheatList_TreeView')
        self.cheatlist_liststore = Gtk.ListStore(str, bool, str, str, str, str, bool) #lockflag, locked, description, addr, type, value, valid
        self.cheatlist_tv.set_model(self.cheatlist_liststore)
        self.cheatlist_tv.set_reorderable(True)
        self.cheatlist_editing = False
        self.cheatlist_tv.connect('key-press-event', self.cheatlist_keypressed)
        # Lock Flag
        misc.treeview_append_column(self.cheatlist_tv, '' 
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
        misc.treeview_append_column(self.cheatlist_tv, 'Lock'
                                        ,renderer_class = Gtk.CellRendererToggle
                                        ,attributes = (('active',1),)
                                        ,properties = (('activatable', True)
                                                      ,('radio', False)
                                                      ,('inconsistent', False)) 
                                        ,signals = (('toggled', self.cheatlist_toggle_lock_cb),)
                                   )
        # Description
        misc.treeview_append_column(self.cheatlist_tv, 'Description'
                                        ,attributes = (('text',2),)
                                        ,properties = (('editable', True),)
                                        ,signals = (('edited', self.cheatlist_edit_description_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )
        # Address
        misc.treeview_append_column(self.cheatlist_tv, 'Address'
                                        ,attributes = (('text',3),)
                                        ,properties = (('family', 'monospace'),)
                                   )
        # Type
        misc.treeview_append_column(self.cheatlist_tv, 'Type'
                                        ,renderer_class = Gtk.CellRendererCombo
                                        ,attributes = (('text',4),)
                                        ,properties = (('editable', True)
                                                      ,('has-entry', False)
                                                      ,('model', LOCK_VALUE_TYPES)
                                                      ,('text-column', 0))
                                        ,signals = (('edited', self.cheatlist_edit_type_cb),
                                                    ('editing-started', self.cheatlist_edit_start),
                                                    ('editing-canceled', self.cheatlist_edit_cancel),)
                                   )
        # Value 
        misc.treeview_append_column(self.cheatlist_tv, 'Value'
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
        self.processlist_tv.get_selection().set_mode(Gtk.SelectionMode.SINGLE)
        self.processlist_liststore = Gtk.ListStore(str, str, str)
        self.processlist_filter = self.processlist_liststore.filter_new(root=None)
        self.processlist_filter.set_visible_func(self.processlist_filter_func, data=None)
        self.processlist_tv.set_model(self.processlist_filter)
        self.processlist_tv.set_enable_search(True)
        self.processlist_tv.set_search_column(1)
        # first col
        misc.treeview_append_column(self.processlist_tv, 'PID'
                                        ,attributes = (('text',0),)
                                   )
        # second col
        misc.treeview_append_column(self.processlist_tv, 'User'
                                        ,attributes = (('text',1),)
                                   )

        # third col
        misc.treeview_append_column(self.processlist_tv, 'Process'
                                        ,attributes = (('text',2),)
                                   )
        # init AddCheatDialog
        self.addcheat_address_input = self.builder.get_object('Address_Input')
        self.addcheat_description_input = self.builder.get_object('Description_Input')
        self.addcheat_type_combobox = self.builder.get_object('Type_ComboBox')
        misc.build_combobox(self.addcheat_type_combobox, LOCK_VALUE_TYPES)
        misc.combobox_set_active_item(self.addcheat_type_combobox, SETTINGS['lock_data_type'])
        
        
        
        # init popup menu for scanresult
        self.scanresult_popup = Gtk.Menu()
        misc.menu_append_item(self.scanresult_popup, 'Add to cheat list', self.scanresult_popup_cb, 'add_to_cheat_list')
        misc.menu_append_item(self.scanresult_popup, 'Browse this address', self.scanresult_popup_cb, 'browse_this_address')
        misc.menu_append_item(self.scanresult_popup, 'Scan for this address', self.scanresult_popup_cb, 'scan_for_this_address')
        self.scanresult_popup.show_all()

        # init popup menu for cheatlist
        self.cheatlist_popup = Gtk.Menu()
        misc.menu_append_item(self.cheatlist_popup, 'Browse this address', self.cheatlist_popup_cb, 'browse_this_address')
        misc.menu_append_item(self.cheatlist_popup, 'Copy address', self.cheatlist_popup_cb, 'copy_address')
        misc.menu_append_item(self.cheatlist_popup, 'Remove this entry', self.cheatlist_popup_cb, 'remove_entry')
        self.cheatlist_popup.show_all()

        self.builder.connect_signals(self)
        self.main_window.connect('destroy', self.exit)

        ###########################
        # init others (backend, flag...)
        self.pid = 0 # target pid
        self.maps = []
        self.last_hexedit_address = (0,0) # used for hexview
        self.is_scanning = False
        self.exit_flag = False # currently for data_worker only, other 'threads' may also use this flag
        self.is_data_worker_working = False

        self.backend = GameConquerorBackend()
        self.check_backend_version()
        self.search_count = 0
        GObject.timeout_add(DATA_WORKER_INTERVAL, self.data_worker)
        self.command_lock = threading.RLock()


    ###########################
    # GUI callbacks

    def MemoryEditor_Window_delete_event_cb(self, widget, event, data=None):
        self.memoryeditor_window.hide()
        return True

    def MemoryEditor_Button_clicked_cb(self, button, data=None):
        if self.pid == 0:
            self.show_error('Please select a process')
            return
        self.browse_memory()
        return True

    def MemoryEditor_Address_Entry_activate_cb(self, entry, data=None):
        txt = self.memoryeditor_address_entry.get_text()
        if txt == '':
            return
        try:
            addr = int(txt, 16)
            self.browse_memory(addr)
        except:
            self.show_error('Invalid address')

    def MemoryEditor_JumpTo_Button_clicked_cb(self, button, data=None):
        txt = self.memoryeditor_address_entry.get_text()
        if txt == '':
            return
        try:
            addr = int(txt, 16)
            self.browse_memory(addr)
        except:
            self.show_error('Invalid address')

    def ManuallyAddCheat_Button_clicked_cb(self, button, data=None):
        self.addcheat_dialog.show()
        return True

    def RemoveAllCheat_Button_clicked_cb(self, button, data=None):
        self.cheatlist_liststore.clear()
        return True

    def LoadCheat_Button_clicked_cb(self, button, data=None):
        dialog = Gtk.FileChooserDialog("Open..",
                self.main_window,
                Gtk.FileChooserAction.OPEN,
                (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                    Gtk.STOCK_OPEN, Gtk.ResponseType.OK))
        dialog.set_default_response(Gtk.ResponseType.OK)

        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            try:
                with open(dialog.get_filename(), 'rb') as f:
                    obj = json.load(f)
                    for row in obj['cheat_list']:
                        self.add_to_cheat_list(row[3],row[5],row[4],row[2])
            except:
                pass
        dialog.destroy()
        return True

    def SaveCheat_Button_clicked_cb(self, button, data=None):
        dialog = Gtk.FileChooserDialog("Save..",
                self.main_window,
                Gtk.FileChooserAction.SAVE,
                (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                    Gtk.STOCK_OPEN, Gtk.ResponseType.OK))
        dialog.set_default_response(Gtk.ResponseType.OK)

        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            try:
                with open(dialog.get_filename(), 'wb') as f:
                    obj = {'cheat_list' : [list(i) for i in self.cheatlist_liststore]}
                    json.dump(obj, f);
            except:
                pass
        dialog.destroy()
        return True

    def SearchScope_Scale_format_value_cb(self, scale, value, Data=None):
        return SEARCH_SCOPE_NAMES[int(value)]

    def Value_Input_activate_cb(self, entry, data=None):
        self.do_scan()
        return True

    def ScanResult_TreeView_button_release_event_cb(self, widget, event, data=None):
        if event.button == 3: # right click
            (model, iter) = self.scanresult_tv.get_selection().get_selected()
            if iter is not None:
                self.scanresult_popup.popup(None, None, None, None, event.button, event.get_time())
                return True
            return False
        return False

    def ScanResult_TreeView_popup_menu_cb(self, widget, data=None):
        (model, iter) = self.scanresult_tv.get_selection().get_selected()
        if iter is not None:
            self.scanresult_popup.popup(None, None, None, 0, 0)
            return True
        return False

    def ScanResult_TreeView_row_activated_cb(self, treeview, path, view_column, data=None):
        # add to cheat list
        (model, iter) = self.scanresult_tv.get_selection().get_selected()
        if iter is not None:
            (addr, value, typestr) = model.get(iter, 0, 1, 2)
            self.add_to_cheat_list(addr, value, typestr)
            return True
        return False

    def CheatList_TreeView_button_release_event_cb(self, widget, event, data=None):
        if event.button == 3: # right click
            (model, iter) = self.cheatlist_tv.get_selection().get_selected()
            if iter is not None:
                self.cheatlist_popup.popup(None, None, None, None, event.button, event.get_time())
                return True
            return False
        return False

    def CheatList_TreeView_popup_menu_cb(self, widget, data=None):
        (model, iter) = self.cheatlist_tv.get_selection().get_selected()
        if iter is not None:
            self.cheatlist_popup.popup(None, None, None, 0, 0)
            return True
        return False

    def ProcessFilter_Input_changed_cb(self, widget, data=None):
        self.processlist_filter.refilter()

    def UserFilter_Input_changed_cb(self, widget, data=None):
        self.processlist_filter.refilter()

    def ProcessList_TreeView_row_activated_cb(self, treeview, path, view_column, data=None):
        (model, iter) = self.processlist_tv.get_selection().get_selected()
        if iter is not None:
            (pid, user, process) = model.get(iter, 0, 1, 2)
            self.select_process(int(pid), process)
            self.process_list_dialog.response(Gtk.ResponseType.CANCEL)
            return True
        return False

    def SelectProcess_Button_clicked_cb(self, button, data=None):
        self.processlist_liststore.clear()
        pl = self.get_process_list()
        for p in pl:
           # self.processlist_liststore.append([p[0], (p[1][:50] if len(p) > 1 else '<unknown>')]) # limit the length here, otherwise it may crash (why?)
            self.processlist_liststore.append([p[0], (p[1] if len(p) > 1 else '<unknown>'),(p[2] if len(p) > 2 else '<unknown>')])
        self.process_list_dialog.show()
        while True:
            res = self.process_list_dialog.run()
            if res == Gtk.ResponseType.OK: # -5
                (model, iter) = self.processlist_tv.get_selection().get_selected()
                if iter is None:
                    self.show_error('Please select a process')
                    continue
                else:
                    (pid, process) = model.get(iter, 0, 1)
                    self.select_process(int(pid), process)
                    break
            else: # for None and Cancel
                break
        self.process_list_dialog.hide()
        return True

    def ConfirmAddCheat_Button_clicked_cb(self, button, data=None):
        try:
            addr = self.addcheat_address_input.get_text()
        except ValueError:
            self.show_error('Please enter a valid address.')
            return False

        description = self.addcheat_description_input.get_text()
        if not description: description = 'No Description'
        typestr = LOCK_VALUE_TYPES[self.addcheat_type_combobox.get_active()][0]
        if 'int' in typestr: value = 0
        elif 'float' in typestr: value = 0.0
        elif 'string' in typestr: value = ''
        else: value = None
        
        self.add_to_cheat_list(addr, value, typestr, description)
        self.addcheat_dialog.hide()
        return True

    def CloseAddCheat_Button_clicked_cb(self, button, data=None):
        self.addcheat_dialog.hide()
        return True

    def Scan_Button_clicked_cb(self, button, data=None):
        self.do_scan()
        return True

    def Reset_Button_clicked_cb(self, button, data=None):
        self.reset_scan()
        return True

    def Logo_EventBox_button_release_event_cb(self, widget, data=None):
        self.about_dialog.run()
        self.about_dialog.hide()
        return True

    #######################
    # customed callbacks
    # (i.e. not standard event names are used)
    
    def memoryeditor_hexview_char_changed_cb(self, hexview, offset, charval):
        addr = hexview.base_addr + offset
        self.write_value(addr, 'int8', charval)
        # return False such that the byte the default handler will be called, and will be displayed correctly 
        return False

    
    def cheatlist_edit_start(self, a, b, c):
        self.cheatlist_editing = True
    def cheatlist_edit_cancel(self, a):
        self.cheatlist_editing = False

    def scanresult_popup_cb(self, menuitem, data=None):
        (model, iter) = self.scanresult_tv.get_selection().get_selected()
        (addr, value, typestr) = model.get(iter, 0, 1, 2)
        if iter is None:
            return False
        if data == 'add_to_cheat_list':
            self.add_to_cheat_list(addr, value, typestr)
            return True
        elif data == 'browse_this_address':
            self.browse_memory(int(addr,16))
            return True
        elif data == 'scan_for_this_address':
            self.scan_for_addr(int(addr,16))
            return True
        return False
        
    def cheatlist_keypressed(self, cheatlist_tv, event, selection=None):
        keycode = event.keyval
        pressedkey = Gdk.keyval_name(keycode)
        if pressedkey == 'Delete':
            (model, iter) = self.cheatlist_tv.get_selection().get_selected()
            if iter is None: return
            self.cheatlist_liststore.remove(iter) 

    def cheatlist_popup_cb(self, menuitem, data=None):
        self.cheatlist_editing = False
        (model, iter) = self.cheatlist_tv.get_selection().get_selected()
        addr = model.get(iter, 3)[0]
        if iter is None:
            return False
        if data == 'remove_entry':
            self.cheatlist_liststore.remove(iter) 
            return True
        elif data == 'browse_this_address':
            self.browse_memory(int(addr,16))
            return True
        elif data == 'copy_address':
            CLIPBOARD.set_text(addr, len(addr))
            return True
        return False

    def cheatlist_toggle_lock_cb(self, cellrenderertoggle, path, data=None):
        row = int(path)
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
        row = int(path)
        self.cheatlist_liststore[row][2] = new_text
        return True

    def cheatlist_edit_value_cb(self, cell, path, new_text, data=None):
        self.cheatlist_editing = False
        # ignore empty value
        if new_text == '':
            return True
        row = int(path)
        if not self.cheatlist_liststore[row][6]: #not valid
            return True
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
        row = int(path)
        self.cheatlist_liststore[row][4] = new_text
        if self.cheatlist_liststore[row][1]: # locked
            # false unlock it
            self.cheatlist_liststore[row][1] = False
            pass
        return True

    def processlist_filter_func(self, model, iter, data=None):
        (pid, user, process) = model.get(iter, 0, 1, 2)
        return process is not None and \
                process.find(self.processfilter_input.get_text()) != -1 and \
                user is not None and \
                user.find(self.userfilter_input.get_text()) != -1
        


    ############################
    # core functions
    def show_error(self, msg):
        dialog = Gtk.MessageDialog(None
                                 ,Gtk.DialogFlags.MODAL
                                 ,Gtk.MessageType.ERROR
                                 ,Gtk.ButtonsType.OK
                                 ,msg)
        dialog.run()
        dialog.destroy()

    # return None if unknown
    def get_pointer_width(self):
        bits = platform.architecture()[0]
        if not bits.endswith('bit'):
            return None
        try:
            bitn = int(bits[:-len('bit')])
            if bitn not in [8,16,32,64]:
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
            return len(eval('\''+value+'\''))
        return None

    # parse bytes dumped by scanmem into number, string, etc.
    def bytes2value(self, typename, bytes):
        if bytes is None:
            return None
        if typename in TYPENAMES_G2STRUCT:
            return struct.unpack(TYPENAMES_G2STRUCT[typename], bytes)[0]
        elif typename == 'string':
            return repr('%s'%(bytes,))[1:-1]
        elif typename == 'bytearray':
            return ' '.join(['%02x'%ord(i) for i in bytes])
        else:
            return bytes
    
    def scan_for_addr(self, addr):
        bits = self.get_pointer_width()
        if bits is None:
            show_error('Unknown architecture, you may report to developers')
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
            show_error('Cannot retieve memory maps of that process, maybe it has exited (crashed), or you don\'t have enough privilege')
        selected_region = None
        if addr:
            for m in self.maps:
                if m['start_addr'] <= addr and addr < m['end_addr']:
                    selected_region = m
                    break
            if selected_region:
                if selected_region['flags'][0] != 'r': # not readable
                    show_error('Address %x is not readable' % (addr,))
                    return
            else:
                show_error('Address %x is not valid' % (addr,))
                return
        else:
            # just select the first readable region
            for m in self.maps:
                if m['flags'][0] == 'r':
                    selected_region = m
                    break
            if selected_region is None:
                self.show_error('Cannot find a readable region')
                return
            addr = selected_region['start_addr']

        # read region if necessary
        start_addr = max(addr - 1024, selected_region['start_addr'])
        end_addr = min(addr + 1024, selected_region['end_addr'])
        if self.last_hexedit_address[0] != start_addr or \
           self.last_hexedit_address[1] != end_addr:
            data = self.read_memory(start_addr, end_addr - start_addr)
            if data is None:
                self.show_error('Cannot read memory')
                return
            self.last_hexedit_address = (start_addr, end_addr)
            self.memoryeditor_hexview.payload=data
            self.memoryeditor_hexview.base_addr = start_addr
        
        # set editable flag
        self.memoryeditor_hexview.editable = (selected_region['flags'][1] == 'w')

        if addr:
            self.memoryeditor_hexview.show_addr(addr)
        self.memoryeditor_window.show()

    # this callback will be called from other thread
    def progress_watcher(self):
        Gdk.threads_enter()
        self.scanprogress_progressbar.set_fraction(self.backend.get_scan_progress())
        Gdk.flush()
        Gdk.threads_leave()
        return True

    def add_to_cheat_list(self, addr, value, typestr, description='No Description'):
        # determine longest possible type
        types = typestr.split()
        vt = typestr
        for t in types:
            if t in TYPENAMES_S2G:
                vt = TYPENAMES_S2G[t]
                break
        self.cheatlist_liststore.prepend(['=', False, description, addr, vt, str(value), True])

    def get_process_list(self):
        return [list(map(str.strip, e.strip().split(' ',2))) for e in os.popen('ps -wweo pid=,user=,command= --sort=-pid').readlines()]

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
            self.process_label.set_text('No process selected')
            self.process_label.set_property('tooltip-text', 'Select a process')
            self.show_error('Cannot retieve memory maps of that process, maybe it has exited (crashed), or you don\'t have enough privilege')
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
        self.search_count = 0
        # reset search type and value type
        self.scanresult_liststore.clear()
        
        self.command_lock.acquire()
        self.backend.send_command('reset')
        self.update_scan_result()
        self.command_lock.release()

        self.scanoption_frame.set_sensitive(True)
        self.is_first_scan = True

    def apply_scan_settings (self):
        # scan data type
        active = self.scan_data_type_combobox.get_active()
        assert(active >= 0)
        dt = self.scan_data_type_combobox.get_model()[active][0]

        self.command_lock.acquire()
        self.backend.send_command('option scan_data_type %s' % (dt,))
        # search scope
        self.backend.send_command('option region_scan_level %d' %(1 + int(self.search_scope_scale.get_value()),))
        # TODO: ugly, reset to make region_scan_level taking effect
        self.backend.send_command('reset')
        self.command_lock.release()

    
    # perform scanning through backend
    # set GUI if needed
    def do_scan(self):
        if self.pid == 0:
            self.show_error('Please select a process')
            return
        active = self.scan_data_type_combobox.get_active()
        assert(active >= 0)
        data_type = self.scan_data_type_combobox.get_model()[active][0]
        cmd = self.value_input.get_text()
   
        try:
            cmd = misc.check_scan_command(data_type, cmd, self.is_first_scan)
        except Exception as e:
            # this is not quite good
            self.show_error(e.args[0])
            return

        # disable the window before perform scanning, such that if result come so fast, we won't mess it up
        self.search_count +=1 
        self.scanoption_frame.set_sensitive(False) # no need to check search_count here
        self.main_window.set_sensitive(False)
        self.memoryeditor_window.set_sensitive(False)
        self.is_scanning = True
        # set scan options only when first scan, since this will reset backend
        if self.search_count == 1:
            self.apply_scan_settings()
        self.backend.reset_scan_progress()
        self.progress_watcher_id = GObject.idle_add(self.progress_watcher)
        threading.Thread(target=self.scan_thread_func, args=(cmd,)).start()

    def scan_thread_func(self, cmd):
        self.command_lock.acquire()
        self.backend.send_command(cmd)

        GObject.source_remove(self.progress_watcher_id)
        Gdk.threads_enter()

        self.main_window.set_sensitive(True)
        self.memoryeditor_window.set_sensitive(True)
        self.is_scanning = False
        self.update_scan_result()
        self.is_first_scan = False

        Gdk.flush()
        Gdk.threads_leave()
        self.command_lock.release()

    def update_scan_result(self):
        match_count = self.backend.get_match_count()
        self.found_count_label.set_text('Found: %d' % (match_count,))
        if (match_count > SCAN_RESULT_LIST_LIMIT):
            self.scanresult_liststore.clear()
        else:
            self.command_lock.acquire()
            lines = self.backend.send_command('list', get_output = True)
            self.command_lock.release()

            self.scanresult_tv.set_model(None)
            # temporarily disable model for scanresult_liststore for the sake of performance
            self.scanresult_liststore.clear()
            for line in lines:
                line = line[line.find(']')+1:]
                (a, v, t) = list(map(str.strip, line.split(',')[:3]))
                a = '%x'%(int(a,16),)
                t = t[1:-1]
                self.scanresult_liststore.append([a, v, t, True])
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
        if (not self.is_scanning) and (self.pid != 0) and self.command_lock.acquire(0): # non-blocking
            Gdk.threads_enter()

            self.is_data_worker_working = True
            rows = self.get_visible_rows(self.scanresult_tv)
            for i in rows:
                row = self.scanresult_liststore[i]
                addr, cur_value, scanmem_type, valid = row
                if valid:
                    new_value = self.read_value(addr, TYPENAMES_S2G[scanmem_type.strip()], cur_value)
                    if new_value is not None:
                        row[1] = str(new_value)
                    else:
                        row[1] = '??'
                        row[3] = False
            # write locked values in cheat list and read unlocked values
            for i in self.cheatlist_liststore:
                if i[1] and i[6]: # locked and valid
                    self.write_value(i[3], i[4], i[5]) # addr, typestr, value
            rows = self.get_visible_rows(self.cheatlist_tv)
            for i in rows:
                lockflag, locked, desc, addr, typestr, value, valid = self.cheatlist_liststore[i]
                if valid and not locked:
                    newvalue = self.read_value(addr, typestr, value)
                    if newvalue is None:
                        self.cheatlist_liststore[i] = (lockflag, False, desc, addr, typestr, '??', False)
                    elif newvalue != value and not self.cheatlist_editing:
                        self.cheatlist_liststore[i] = (lockflag, locked, desc, addr, typestr, str(newvalue), valid)
            self.is_data_worker_working = False

            Gdk.flush()
            Gdk.threads_leave()
            self.command_lock.release()
        return not self.exit_flag

    def read_value(self, addr, typestr, prev_value):
        return self.bytes2value(typestr, self.read_memory(addr, self.get_type_size(typestr, prev_value)))
    
    # addr could be int or str
    def read_memory(self, addr, length):
        if not isinstance(addr,str):
            addr = '%x'%(addr,)
        f = tempfile.NamedTemporaryFile()

        self.command_lock.acquire()
        self.backend.send_command('dump %s %d %s' % (addr, length, f.name))
        self.command_lock.release()

        data = f.read()

#        lines = self.backend.send_command('dump %s %d' % (addr, length))
#        data = ''
#        for line in lines:
#            bytes = line.strip().split()
#            for byte in bytes: data += chr(int(byte, 16))
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
        Gtk.main_quit()

    def main(self):
        Gtk.main()

    def check_backend_version(self):
        if self.backend.get_version() != VERSION:
            self.show_error('Version of scanmem mismatched, you may encounter problems. Please make sure you are using the same version of Gamconqueror as scanmem.')

    

if __name__ == '__main__':
    GObject.threads_init()
    Gdk.threads_init()
    GameConqueror().main()
