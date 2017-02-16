Image Compare Plugin for OBS Studio
===========================================

This plugin will compare the current frame of a source with a images
loaded from disk and executes a program, if one of the images matches.

The initial intension was to use it for auto splitting of speedruns
regardless if you play a game that is game captured, an emulator that is
window captured or even play on console that is captured using a video
device sources. But it can certainly used for other purposes too.

Build
-----

To build the plugin you have to integrate it into the build of OBS
Studio. Check out the repository into a subdirectory of the plugins
folder in the OBS Studio source and add it to the `CMakeLists.txt`.
