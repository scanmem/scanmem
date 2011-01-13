#!/usr/bin/env python

"""
Dirty script for building package for PPA
by WangLu
2011.01.13
"""


import os
import sys
import re
import time

print 'SVN commit...'
if os.system('svn ci') != 0:
    print 'Failed in svn commit.'
    sys.exit(-1)

print
print 'Generating version...'
try:
    rev = re.findall(r'Revision:\s*(\d+)', os.popen('svn info').read())[0]
except:
    print 'Cannot get revision number'
    sys.exit(-1)

today_timestr = time.strftime('%Y%m%d')
try:
    package,version = re.findall(r'AC_INIT\(\[([^]]+)\],\s*\[([^]]+)\]', open('configure.ac').read())[0]
except:
    print 'Cannot get package name and version number'
    sys.exit(-1)

# debug, current "-2" for overwriting last version, 
# change back to 1 next time!
full_deb_version = version+'-2~svn'+today_timestr+'r'+rev+'-0ubuntu1'

#check if we need to update debian/changelog
if re.findall(r'\(([^)]+)\)', open('debian/changelog').readline())[0] == full_deb_version:
    print
    print 'No need to update debian/changelog, skipping'
else:
    print
    print 'Writing debian/changelog'
    if os.system('dch -v "%s"' % (full_deb_version,)) != 0:
        print 'Failed when updating debian/changelog'
        sys.exit(-1)

# commit again
if os.system('svn ci -m "update debian/changelog for packaging"') != 0:
    print 'Failed in svn commit.'
    sys.exit(-1)

print
print 'Creating tarball'
if os.system('./configure --enable-gui && make dist') != 0:
    print 'Failed in creating tarball'
    sys.exit(-1)


