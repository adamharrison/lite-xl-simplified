#include "api.h"

int luaopen_system(lua_State *L);
int luaopen_renderer(lua_State *L);
int luaopen_regex(lua_State *L);
int luaopen_process(lua_State *L);
int luaopen_dirmonitor(lua_State* L);
int luaopen_utf8extra(lua_State* L);

static const luaL_Reg libs[] = {
  { "system",     luaopen_system     },
  { "renderer",   luaopen_renderer   },
  { "regex",      luaopen_regex      },
  { "process",    luaopen_process    },
  { "dirmonitor", luaopen_dirmonitor },
  { "utf8extra",  luaopen_utf8extra  },
  { NULL, NULL }
};


void api_load_libs(lua_State *L) {
  for (int i = 0; libs[i].name; i++)
    luaL_requiref(L, libs[i].name, libs[i].func, 1);
  #if LUA_VERSION_NUM <= 501
  lua_newtable(L);
  lua_pushcfunction(L, bit32_extract);
  lua_setfield(L, -2, "extract");
  lua_pushcfunction(L, bit32_replace);
  lua_setfield(L, -2, "replace");
  lua_setglobal(L, "bit32");
  #endif
}

