# scanmem & Gameconqueror

scanmem is a debugging utility designed to isolate the address of an arbitrary
variable in an executing process. scanmem simply needs to be told the pid of
the process, and the value of the variable at several different times.

After several scans of the process, scanmem isolates the position of the
variable and allows you to modify it's value.

## Requirements

scanmem requires libreadline to read commands interactively, and /proc must be
mounted.


## Install
If you obtained the source from the source repository (instead of a archive), run:
    ./autogen.sh

To build with gui:

    ./configure --prefix=/usr --enable-gui && make    
    sudo make install

To build without gui:

    ./configure --prefix=/usr && make
    sudo make install

If you want to specify a prefix other than '/usr', make sure that <prefix>/lib is in the
LD_LIBRARY_PATH for root

## GUI

GameConqueror is a GUI front-end for scanmem, providing more features, such as:
  * Easier variable locking
  * Better process finder
  * Memory browser/editor
  * Expression evaluation for searching

See gui/README for more detail.


## Known Issues

* some hardened systems have unusable maps files, where all entries are zeroed.
* the snapshot command uses too much memory on large processes.
* performance is currently very poor.


Author: Tavis Ormandy <taviso(a)sdf.lonestar.org>
        Eli   Dupree  <elidupree(a)charter.net>
        WANG  Lu      <coolwanglu(a)gmail.com>

License: GPLv3

## Resources

Official site: http://code.google.com/p/scanmem

Development: https://github.com/coolwanglu/scanmem
