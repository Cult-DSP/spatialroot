## notes from first gui prototype

functional:

- source audio needs to prompt an os file selector
- same with remap csv
- layout needs to keep the drop down on the left but also provide an os file selector on the right
- transcod tab also needs os file selector drop down menu

I after i verify basic functionality

aesthetic:

- after these functional changes, i want to change the font and overall aesthetic to look professional, and like our old prototype, which I will provide an image of. it should like a native apple app.
- can we give the application a logo in desktop: gui/imgui/src/miniLogo.png

architectural: does it make sense to rewrite the app as an alloApp? it seems we have recreated our own app class with threading, but we could just use allolibs
