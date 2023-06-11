# filemap

File size visualiser using the Squarified Treemap method ([pdf](http://www.win.tue.nl/~vanwijk/stm.pdf)).

![Screenshot of app](capture2.png)

## Building
* Uses SDL (make sure sdl2-config is on path).
* Windows build uses MinGW.
* ImGui is fetched as a git submodule so make sure to clone with '--recurse-submodules' or download separately and place into external/imgui.


### Windows or Linux
```
make
```

## Use
Run ./filemap [name of folder] or filemap.exe [name of folder]

Pan the map by clicking and dragging the mouse.
The name and size of the hovered-over file is displayed.
Scolling travels up the tree and displays ancestors.
Scrolling while holding down click will zoom in/out.

## TODO
* Scanning could be _much_ faster
* UI improvements
