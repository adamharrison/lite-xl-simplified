#include "api.h"
#include <string.h>

int luaopen_system(lua_State *L);
int luaopen_renderer(lua_State *L);
int luaopen_regex(lua_State *L);
int luaopen_process(lua_State *L);
int luaopen_dirmonitor(lua_State* L);
int luaopen_tokenizer(lua_State* L);

static const luaL_Reg libs[] = {
  { "system",     luaopen_system     },
  { "renderer",   luaopen_renderer   },
  { "regex",      luaopen_regex      },
  { "process",    luaopen_process    },
  { "dirmonitor", luaopen_dirmonitor },
  { "tokenizer",  luaopen_tokenizer  },
  { NULL, NULL }
};


void api_load_libs(lua_State *L) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  for (int i = 0; libs[i].name; i++) {
    libs[i].func(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, libs[i].name);
    lua_setglobal(L, libs[i].name);
  }
}

extern const char** internal_packed_files;
const char* api_retrieve_internal_file(const char* path) {
  for (int i = 0; internal_packed_files[i]; i += 2) {
    if (strcmp(path, internal_packed_files[i]) == 0)
      return internal_packed_files[i+1];
  }
  return NULL;
}
