#!/usr/bin/env python
# -*- coding: utf-8 -*-
# HexView.py
#
# Extended by Wang Lu
# Added editing and cursor moving stuff
# Port to GTK 3
# Copyright (C) 2010,2011,2013 WANG Lu <coolwanglu@gmail.com>
#
# First version
# Copyright (C) 2008, 2009 Adriano Monteiro Marques
# Author: Francesco Piccinno <stack.box@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Pango
from gi.repository import GObject

class BaseText(Gtk.TextView):
    __gtype_name__ = 'BaseText'

    def __init__(self, parent):
        super(BaseText, self).__init__()
        self.buffer = self.get_buffer()

        self._parent = parent
        self.modify_font(Pango.FontDescription(parent.font))
        self.set_editable(False)
        self.texttag = self.buffer.create_tag(None)

GObject.type_register(BaseText)

class OffsetText(BaseText):
    __gtype_name__ = 'OffsetText'

    def __init__(self, parent):
        super(OffsetText, self).__init__(parent)
        self.off_len = 1
        self.connect('button-press-event', self.__on_button_press)
        self.connect('realize', self.__on_realize)

        self.set_cursor_visible(False)
        self.texttag.set_property('weight', Pango.Weight.BOLD)

    def __on_button_press(self, widget, evt):
        return True

    def __on_realize(self, widget):
        """
        TODO
        self.modify_base(Gtk.StateType.NORMAL, self.get_style().dark[Gtk.StateType.NORMAL])
        """
        return True

    def render(self, txt):
        self.buffer.set_text('')
        base_addr = self._parent.base_addr
        bpl = self._parent.bpl
        tot_lines = int(len(txt) / bpl)

        if len(txt) % bpl != 0:
            tot_lines += 1

        self.off_len = len('%x'%(base_addr+len(txt),))
        output = []

        for i in range(tot_lines):
            output.append(("%0" + str(self.off_len) + "x") % (base_addr + i*bpl))

        if output:
            self.buffer.insert_with_tags(
                self.buffer.get_end_iter(),
                "\n".join(output),
                self.texttag
            )

    def do_get_preferred_width(self):
        ctx = self.get_pango_context()
        font = ctx.load_font(Pango.FontDescription(self._parent.font))
        metric = font.get_metrics(ctx.get_language())

        w = metric.get_approximate_char_width() / Pango.SCALE * (self.off_len + 1)
        w += 2

        return w,w 

    def do_get_preferred_height(self):
        return 0,0

class AsciiText(BaseText):
    __gtype_name__ = 'AsciiText'
    _printable = \
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%" \
        "&'()*+,-./:;<=>?@[\]^_`{|}~ "

    def __init__(self, parent):
        super(AsciiText, self).__init__(parent)
        self.connect('key-press-event', self.__on_key_press)
        self.connect('button-release-event', self.__on_button_release)
        self.connect_after('move-cursor', self.__on_move_cursor)
        self.prev_start = None
        self.prev_end = None
        """
        TODO
        self.texttag.set_property(
            'background-gdk',
            self.get_style().text_aa[Gtk.StateType.NORMAL]
        )
        """
    def __on_move_cursor(self, textview, step_size, count, extend_selection, data=None):
        if True: #step_size == Gtk.MovementStep.VISUAL_POSITIONS:
            buffer = self.get_buffer()
            insert_mark = buffer.get_insert()
            insert_iter = buffer.get_iter_at_mark(insert_mark)
            insert_off = insert_iter.get_offset()
            if not extend_selection:
                if (insert_off+1) % (self._parent.bpl+1) == 0:
                    if count > 0:
                        # try to move forward
                        if insert_iter.is_end():
                            end_iter = insert_iter.copy()
                            insert_iter.backward_char()
                        else:
                            insert_iter.forward_char()
                            end_iter = insert_iter.copy()
                            end_iter.forward_char()
                    elif count < 0:
                        # try to move backward
                        if insert_iter.is_start():
                            end_iter = insert_iter.copy()
                            end_iter.forward_char()
                        else:
                            end_iter = insert_iter.copy()
                            insert_iter.backward_char()
                else:
                    if insert_iter.is_end():
                        end_iter = insert_iter.copy()
                        insert_iter.backward_char()
                    else:
                        end_iter = insert_iter.copy()
                        end_iter.forward_char()
                # select one char
                buffer.select_range(insert_iter, end_iter)
            
            # select one char
            buffer.select_range(insert_iter, end_iter)
        return True

    def __on_key_press(self, widget, evt, data=None):
        if not self._parent.editable:
            return False
        c = evt.keyval
        if c < 256 and (chr(c) in AsciiText._printable):
            buffer = self.get_buffer()
            bounds = buffer.get_selection_bounds()
            if bounds and (bounds[1].get_offset() - bounds[0].get_offset() > 1):
                self.select_a_char()
            else:
                iter = buffer.get_iter_at_mark(buffer.get_insert())
                off = iter.get_offset()
                org_off = off - off / (self._parent.bpl + 1)
                self._parent.emit('char-changed', org_off, c)
                self.select_a_char(buffer.get_iter_at_offset(off+1))
            return True
        return False

    def __on_button_release(self, widget, event, data=None):
        buffer = self.get_buffer()
        bounds = buffer.get_selection_bounds()
        if (not bounds) or (bounds[1].get_offset() - bounds[0].get_offset() == 1):
            self.select_a_char()
        # return False in order to let other handler handle it
        return False

    # we want at least one char is selected all the time
    def select_a_char(self, insert_iter = None):
        buffer = self.get_buffer()
        if insert_iter is None:
            insert_iter = buffer.get_iter_at_mark(buffer.get_insert())
        insert_off = insert_iter.get_offset()
        if (insert_off+1) % (self._parent.bpl+1) == 0:
            if insert_iter.is_end():
                end_iter = insert_iter.copy()
                insert_iter.backward_char()
            else:
                insert_iter.forward_char()
                end_iter = insert_iter.copy()
                end_iter.forward_char()
        else:
            end_iter = insert_iter.copy()
            end_iter.forward_char()
        buffer.select_range(insert_iter, end_iter)
        self.scroll_to_iter(insert_iter, 0, False, 0, 0)

    def render(self, txt):
        self.buffer.set_text('')

        bpl = self._parent.bpl
        tot_lines = int(len(txt) / bpl)

        if len(txt) % bpl != 0:
            tot_lines += 1

        output = []

        convert = lambda i: "".join(
            [(x in AsciiText._printable) and (x) or ('.') for x in list(i)])

        for i in range(tot_lines):
            if i * bpl + bpl > len(txt):
                output.append(
                    convert(txt[i * bpl:])
                )
            else:
                output.append(
                    convert(txt[i * bpl:(i * bpl) + bpl])
                )

        if output:
            self.buffer.insert_with_tags(
                self.buffer.get_end_iter(),
                "\n".join(output),
                self.texttag
            )

    def do_get_preferred_width(self):
        ctx = self.get_pango_context()
        font = ctx.load_font(Pango.FontDescription(self._parent.font))
        metric = font.get_metrics(ctx.get_language())

        w = metric.get_approximate_char_width() / Pango.SCALE * self._parent.bpl
        w += 2

        return w,w

    def do_get_preferred_height(self):
        return 0,0

    # start and end are offset to the original text
    def select_blocks(self, start=None, end=None):
        if not start and not end:
            # deselect
            if self.prev_start and self.prev_end and \
               self.prev_start != self.prev_end:

                self.buffer.remove_tag(self.texttag,
                                       self.buffer.get_iter_at_mark(self.prev_start),
                                       self.buffer.get_iter_at_mark(self.prev_end))

                self.buffer.delete_mark(self.prev_start)
                self.prev_start = None
                self.buffer.delete_mark(self.prev_end)
                self.prev_end = None
            return

        bpl = self._parent.bpl
        start += start/bpl
        end += end/bpl

        if self.prev_start and self.prev_end:
            if self.buffer.get_iter_at_mark(self.prev_start).get_offset() == start \
               and self.buffer.get_iter_at_mark(self.prev_end).get_offset() == end:
                # nothing to do
                return
            else:
                # remove old selection
                self.buffer.remove_tag(self.texttag,
                                       self.buffer.get_iter_at_mark(self.prev_start),
                                       self.buffer.get_iter_at_mark(self.prev_end))

        # apply new selection
        start_iter = self.buffer.get_iter_at_offset(start)
        end_iter = self.buffer.get_iter_at_offset(end)

        self.buffer.apply_tag(self.texttag, start_iter, end_iter)
        if self.prev_start:
            self.buffer.move_mark(self.prev_start, start_iter)
        else:
            self.prev_start = self.buffer.create_mark(None, start_iter, True)
        if self.prev_end:
            self.buffer.move_mark(self.prev_end, end_iter)
        else:
            self.prev_end = self.buffer.create_mark(None, end_iter, False)


class HexText(BaseText):
    __gtype_name__ = 'HexText'
    _hexdigits = '0123456789abcdefABCDEF'

    def __init__(self, parent):
        super(HexText, self).__init__(parent)
        self.connect('realize', self.__on_realize)
        self.connect('button-release-event', self.__on_button_release)
        self.connect('key-press-event', self.__on_key_press)
        self.connect_after('move-cursor', self.__on_move_cursor)

        self.prev_start = None
        self.prev_end = None
        """
        TODO
        self.texttag.set_property(
            'background-gdk',
            self.get_style().mid[Gtk.StateType.NORMAL]
        )
        """

    def __on_key_press(self, widget, evt, data=None):
        if not self._parent.editable:
            return False
        char = evt.keyval
        if char < 256 and (chr(char) in HexText._hexdigits):
            buffer = self.get_buffer()
            bounds = buffer.get_selection_bounds()
            if bounds and (bounds[1].get_offset() - bounds[0].get_offset() > 1):
                self.select_a_char()
            else:
                c = chr(char).upper()
                iter = buffer.get_iter_at_mark(buffer.get_insert())
                off = iter.get_offset()
                pos = off % 3
                org_off = off / 3
                txt = buffer.get_text(
                        buffer.get_iter_at_offset(org_off*3),
                        buffer.get_iter_at_offset(org_off*3+2),
                        True)
                if pos < 2:
                    l = list(txt)
                    l[pos] = c
                    self._parent.emit('char-changed', org_off, int(''.join(l),16))
                    self.select_a_char(buffer.get_iter_at_offset(off+1))
            return True
        return False


    def __on_button_release(self, widget, event, data=None):
        buffer = self.get_buffer()
        bounds = buffer.get_selection_bounds()
        if (not bounds) or (bounds[1].get_offset() - bounds[0].get_offset() == 1):
            self.select_a_char()
        # return False in order to let other handler handle it
        return False

    # we want at least one char is selected all the time
    def select_a_char(self, insert_iter = None):
        buffer = self.get_buffer()
        if insert_iter is None:
            insert_iter = buffer.get_iter_at_mark(buffer.get_insert())
        insert_off = insert_iter.get_offset()
        if insert_off % 3 == 2:
            if insert_iter.is_end():
                end_iter = insert_iter.copy()
                insert_iter.backward_char()
            else:
                insert_iter.forward_char()
                end_iter = insert_iter.copy()
                end_iter.forward_char()
        else:
            end_iter = insert_iter.copy()
            end_iter.forward_char()
        buffer.select_range(insert_iter, end_iter)
        self.scroll_to_iter(insert_iter, 0, False, 0, 0)

    def __on_move_cursor(self, textview, step_size, count, extend_selection, data=None):
        if True: #step_size == Gtk.MovementStep.VISUAL_POSITIONS:
            buffer = self.get_buffer()
            insert_mark = buffer.get_insert()
            insert_iter = buffer.get_iter_at_mark(insert_mark)
            insert_off = insert_iter.get_offset()
            if not extend_selection:
                if insert_off % 3 == 2:
                    if count > 0:
                        # try to move forward
                        if insert_iter.is_end():
                            end_iter = insert_iter.copy()
                            insert_iter.backward_char()
                        else:
                            insert_iter.forward_char()
                            end_iter = insert_iter.copy()
                            end_iter.forward_char()
                    elif count < 0:
                        # try to move backward
                        if insert_iter.is_start():
                            end_iter = insert_iter.copy()
                            end_iter.forward_char()
                        else:
                            end_iter = insert_iter.copy()
                            insert_iter.backward_char()
                else:
                    if insert_iter.is_end():
                        end_iter = insert_iter.copy()
                        insert_iter.backward_char()
                    else:
                        end_iter = insert_iter.copy()
                        end_iter.forward_char()
                # select one char
                buffer.select_range(insert_iter, end_iter)
            return True
        return False

    def __on_realize(self, widget):
        """
        TODO
        self.modify_base(Gtk.StateType.NORMAL, self.get_style().mid[Gtk.StateType.NORMAL])
        """
        return True

    def render(self, txt):
        self.buffer.set_text('')

        bpl = self._parent.bpl
        tot_lines = int(len(txt) / bpl)

        if len(txt) % bpl != 0:
            tot_lines += 1

        output = []
        convert = lambda x: '%02X'%(ord(x),)

        for i in range(tot_lines):
            if i * bpl + bpl > len(txt):
                output.append(
                    " ".join(map(convert, txt[i * bpl:]))
                )
            else:
                output.append(
                    " ".join(map(convert, txt[i * bpl:(i * bpl) + bpl]))
                )

        if output:
            self.buffer.insert_with_tags(
                self.buffer.get_end_iter(),
                "\n".join(output).upper(),
                self.texttag
            )

    def do_get_preferred_width(self):
        ctx = self.get_pango_context()
        font = ctx.load_font(Pango.FontDescription(self._parent.font))
        metric = font.get_metrics(ctx.get_language())

        w = metric.get_approximate_char_width() / Pango.SCALE * \
                        (self._parent.bpl * 3 - 1)
        w += 2
        return w,w

    def do_get_preferred_height(self):
        return 0,0

    # start and end are offset to the original text
    def select_blocks(self, start=None, end=None):
        if not start and not end:
            # deselect
            if self.prev_start and self.prev_end and \
               self.prev_start != self.prev_end:
                self.buffer.remove_tag(self.texttag,
                                       self.buffer.get_iter_at_mark(self.prev_start),
                                       self.buffer.get_iter_at_mark(self.prev_end))

                self.buffer.delete_mark(self.prev_start)
                self.prev_start = None
                self.buffer.delete_mark(self.prev_end)
                self.prev_end = None
            return

        start *= 3
        end = end * 3 -1
        
        if self.prev_start and self.prev_end:
            if self.buffer.get_iter_at_mark(self.prev_start).get_offset() == start \
               and self.buffer.get_iter_at_mark(self.prev_end).get_offset() == end:
                return
            else:
                # remove old selection
                self.buffer.remove_tag(self.texttag,
                                       self.buffer.get_iter_at_mark(self.prev_start),
                                       self.buffer.get_iter_at_mark(self.prev_end))

        start_iter = self.buffer.get_iter_at_offset(start)
        end_iter = self.buffer.get_iter_at_offset(end)

        self.buffer.apply_tag(self.texttag, start_iter, end_iter)
        if self.prev_start:
            self.buffer.move_mark(self.prev_start, start_iter)
        else:
            self.prev_start = self.buffer.create_mark(None, start_iter, True)
        if self.prev_end:
            self.buffer.move_mark(self.prev_end, end_iter)
        else:
            self.prev_end = self.buffer.create_mark(None, end_iter, False)

class HexView(Gtk.Box):
    __gtype_name__ = 'HexView'
    __gsignals__ = {
        'char-changed' : (GObject.SignalFlags.RUN_LAST, GObject.TYPE_BOOLEAN, (int,int))
    }

    def __init__(self):
        super(HexView, self).__init__(False, 4)
        self.set_border_width(4)

        self._bpl = 16
        self._font = "Monospace 10"
        self._payload = ""
        self._base_addr = 0;
        self.scroll_mark = None
        self.editable = False

        self.vadj = Gtk.Adjustment()
        self.vscroll = Gtk.Scrollbar.new(Gtk.Orientation.VERTICAL, self.vadj)


        self.offset_text = OffsetText(self)
        self.hex_text = HexText(self)
        self.ascii_text = AsciiText(self)

        self.offset_text.set_vadjustment(self.vadj)
        self.hex_text.set_vadjustment(self.vadj)
        self.ascii_text.set_vadjustment(self.vadj)

        self.hex_text.buffer.connect('mark-set', self.__on_hex_change)
        self.ascii_text.buffer.connect('mark-set', self.__on_ascii_change)

        def scroll(widget):
            widget.set_size_request(-1, 128)
            frame = Gtk.ScrolledWindow.new(None, self.vadj)
#            frame.set_shadow_type(Gtk.ShadowType.IN)
            frame.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.NEVER)
            frame.add(widget)
            frame.connect('scroll-event', self.__on_scroll_event)
            return frame

        self.pack_start(scroll(self.offset_text), expand=False, fill=False, padding=0)
        self.pack_start(scroll(self.hex_text), expand=False, fill=False, padding=0)
        self.pack_start(scroll(self.ascii_text), expand=False, fill=False, padding=0)
        self.pack_end(self.vscroll, False, False, 0)

#        self.connect('char-changed', self.do_char_changed)

    def __on_scroll_event(self, widget, event, data=None):
        self.vscroll.emit('scroll-event', event.copy())
        return True

    # scroll to the addr
    # select the byte at addr
    # set focus
    def show_addr(self, addr):
        GObject.idle_add(self.show_addr_helper, addr)

    def show_addr_helper(self, addr):
        off = addr - self._base_addr
        off *= 3

        buf = self.hex_text.get_buffer()
        off_iter = buf.get_iter_at_offset(off)

        if self.scroll_mark is None:
            self.scroll_mark = buf.create_mark(None, off_iter)
        else:
            buf.move_mark(self.scroll_mark, off_iter)

        self.hex_text.scroll_to_mark(self.scroll_mark, 0, True, 0, 0)
        iter2 = off_iter.copy()
        iter2.forward_char()
        buf.select_range(off_iter, iter2)
        self.hex_text.grab_focus()

    def do_realize(self):
        Gtk.Box.do_realize(self)
        # set font
        self.modify_font(self._font)


    def __on_hex_change(self, buffer, iter, mark):
        if not buffer.get_selection_bounds():
            self.ascii_text.select_blocks() # Deselect
            return True

        start, end = buffer.get_selection_bounds()

        s_off = start.get_offset()
        e_off = end.get_offset()

        self.ascii_text.select_blocks((s_off+1) / 3, (e_off-1) / 3+1)
        return True

    def __on_ascii_change(self, buffer, iter, mark):
        if not self.ascii_text.buffer.get_selection_bounds():
            self.hex_text.select_blocks() # Deselect
            return True

        start_iter, end_iter = self.ascii_text.buffer.get_selection_bounds()

        start_off = start_iter.get_offset()
        end_off = end_iter.get_offset()
        bpl = self._bpl

        self.hex_text.select_blocks(
            start_off - start_off / (bpl + 1),
            end_off - end_off / (bpl + 1)
        )
        return True

    def do_char_changed(self, offset, charval):
        hex_buffer = self.hex_text.get_buffer()
        ascii_buffer = self.ascii_text.get_buffer()
        # set text
        # set hex
        iter1 = hex_buffer.get_iter_at_offset(offset * 3)
        iter2 = hex_buffer.get_iter_at_offset(offset * 3 + 2)
        hex_buffer.delete(iter1, iter2)
        hex_buffer.insert(iter1, '%02X'%(charval,))
        # set ascii
        iter1 = ascii_buffer.get_iter_at_offset(offset + offset / self._bpl)
        iter2 = ascii_buffer.get_iter_at_offset(offset + offset / self._bpl + 1)
        ascii_buffer.delete(iter1, iter2)
        char = chr(charval)
        ascii_buffer.insert(iter1, (char in AsciiText._printable and char or '.'))
        return True

    def get_payload(self):
        return self._payload
    def set_payload(self, val):
        self._payload = val

        for view in (self.offset_text, self.hex_text, self.ascii_text):
            # Invalidate previous iters
            if hasattr(view, 'prev_start'):
                view.prev_start = None
            if hasattr(view, 'prev_end'):
                view.prev_end = None

            view.render(self._payload)

    def get_font(self):
        return self._font

    def modify_font(self, val):
        try:
            desc = Pango.FontDescription(val)
            self._font = val

            for view in (self.offset_text, self.hex_text, self.ascii_text):
                view.modify_font(desc)
        except Exception:
            pass

    def get_bpl(self):
        return self._bpl
    def set_bpl(self, val):
        self._bpl = val
        # Redraw!
        self.payload = self.payload

    def get_base_addr(self):
        return self._base_addr
    def set_base_addr(self, val):
        self._base_addr = val
        view = self.offset_text
        # Invalidate previous iters
        if hasattr(view, 'prev_start'):
            view.prev_start = None
        if hasattr(view, 'prev_end'):
            view.prev_end = None

        view.render(self._payload)


    payload = property(get_payload, set_payload)
    font = property(get_font, modify_font)
    bpl = property(get_bpl, set_bpl)
    base_addr = property(get_base_addr, set_base_addr)

GObject.type_register(HexView)

if __name__ == "__main__":
    def char_changed_handler(hexview, offset, charval):
        #print 'handler:','%X' % (offset,), chr(charval), '%02X' % (charval,)
        return False
    w = Gtk.Window()
    w.resize(500,500)
    view = HexView()
    view.payload = "Woo welcome this is a simple read/only HexView widget for PacketManipulator"*16
    view.base_addr = 0x6fff000000000000;
#    view.connect('char-changed', char_changed_handler)
    view.editable = True
    w.add(view)
    w.show_all()
    w.connect('delete-event', lambda *w: Gtk.main_quit())
    Gtk.main()
