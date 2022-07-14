# Lite XL - Simplified

A lightweight text editor written in Lua, adapted from [lite-xl]. Makes it easier to build
on different platforms if you're having trouble with meson.

Will always be rebased off upstream [lite-xl]; will never deviate far.

Quickstart, if you have a C compiler.

```
git clone git@github.com:adamharrison/lite-xl-simplified.git --shallow-submodules --recurse-submodules && cd lite-xl-simplified && ./build.sh && ./lite-xl
````

## Supporting Libraries

The 4 supporting libraries of lite are now git submodules. These **must** be pulled in with: 
`git submodule update --remote --init --depth=1` after cloning the repository, or by the above clone command.

The build tool will automatically build all necessary libraries.

Alternatively, you can supply your system libraries on the command line like so, to build from your system:

```
./build.sh `pkg-config lua5.4 freetype2 libpcre2-8 --cflags` `pkg-config lua5.4 freetype2 libpcre2-8 --libs` `sdl2-config --cflags` `sdl2-config --libs`.
```

## Building

### Linux, Mac, Windows/MSYS

**To build**, simply run `build.sh`; this should function on Mac, Linux and MSYS command line.

If you desperately want better build times, you can speed up builds by specifying a `ccache`
`CC` variable (e.g. `CC='ccache gcc' ./build.sh`). After the first build, these builds should
be quite quick (on my machine, building from scratch moves from 1 second to about .1 seconds).

### Windows/cmd.exe

**If you're running on windows on the command line; you should use `build.cmd`.**

**On Windows, if building using cmd.exe**, you should place `SDLmain.lib`, `SDL.lib`,
`SDL.dll` into the main folder project directory, before running a build. You can retrieve
these [here](https://www.libsdl.org/release/SDL2-devel-2.0.16-VC.zip). They're located under
lib/x64.


## Cross Compiling

If you are cross compiling, between each build, you should run `./build.sh clean`.

### Linux to Windows

From Linux, to compile a windows executable, all you need to do is make sure you have mingw64 (`sudo apt-get install mingw-w64`).

```
CC=i686-w64-mingw32-gcc AR=i686-w64-mingw32-gcc-ar SDL_CONFIGURE="--host=i686-w64-mingw32" ./build.sh
```

### Linux to MacOS

From linux, to compile a mac executable, you can use (OSXCross)[https://github.com/tpoechtrager/osxcross].

```
CC=o32-clang AR=o32-llvm-ar SDL_CONFIGURE="--host=i386-apple-darwinXX" ./build.sh
```

### Linux to Webassembly

To compile to webassembly, install emscripten, and simply go to the main folder, have the emsdk prefab SDL installed, and do:

```
AR=emar CC=emcc ./build.sh -I`$EMSDK/upstream/emscripten/system/bin/sdl2-config --cflags` `$EMSDK/upstream/emscripten/system/bin/sdl2-config --libs` -o index.html -s ASYNCIFY -s USE_SDL=2 --preload-file data -s INITIAL_MEMORY=134217728 --shell-file resources/lite-xl.html
```

## Licenses

This project is free software; you can redistribute it and/or modify it under
the terms of the MIT license. Dependencies are licensed under various open
source licenses.  See [LICENSE] for details.

[lite-xl]:                    https://github.com/lite-xl/lite-xl
[LICENSE]:                    LICENSE
