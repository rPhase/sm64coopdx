# sm64ex-coop

Online multiplayer mod for SM64 that synchronizes all entities and every level for multiple players, targeting Android and other open-source UNIX-like operating systems, plus improved touchscreen controls. In addition to code from github parents of this fork, contains code copied from [AloXado320/sm64ex-alo](https://github.com/AloXado320/sm64ex-alo), [VDavid003/sm64-port-android](https://github.com/VDavid003/sm64-port-android), [VDavid003/sm64-port-android-base](https://github.com/VDavid003/sm64-port-android-base), and [porcino/sm64-port-android](https://github.com/porcino/sm64-port-android).

Feel free to report bugs and contribute, but remember, there must be **no upload of any copyrighted asset**. 
Run `./extract_assets.py --clean && make clean` or `make distclean` to remove ROM-originated content.

## How to Play on Android

[Click here to read the guide for Android](README_android.md).

> Android 14, which released on October 4, 2023, **partially breaks sm64ex-coop builder Termux v0.1.2**, but not entirely. My advice for using sm64ex-coop builder Termux v0.1.2 on Android 14 is to **type `apt-mark hold bash && yes | pkg upgrade -y` and press Enter, then wait for the command to complete, then touch "Exit (touch to reset)" in your notifications, then open sm64ex-coop builder Termux again**. This bug is a manifestation of **[upstream Termux bug #3647](https://github.com/termux/termux-app/issues/3647)** in sm64ex-coop builder Termux, and this workaround is my recommended implementation of [the solution for upstream](https://github.com/termux/termux-app/issues/3647#issuecomment-1765307488) backported to sm64ex-coop builder Termux v0.1.2. This behavior will eventually be fixed in a future release of sm64ex-coop builder Termux once I decide the current release is too broken to continue distributing sm64ex-coop for Android.

* The easiest way is to download [sm64ex-coop builder Termux](https://github.com/robertkirkman/termux-app/releases/download/v0.1.2/termux-app_v0.1.2+apt-android-7-github-debug_universal.apk)
* If you already have Termux, you can try the auto builder without removing your current version of Termux by copying and pasting this command into it, then pushing enter:
```bash
bash <(curl -s https://raw.githubusercontent.com/robertkirkman/termux-packages/master/packages/bash/bin-build-sm64ex-coop.sh)
```

[Click here to build the `.apk` from a non-Android device](https://github.com/robertkirkman/sm64ex-coop-android-base). 

Recording showcasing touchscreen button features:

[sm64ex-coop-android.webm](https://user-images.githubusercontent.com/31490854/213303280-b4a160a6-f711-4497-b9e1-d463546048e1.webm)

Recording showcasing several mods:

[olaiabuttons.webm](https://user-images.githubusercontent.com/31490854/215781008-f83f9659-1ea2-4fbb-bf8d-43a0e0c6a1f6.webm)

* [Render96 models and textures](https://github.com/Render96/Render96ex) ([SonicDark The Angel ✪🇨🇱#5827's colored version](https://web.archive.org/web/20231228171913if_/https://sm64ex-coopmods.com/wp-content/uploads/2023/01/Render96_Chars.zip))

* [Sharen's Mod](https://mods.sm64coopdx.com/mods/sharens-animation-overhaul.262/)

* [JustOlaia's HD touchscreen button textures](https://github.com/JustOlaia/sm64ex-coop-apk/tree/coop/textures/touchcontrols)

All custom icons by **xLuigiGamerx#6999**!

Working:

✅ Multiplayer (including cross play with other builds of sm64ex-coop version 36.1)

✅ DynOS and Lua mods designed for sm64ex-coop version 36.1

✅ GPU acceleration

✅ Audio

✅ Touchscreen controls

✅ Fullscreen and more modes

✅ External gamepads

Not currently supported ([planned for a future update](https://github.com/robertkirkman/sm64ex-coop/issues/19)):

❌ Version 37+ exclusive Lua mods

❌ Cross play with version 37+

Not planned:

❌ Android force closes the app when loading HD texture packs or generating new `.lvl` files 
- Try restarting app or [using process killer workarounds](https://github.com/agnostic-apollo/Android-Docs/blob/master/en/docs/apps/processes/phantom-cached-and-empty-processes.md#commands-to-disable-phantom-process-killing-and-tldr)

❌ Discord
- This voip service's closed-source SDK is thought to be prohibitively difficult to use on alternative operating systems. Let me know if you would like [Mumla](https://f-droid.org/en/packages/se.lublin.mumla/) integration instead!

## How to Play on any open-source UNIX-like OS

This repository also contains changes meant to make porting sm64ex-coop to open-source UNIX-like operating systems easier (the normal sm64ex-coop only supports GNU/Linux and MacOS at the exclusion of others). On many of them, setting up the dependencies (a C and C++ preprocessor and compiler, GNU Make, readline, binutils, libcurl, Python 3, SDL2, GLEW, an OpenGL or OpenGL ES graphics driver, and an SDL2-compatible display server) can take many steps that vary dramatically, so I've decided not to make a complete walkthrough for every single one like I have for Android. Once you have all of those configured just right, though, you'll be able to compile and play this way, and hopefully you'll only need to make minor adjustments to the `Makefile` to detect the dependencies the way your OS needs it to.

```
git clone --recursive https://github.com/robertkirkman/sm64ex-coop.git
cp /path/to/baserom.us.z64 sm64ex-coop/baserom.us.z64
cd sm64ex-coop
TOUCH_CONTROLS=1 make
build/us_pc/sm64.us.f3dex2e
```

To get you started, I've confirmed my fork to work on the following operating systems (as well as GNU/Linux), and have committed initial changes for them:

✅ postmarketOS

✅ FreeBSD

✅ OpenBSD

There are some bugs when running on them, but I've fixed just enough to get sm64ex-coop to compile, launch and host servers with mods in a playable state on them. On OpenBSD and FreeBSD, to build, I install `pkg-config` and temporarily symlink `make` to `gmake`, `gcc` to `clang`, `g++` to `clang++`, and `cpp` to `clang-cpp`. On postmarketOS, I use `clang` and install `bsd-compat-headers`. I strongly suspect that if your device has a non-`amd64` architecture and you find bugs, you'll probably want to look through my "`__ANDROID__`" `#ifdef`s in the code and check whether the code I've placed in them turns out to be architecture-related rather than Android-related.

## How to get this mode
![Screenshot_20231028-102901_Termux_X11](https://github.com/robertkirkman/sm64ex-coop/assets/31490854/166e8569-634e-454a-b271-a2d9dffae294)
> Build on Android for X11 windowing backend and launch in [Termux:X11](https://github.com/termux/termux-x11). Use your preferred Termux:X11 graphics driver(s); `virglrenderer-android` shown as example.
```
pkg install x11-repo
pkg install git wget make python getconf zip clang binutils sdl2 zlib libglvnd-dev termux-x11-nightly xfce virglrenderer-android
pkg remove which # having which installed selects SurfaceFlinger windowing backend, and not having it installed selects X11 windowing backend. sorry for this insanity. also remember to uninstall libglvnd anytime you build for SurfaceFlinger windowing backend.
git clone --recursive https://github.com/robertkirkman/sm64ex-coop.git
cp /path/to/baserom.us.z64 sm64ex-coop/baserom.us.z64
cd sm64ex-coop
USE_GLES=1 TOUCH_CONTROLS=1 NO_PIE=0 make
virgl_test_server_android &
termux-x11 :0 -xstartup "xfce4-session" &
DISPLAY=:0 GALLIUM_DRIVER=virpipe build/us_pc/sm64.us.f3dex2e
```
