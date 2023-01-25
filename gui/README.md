# ![](https://raw.githubusercontent.com/scanmem/scanmem/main/gui/GameConqueror_48x48.png) GameConqueror

Game Conqueror is a graphical game cheating tool under Linux, a frontend for scanmem.


## Requirements

```
libscanmem (latest version)
Python (2.x / 3.x)
PyGObject
GTK+ 3
policykit
```

## Usage

The interface is similar to the Windows-only Cheat Engine.

- Enter a value to search in the Value entry, then press Enter or the scan buttons
  * Check supported syntax hovering over the '?' mark
  * You scan choose the search region/type via the dropdowns.
  * Number of matches will be shown in 'Found: XX' at topleft
  * Matches will be displayed in the left list if there are not too many
- Add a candidate address to the list below by double-click on it or use the context-menu
- There you can edit description, value, and lock it.

## Screenshot

![](https://raw.githubusercontent.com/scanmem/scanmem/main/gui/screenshot.png)

## Authors

WANG Lu            <coolwanglu(a)gmail.com>  
Andrea Stacchiotti <andreastacchiotti(a)gmail.com>

## License

GPLv3
