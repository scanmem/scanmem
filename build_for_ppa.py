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
deb_version = version+'-2~svn'+today_timestr+'r'+rev+'-0ubuntu1'

#check if we need to update debian/changelog
if re.findall(r'\(([^)]+)\)', open('debian/changelog').readline())[0] == deb_version:
    print
    print 'No need to update debian/changelog, skipping'
else:
    print
    print 'Writing debian/changelog'
    if os.system('dch -v "%s"' % (deb_version,)) != 0:
        print 'Failed when updating debian/changelog'
        sys.exit(-1)

# commit again
if os.system('svn ci -m "update debian/changelog for packaging"') != 0:
    print 'Failed in svn commit.'
    sys.exit(-1)

print
print 'Building...'
# handling files
if os.system('./configure --enable-gui && make dist') != 0:
    print 'Failed in creating tarball'
    sys.exit(-1)

orig_tar_filename = package+'-'+version+'.tar.gz'
if os.system('test -e %s && cp %s ../build-area' % (orig_tar_filename, orig_tar_filename)) != 0:
    print 'Cannot copy tarball file to build area'
    sys.exit(-1)

deb_orig_tar_filename = package+'_'+deb_version+'.orig.tar.gz'

try:
    os.chdir('../build-area')
except:
    print 'Cannot find ../build-area'
    sys.exit(-1)
try:
    os.rmdir(package+'-'+version)
except:
    pass

if os.system('mv %s %s && tar -xvf %s' % (orig_tar_filename, deb_orig_tar_filename, deb_orig_tar_filename)) != 0:
    print 'Cannot extract tarball'
    sys.exit(-1)

try:
    os.chdir(package+'-'+version)
except:
    print 'Cannot enter project dir'
    sys.exit(-1)

os.system('cp -r ../../%s/debian .' % (package,))

# building
if os.system('debuild -S -sa') != 0:
    print 'Failed in debuild'
    sys.exit(-1)

print
sys.stdout.write('Everything seems to be good so far, upload?(y/n)')
sys.stdout.flush()
ans = raw_input().lower()
while ans not in ['y', 'n']:
    sys.stdout.write('I don\'t understand, enter \'y\' or \'n\':')
    ans = raw_input().lower()

if ans == 'n':
    print 'Skipped.'
    sys.exit(0)
   
if os.system('dput ppa:coolwanglu/%s' % (package,)) != 0:
    print 'Failed in uploading by dput'
    sys.exit(-1)

print 'All done. Cool!'



