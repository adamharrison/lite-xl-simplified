# Lite IDE

A lightweight IDE written in Lua, adapted from [lite-xl-simplified]. Makes it easier to build
on different platforms if you're having trouble with meson.

Will always be rebased off upstream [lite-xl]; will never deviate far.

[Click here](https://adamharrison.github.io/lite-xl-simplified/) to try it out online!

## Quickstart

If you have a C compiler, and `git`:

```
git clone git@github.com:adamharrison/lite-xl-simplified.git --shallow-submodules \
  --recurse-submodules && cd lite-xl-simplified && git checkout ide && ./build.sh && ./lite-xl
````

## Build

See [lite-xl-simplified].

## Features

* Build-in Autocompletion via LSP (`clangd`)
* Automatic Build / Makefile Support
* GDB Debugger Support

## Licenses

This project is free software; you can redistribute it and/or modify it under
the terms of the MIT license. Dependencies are licensed under various open
source licenses.  See [LICENSE] for details.

[lite-xl-simplified]:                    https://github.com/lite-xl/lite-xl
[LICENSE]:                    LICENSE
