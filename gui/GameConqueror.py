#!/usr/bin/env python
"""
    Game Conqueror: a graphical game cheating tool, using scanmem as its backend
    
    Copyright (C) 2009,2010 Wang Lu <coolwanglu(a)gmail.com>

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

import pygtk
import gtk
import gobject

from consts import *
from hexview import HexView
from backend import GameConquerorBackend
import misc

WORK_DIR = os.path.dirname(sys.argv[0])
DATA_WORKER_INTERVAL = 500 # for read(update)/write(lock)
SCAN_RESULT_LIST_LIMIT = 1000 # maximal number of entries that can be displayed

SCAN_VALUE_TYPES = misc.build_simple_str_liststore(['int'
                                              ,'int8'
                                              ,'int16'
                                              ,'int32'
                                              ,'int64'
                                              ,'float'
                                              ,'float32'
                                              ,'float64'
                                              ,'number'
                                              ,'bytearray'
                                              ,'string'
                                              ])

LOCK_FLAG_TYPES = misc.build_simple_str_liststore(['=', '+', '-'])

LOCK_VALUE_TYPES = misc.build_simple_str_liststore(['int8' 
                                            ,'int16'
                                            ,'int32'
                                            ,'int64' 
                                            ,'float32'
                                            ,'float64'
                                            ,'bytearray'
                                            ,'string'
                                            ])

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

        

class GameConqueror():
    def __init__(self):
        ##################################
        # init GUI
        self.builder = gtk.Builder()
        self.builder.add_from_file(os.path.join(WORK_DIR, 'GameConqueror.xml'))

        self.main_window = self.builder.get_object('MainWindow')
        self.main_window.set_title('GameConqueror %s' % (VERSION,))
        self.about_dialog = self.builder.get_object('AboutDialog')
        # set version
        self.about_dialog.set_version(VERSION)

        self.process_list_dialog = self.builder.get_object('ProcessListDialog')

        # init memory editor
        self.memoryeditor_window = self.builder.get_object('MemoryEditor_Window')
        self.memoryeditor_hexview = HexView()
        self.memoryeditor_window.child.pack_start(self.memoryeditor_hexview)
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

        ###
        # Set scan data type
        self.scan_data_type_combobox = self.builder.get_object('ScanDataType_ComboBox')
        misc.build_combobox(self.scan_data_type_combobox, SCAN_VALUE_TYPES)
        # apply setting
        misc.combobox_set_active_item(self.scan_data_type_combobox, SETTINGS['scan_data_type'])

        ###
        # set search scope
        self.search_scope_scale = self.builder.get_object('SearchScope_Scale')
        self.search_scope_scale_adjustment = gtk.Adjustment(lower=0, upper=2, step_incr=1, page_incr=1, page_size=0)
        self.search_scope_scale.set_adjustment(self.search_scope_scale_adjustment)
        # apply setting
        self.search_scope_scale.set_value(SETTINGS['search_scope'])



        # init scanresult treeview
        # we may need a cell data func here
        # create model
        self.scanresult_tv = self.builder.get_object('ScanResult_TreeView')
        self.scanresult_liststore = gtk.ListStore(str, str, str) 
        self.scanresult_tv.set_model(self.scanresult_liststore)
        # init columns
        misc.treeview_append_column(self.scanresult_tv, 'Address', attributes=(('text',0),) )
        misc.treeview_append_column(self.scanresult_tv, 'Value', attributes=(('text',1),) )

        # init CheatList TreeView
        self.cheatlist_tv = self.builder.get_object('CheatList_TreeView')
        self.cheatlist_liststore = gtk.ListStore(str, bool, str, str, str, str)
        self.cheatlist_tv.set_model(self.cheatlist_liststore)
        # Lock Flag
        misc.treeview_append_column(self.cheatlist_tv, '' 
                                        ,renderer_class = gtk.CellRendererCombo
                                        ,attributes = (('text',0),)
                                        ,properties = (('editable', True)
                                                      ,('has-entry', False)
                                                      ,('model', LOCK_FLAG_TYPES)
                                                      ,('text-column', 0)
                                        )
                                        ,signals = (('edited', self.cheatlist_toggle_lock_flag_cb),)
                                   )
        # Lock
        misc.treeview_append_column(self.cheatlist_tv, 'Lock'
                                        ,renderer_class = gtk.CellRendererToggle
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
                                        ,signals = (('edited', self.cheatlist_edit_description_cb),)
                                   )
        # Address
        misc.treeview_append_column(self.cheatlist_tv, 'Address'
                                        ,attributes = (('text',3),)
                                   )
        # Type
        misc.treeview_append_column(self.cheatlist_tv, 'Type'
                                        ,renderer_class = gtk.CellRendererCombo
                                        ,attributes = (('text',4),)
                                        ,properties = (('editable', True)
                                                      ,('has-entry', False)
                                                      ,('model', LOCK_VALUE_TYPES)
                                                      ,('text-column', 0))
                                        ,signals = (('edited', self.cheatlist_edit_type_cb),)
                                   )
        # Value 
        misc.treeview_append_column(self.cheatlist_tv, 'Value'
                                        ,attributes = (('text',5),)
                                        ,properties = (('editable', True),)
                                        ,signals = (('edited', self.cheatlist_edit_value_cb),)
                                   )

        # init ProcessList_TreeView
        self.processlist_tv = self.builder.get_object('ProcessList_TreeView')
        self.processlist_tv.get_selection().set_mode(gtk.SELECTION_SINGLE)
        self.processlist_liststore = gtk.ListStore(str, str)
        self.processlist_tv.set_model(self.processlist_liststore)
        self.processlist_tv.set_enable_search(True)
        self.processlist_tv.set_search_column(1)
        # first col
        misc.treeview_append_column(self.processlist_tv, 'PID'
                                        ,attributes = (('text',0),)
                                   )
        # second col
        misc.treeview_append_column(self.processlist_tv, 'Process'
                                        ,attributes = (('text',1),)
                                   )

        # init popup menu for scanresult
        self.scanresult_popup = gtk.Menu()
        misc.menu_append_item(self.scanresult_popup
                             ,'Add to cheat list'
                             ,self.scanresult_popup_cb
                             ,'add_to_cheat_list')
        misc.menu_append_item(self.scanresult_popup
                             ,'Browse this address'
                             ,self.scanresult_popup_cb
                             ,'browse_this_address')
        self.scanresult_popup.show_all()

        # init popup menu for cheatlist
        self.cheatlist_popup = gtk.Menu()
        misc.menu_append_item(self.cheatlist_popup
                             ,'Browse this address'
                             ,self.cheatlist_popup_cb
                             ,'browse_this_address')
        misc.menu_append_item(self.cheatlist_popup
                             ,'Remove this entry'
                             ,self.cheatlist_popup_cb
                             ,'remove_entry')
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

        self.backend = GameConquerorBackend()
        self.backend.add_error_listener(self.backend_error_cb)
        self.backend.add_progress_listener(self.backend_progress_cb)
        self.search_count = 0
        self.data_worker_id = gobject.timeout_add(DATA_WORKER_INTERVAL, self.data_worker)

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

    def RemoveAllCheat_Button_clicked_cb(self, button, data=None):
        self.cheatlist_liststore.clear()
        return True

    def ManuallyAddCheat_Button_clicked_cb(self, button, data=None):
        # TODO
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
                self.scanresult_popup.popup(None, None, None, event.button, event.get_time())
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
                self.cheatlist_popup.popup(None, None, None, event.button, event.get_time())
                return True
            return False
        return False

    def CheatList_TreeView_popup_menu_cb(self, widget, data=None):
        (model, iter) = self.cheatlist_tv.get_selection().get_selected()
        if iter is not None:
            self.cheatlist_popup.popup(None, None, None, 0, 0)
            return True
        return False

    def ProcessList_TreeView_row_activated_cb(self, treeview, path, view_column, data=None):
        (model, iter) = self.processlist_tv.get_selection().get_selected()
        if iter is not None:
            (pid, process) = model.get(iter, 0, 1)
            self.select_process(int(pid), process)
            self.process_list_dialog.response(gtk.RESPONSE_CANCEL)
            return True
        return False

    def SelectProcess_Button_clicked_cb(self, button, data=None):
        self.processlist_liststore.clear()
        pl = self.get_process_list()
        for p in pl:
            self.processlist_liststore.append([p[0], p[1][:50]]) # limit the length here, otherwise it may crash (why?)
        self.process_list_dialog.show()
        while True:
            res = self.process_list_dialog.run()
            if res == gtk.RESPONSE_OK: # -5
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

    #######################3
    # customed callbacks
    # (i.e. not standard event names are used)
    
    def memoryeditor_hexview_char_changed_cb(self, hexview, offset, charval):
        addr = hexview.base_addr + offset
        self.write_value(addr, 'int8', charval)
        # return False such that the byte the default handler will be called, and will be displayed correctly 
        return False

    def scanresult_popup_cb(self, menuitem, data=None):
        if data == 'add_to_cheat_list':
            (model, iter) = self.scanresult_tv.get_selection().get_selected()
            if iter is not None:
                (addr, value, typestr) = model.get(iter, 0, 1, 2)
                self.add_to_cheat_list(addr, value, typestr)
                return True
            return False
        elif data == 'browse_this_address':
            (model, iter) = self.scanresult_tv.get_selection().get_selected()
            if iter is not None:
                addr = model.get(iter, 0)[0]
                self.browse_memory(int(addr,16))
                return True
            return False
        return False

    def cheatlist_popup_cb(self, menuitem, data=None):
        if data == 'remove_entry':
            (model, iter) = self.cheatlist_tv.get_selection().get_selected()
            if iter is not None:
                self.cheatlist_liststore.remove(iter) 
                return True
            return False
        elif data == 'browse_this_address':
            (model, iter) = self.cheatlist_tv.get_selection().get_selected()
            if iter is not None:
                addr = model.get(iter, 3)[0]
                self.browse_memory(int(addr,16))
                return True
            return False
        return False

    def cheatlist_toggle_lock_cb(self, cellrenderertoggle, path, data=None):
        row = int(path)
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
        # currently only one lock flag is supported
        return True
        row = int(path)
        self.cheatlist_liststore[row][0] = new_text
        # data_worker will handle this later
        return True

    def cheatlist_edit_description_cb(self, cell, path, new_text, data=None):
        row = int(path)
        self.cheatlist_liststore[row][2] = new_text
        return True

    def cheatlist_edit_value_cb(self, cell, path, new_text, data=None):
        row = int(path)
        self.cheatlist_liststore[row][5] = new_text
        if self.cheatlist_liststore[row][1]: # locked
            # data_worker will handle this
            pass
        else:
            # write it for once
            (lockflag, locked, desc, addr, typestr, value) = self.cheatlist_liststore[row]
            self.write_value(addr, typestr, value)
        return True

    def cheatlist_edit_type_cb(self, cell, path, new_text, data=None):
        row = int(path)
        self.cheatlist_liststore[row][4] = new_text
        if self.cheatlist_liststore[row][1]: # locked
            # false unlock it
            self.cheatlist_liststore[row][1] = False
            pass
        # TODO may need read & update (reinterpret) the value
        return True

    ############################
    # core functions
    def show_error(self, msg):
        dialog = gtk.MessageDialog(None
                                 ,gtk.DIALOG_MODAL
                                 ,gtk.MESSAGE_ERROR
                                 ,gtk.BUTTONS_OK
                                 ,msg)
        dialog.run()
        dialog.destroy()

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
        start_addr = selected_region['start_addr']
        end_addr = selected_region['end_addr']
        if self.last_hexedit_address[0] != start_addr or \
           self.last_hexedit_address[1] != end_addr:
            try:
                data = self.read_memory(start_addr, end_addr - start_addr)
            except IOError,e:
                self.show_error('Cannot')
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
    def backend_error_cb(self, msg):
        gtk.gdk.threads_enter()
        self.show_error('Backend error: %s'%(msg,))
        if self.is_scanning:
            self.finish_scan()
        gtk.gdk.threads_leave()
                   
    # this callback will be called from other thread
    def backend_progress_cb(self, cur, total):
        gtk.gdk.threads_enter()
        self.scanprogress_progressbar.set_fraction(float(cur)/total)
        if (cur == total) and self.is_scanning:
            self.finish_scan()
        gtk.gdk.threads_leave()

    def add_to_cheat_list(self, addr, value, typestr):
        # determine longest possible type
        types = typestr.split()
        for t in types:
            if TYPENAMES_S2G.has_key(t):
                vt = TYPENAMES_S2G[t]
                break
        self.cheatlist_liststore.prepend(['=', False, 'No Description', addr, vt, value])

    def get_process_list(self):
        return [e.strip().split(' ',1) for e in os.popen('ps -wweo pid=,command= --sort=-pid').readlines()]

    def select_process(self, pid, process_name):
        # ask backend for attaching the target process
        # update 'current process'
        # reset flags
        # for debug/log
#        print 'Select process: %d - %s' % (pid, process_name)
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
        self.backend.send_command('pid %d' % (pid,))
        self.reset_scan()
        # unlock all entries in cheat list
        for i in xrange(len(self.cheatlist_liststore)):
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
        self.backend.send_command('reset')
        self.update_scan_result()
        self.scanoption_frame.set_sensitive(True)

    def apply_scan_settings (self):
        # scan data type
        active = self.scan_data_type_combobox.get_active()
        assert(active >= 0)
        dt = self.scan_data_type_combobox.get_model()[active][0]
        self.backend.send_command('option scan_data_type %s' % (dt,))
        # search scope
        self.backend.send_command('option region_scan_level %d' %(1 + int(self.search_scope_scale.get_value()),))
        # TODO: ugly, reset to make region_scan_level taking effect
        self.backend.send_command('reset')

    
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
            cmd = misc.check_scan_command(data_type, cmd)
        except Exception,e:
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
        self.backend.send_command(cmd, get_output = False)

    def finish_scan(self):
        self.main_window.set_sensitive(True)
        self.memoryeditor_window.set_sensitive(True)
        self.backend.get_output_lines()
        self.is_scanning = False
        self.update_scan_result()
 
    def update_scan_result(self):
        self.found_count_label.set_text('Found: %d' % (self.backend.match_count,))
        if (self.backend.match_count > SCAN_RESULT_LIST_LIMIT):
            self.scanresult_liststore.clear()
        else:
            lines = self.backend.send_command('list')
            self.scanresult_tv.set_model(None)
            # temporarily disable model for scanresult_liststore for the sake of performance
            self.scanresult_liststore.clear()
            for line in lines:
                line = line[line.find(']')+1:]
                (a, v, t) = map(str.strip, line.split(',')[:3])
                a = '%x'%(int(a,16),)
                t = t[1:-1]
                self.scanresult_liststore.append([a, v, t])
            self.scanresult_tv.set_model(self.scanresult_liststore)

    # return (r1, r2) where all rows between r1 and r2 (INCLUSIVE) are visible
    # return None if no row visible
    def get_visible_rows(self, treeview):
        rect = treeview.get_visible_rect()
        (x1,y1) = treeview.tree_to_widget_coords(rect.x,rect.y)
        (x2,y2) = treeview.tree_to_widget_coords(rect.x+rect.width,rect.y+rect.height)
        tup = treeview.get_path_at_pos(x1, y1)
        if tup is None:
            return None
        r1 = tup[0][0]
        tup = treeview.get_path_at_pos(x2, y2)
        if tup is None:
            r2 = len(treeview.get_model()) - 1    
        else:
            r2 = tup[0][0]
        return (r1, r2)
 
    # read/write data periodly
    def data_worker(self):
        rows = self.get_visible_rows(self.scanresult_tv)
        if rows is not None:
            (r1, r2) = rows # [r1, r2] rows are visible
            # TODO: update some part of scanresult
        # TODO read unlocked values in cheat list
        # write locked values in cheat list
        for i in xrange(len(self.cheatlist_liststore)):
            (lockflag, locked, desc, addr, typestr, value) = self.cheatlist_liststore[i]
            if locked:
                self.write_value(addr, typestr, value)
        return not self.exit_flag

    def read_memory(self, addr, length):
        lines = self.backend.send_command('dump %x %d' % (addr, length))
        data = []
        for l in lines:
            data.extend([unichr(int(item,16)) for item in l.split()])
        if len(data) != length:
            raise Exception('Cannot access target memory')
        return ''.join(data)
            
    # addr could be int or str
    def write_value(self, addr, typestr, value):
        if isinstance(addr,int):
            addr = '%x'%(addr,)
        self.backend.send_command('write %s %s %s'%(typestr, addr, value))

    def exit(self, object, data=None):
        self.exit_flag = True
        gtk.main_quit()

    def main(self):
        gtk.main()

    

if __name__ == '__main__':
    gobject.threads_init()
    gtk.gdk.threads_init()
    GameConqueror().main()
