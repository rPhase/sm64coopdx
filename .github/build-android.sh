#!/bin/bash

yes | pkg upgrade -y # Upgrading Packages

pkg install -y git wget make python getconf zip apksigner clang binutils libglvnd-dev aapt which # Installing Dependencies

# Begin workaround exclusively to allow building mario 64 APKs
# specifically inside termux-docker (rather than normal Termux)
# while preventing 'SDL Error'

# in this specific situation this is necessary and OK
# as long as the next commands are also used
pkg reinstall -y libglvnd

# remove symbolic links that are the source of
# makes the package 'libglvnd' cause SDL Error
rm $PREFIX/lib/libGLESv2.so
rm $PREFIX/lib/libGLESv2.so.2

# rename build-time OpenGL ES 2 library to simulate
# the presence of a /system folder libGLESv2.so
# (which termux-docker does not have)
# by matching the filename 
mv $PREFIX/lib/libGLESv2.so.2.1.0 $PREFIX/lib/libGLESv2.so

# End workaround

make