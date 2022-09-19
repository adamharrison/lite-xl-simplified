# Lite XL - Simplified

A lightweight text editor written in Lua, adapted from [lite-xl]. Makes it easier to build
on different platforms if you're having trouble with meson.

Will always be rebased off upstream [lite-xl]; will never deviate far.

[Click here](https://adamharrison.github.io/lite-xl-simplified/) to try it out online!

## Quickstart

If you have a C compiler, and `git`:

```
git clone git@github.com:adamharrison/lite-xl-simplified.git --shallow-submodules \
  --recurse-submodules && cd lite-xl-simplified && ./build.sh && ./lite-xl
````

CI is enabled on this repository, so you can grab Windows and Linux builds from the 
`continuous` [release page](https://github.com/adamharrison/lite-xl-simplified/releases/tag/continuous).

## Supporting Libraries

The 4 supporting libraries of lite are now git submodules. These **must** be pulled in with: 
`git submodule update --remote --init --depth=1` after cloning the repository, or by the above clone command.

The build tool will automatically build all necessary libraries.

Alternatively, you can supply your system libraries on the command line like so, to build from your system:

```
./build.sh `pkg-config lua5.4 freetype2 libpcre2-8 --cflags`\
  `pkg-config lua5.4 freetype2 libpcre2-8 --libs` `sdl2-config --cflags` `sdl2-config --libs`.
```

## Building

### Linux, Mac, Windows/MSYS, FreeBSD

**To build**, simply run `build.sh`; this should function on Mac, Linux and MSYS command line.

If you desperately want better build times, you can speed up builds by specifying a `ccache`
`CC` variable (e.g. `CC='ccache gcc' ./build.sh`). After the first build, these builds should
be quite quick (on my machine, building from scratch moves from 1 second to about .1 seconds).

### Cross Compiling

If you are cross compiling, between each build, you should run `./build.sh clean`.

#### Linux to Windows

From Linux, to compile a windows executable, all you need to do is make sure you have mingw64 (`sudo apt-get install mingw-w64`).

```
CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-gcc-ar SDL_CONFIGURE="--host=x86_64-w64-mingw32" ./build.sh
```

#### Linux to MacOS

From linux, to compile a mac executable, you can use (OSXCross)[https://github.com/tpoechtrager/cctools-port]. 
This is complicated and a clusterfuck to install, because Mac is awful.

```
CC=clang AR=llvm-ar SDL_CONFIGURE="--host=i386-apple-darwin11" ./build.sh
```

#### Linux to Webassembly

To get the emscripten SDK:

```
git clone https://github.com/emscripten-core/emsdk.git && cd emsdk && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh && cd ..
```

To compile to webassembly, do:

```
AR=emar CC=emcc ./build.sh -I`$EMSDK/upstream/emscripten/system/bin/sdl2-config --cflags` `$EMSDK/upstream/emscripten/system/bin/sdl2-config --libs` -o index.html -s ASYNCIFY -s USE_SDL=2 -s ASYNCIFY_WHITELIST="['main','SDL_WaitEvent','SDL_WaitEventTimeout','SDL_Delay','Emscripten_GLES_SwapWindow','SDL_UpdateWindowSurfaceRects','f_call','luaD_callnoyield','luaV_execute','luaD_precall','precallC','luaD_call','f_sleep','Emscripten_UpdateWindowFramebuffer','luaC_freeallobjects','GCTM','luaD_rawrunprotected','lua_close','close_state','f_end_frame','rencache_end_frame','ren_update_rects','renwin_update_rects','lua_pcallk','luaB_xpcall','dynCall_vii','f_wait_event']"  --preload-file data -s INITIAL_MEMORY=33554432 -s DISABLE_EXCEPTION_CATCHING=1 -s ALLOW_MEMORY_GROWTH=1 --shell-file resources/lite-xl.html
```

### Modes

In additional to the normal mode of compilation, we also provide a number of different pieces of functionality. These can be mixed and matched.

#### LuaJIT

To add luajit into the build, you can do:

```
./build.sh `pkg-config luajit --cflags` `pkg-config luajit --libs`
```

#### All-In-One Builds

All-In-One Builds pack all data files into the executable directly, and can be compiled by using the resources/pack.c program, 
to pack all data into an inline C file. It can be compiled like so:

```
gcc resources/pack.c -o pack-data && ./pack-data data/* data/*/* data/*/*/* > src/data.c && ./build.sh -DLITE_ALL_IN_ONE
```

This produces a standalone binary, that doesn't require any additional folders around it.

## Deviations from Lite XL

* Large build system replaced with a 70SLOC `build.sh` and `git` submodules.
* Removed volumunous documentation.
* Anchorpoints for emscripten.
* Added all-in-one build mode.
* Compatibilty with luajit.

## Deviation from Lite XL for Enhanced Builds

* Uses luajit.
* C-written tokenizer that tokenizes several orders of magnitude faster.
* Removed explicit UTF-8 support, as tokenizer handles it implicitly.

## Licenses

This project is free software; you can redistribute it and/or modify it under
the terms of the MIT license. Dependencies are licensed under various open
source licenses.  See [LICENSE] for details.

[lite-xl]:                    https://github.com/lite-xl/lite-xl
[LICENSE]:                    LICENSE
