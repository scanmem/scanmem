SUBDIRS = icons

dist_bin_SCRIPTS = gameconqueror
dist_gameconqueror_DATA = GameConqueror.xml GameConqueror_128x128.png GameConqueror_72x72.png
dist_gameconqueror_DATA += backend.py consts.py misc.py hexview.py
dist_gameconqueror_SCRIPTS = GameConqueror.py
dist_desktop_in_files = GameConqueror.desktop.in
dist_desktop_DATA = $(dist_desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@
dist_polkit_DATA = org.freedesktop.gameconqueror.policy
dist_man_MANS = gameconqueror.1
dist_doc_DATA = README TODO COPYING
EXTRA_DIST = gameconqueror.in consts.py.in org.freedesktop.gameconqueror.policy.in

gameconquerordir=$(datadir)/gameconqueror
desktopdir=$(datadir)/applications
polkitdir=$(datadir)/polkit-1/actions
