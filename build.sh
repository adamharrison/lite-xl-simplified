#!/bin/bash

: ${CC=gcc}
: ${AR=ar}
CFLAGS=" $CFLAGS $@ -Isrc -fno-strict-aliasing"
LDFLAGS=" $LDFLAGS -lm -static-libgcc"
if [[ $OSTYPE == 'msys' ]]; then
  : ${BIN=lite-xl.exe}
  : ${LNAME=liblite.lib}
else
  : ${BIN=lite-xl}
  : ${LNAME=liblite.a}
fi
: ${JOBS=12}

[[ "$@" == "clean" ]] && rm -rf lib/SDL/build liblite.* *.o $BIN && exit 0

# Compile SDL separately, because it's the only complicated module.
if [[ "$LDFLAGS" != *"-lSDL"* ]]; then
  [[ ! -e "lib/SDL/include" ]] && echo "Make sure you've cloned your submodules. (git submodule update --init --depth=1)" && exit -1
  [[ ! -e "lib/SDL/build" ]] && cd lib/SDL && mkdir -p build && cd build && CC=$CC ../configure $SDL_CONFIGURE --disable-audio --disable-joystick --disable-haptic && make -j $JOBS && cd ../../..
  LDFLAGS=" $LDFLAGS -Llib/SDL/build/build/.libs -l:libSDL2.a"
  CFLAGS=" $CFLAGS -Ilib/SDL/include"
fi
# Supporting library. Only compile bits that we're not linking explicitly against, allowing for system linking of libraries.
if [[ "$LDFLAGS" != *"-llua"* ]]; then
  CFLAGS="$CFLAGS -Ilib/lua"
  LLSRCS="$LLSRCS lib/lua/l*.c"
fi
if [[ "$LDFLAGS" != *"-lpcre"* ]]; then
  cp -f lib/pcre2/src/config.h.generic lib/pcre2/src/config.h
  cp -f lib/pcre2/src/pcre2.h.generic lib/pcre2/src/pcre2.h
  cp -f lib/pcre2/src/pcre2_chartables.c.dist lib/pcre2/src/pcre2_chartables.c
  CFLAGS="$CFLAGS -Ilib/pcre2/src -DPCRE2_STATIC"
  LLFLAGS="$LLFLAGS -Ilib/pcre2/src -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8"
  LLSRCS="$LLSRCS lib/pcre2/src/pcre2_substitute.c lib/pcre2/src/pcre2_convert.c lib/pcre2/src/pcre2_dfa_match.c lib/pcre2/src/pcre2_find_bracket.c\
    lib/pcre2/src/pcre2_auto_possess.c lib/pcre2/src/pcre2_substring.c lib/pcre2/src/pcre2_match_data.c lib/pcre2/src/pcre2_xclass.c lib/pcre2/src/pcre2_study.c\
    lib/pcre2/src/pcre2_ucd.c lib/pcre2/src/pcre2_maketables.c lib/pcre2/src/pcre2_compile.c lib/pcre2/src/pcre2_match.c lib/pcre2/src/pcre2_context.c\
    lib/pcre2/src/pcre2_string_utils.c lib/pcre2/src/pcre2_tables.c lib/pcre2/src/pcre2_serialize.c lib/pcre2/src/pcre2_ord2utf.c lib/pcre2/src/pcre2_error.c\
    lib/pcre2/src/pcre2_config.c lib/pcre2/src/pcre2_chartables.c lib/pcre2/src/pcre2_newline.c lib/pcre2/src/pcre2_jit_compile.c lib/pcre2/src/pcre2_fuzzsupport.c\
    lib/pcre2/src/pcre2_valid_utf.c lib/pcre2/src/pcre2_extuni.c lib/pcre2/src/pcre2_script_run.c lib/pcre2/src/pcre2_pattern_info.c"
fi
if [[ "$LDFLAGS" != *"-lfreetype"* ]]; then
  echo "FT_USE_MODULE( FT_Module_Class, autofit_module_class ) FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class ) FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class ) \
    FT_USE_MODULE( FT_Module_Class, psnames_module_class ) FT_USE_MODULE( FT_Module_Class, pshinter_module_class ) FT_USE_MODULE( FT_Module_Class, sfnt_module_class )\
    FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class ) FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )" > lib/freetype/include/freetype/config/ftmodule.h
  CFLAGS="$CFLAGS -Ilib/freetype/include"
  LLFLAGS="$LLFLAGS -Ilib/freetype/include -DFT2_BUILD_LIBRARY"
  LLSRCS=" $LLSRCS lib/freetype/src/base/ftsystem.c lib/freetype/src/base/ftinit.c lib/freetype/src/base/ftdebug.c lib/freetype/src/base/ftbase.c \
    lib/freetype/src/base/ftbbox.c lib/freetype/src/base/ftglyph.c lib/freetype/src/sfnt/sfnt.c lib/freetype/src/truetype/truetype.c lib/freetype/src/raster/raster.c \
    lib/freetype/src/smooth/smooth.c lib/freetype/src/autofit/autofit.c lib/freetype/src/psnames/psnames.c lib/freetype/src/pshinter/pshinter.c lib/freetype/src/cff/cff.c \
    lib/freetype/src/gzip/ftgzip.c lib/freetype/src/base/ftbitmap.c"
fi
if [ ! -f $LNAME ] && { [ ! -z "$LLSRCS" ]; }; then
  echo "Building $LNAME... (Can take a moment, but only needs to be done once)"
  for SRC in $LLSRCS; do 
    ((i=i%JOBS)); ((i++==0)) && wait # Parallelize the build.
    $CC -O3 -c $SRC $LLFLAGS &
  done
  wait && $AR -r -s $LNAME *.o && rm -f *.o
fi
[[  ! -z "$LLSRCS" ]] && LDFLAGS="-L. -llite"

# Main executable; set to -O3 if O or debugging not specified.
[[ " $CFLAGS " != *" -O"* ]] && [[ " $CFLAGS " != *" -g "* ]] && CFLAGS="$CFLAGS -O3"
SRCS="src/*.c src/api/*.c"
if [[ $OSTYPE == 'darwin'* ]]; then
  CFLAGS="$CFLAGS -DLITE_USE_SDL_RENDERER"
  LDFLAGS="$LDFLAGS -Framework CoreServices -Framework Foundation"
  SRCS=$SRCS src/*.m
fi
[[ $OSTYPE != 'msys'* ]] && LDFLAGS=" $LDFLAGS -ldl -pthread"

echo "Building $BIN..."
for SRC in $SRCS; do 
  ((i=i%JOBS)); ((i++==0)) && wait # Parallelize the build.
  $CC $SRC -c $CFLAGS -o $SRC.o &
done
wait && $CC src/*.o src/api/*.o -o $BIN $LDFLAGS $@ && rm -f src/*.o src/api/*.o
echo "Done."
