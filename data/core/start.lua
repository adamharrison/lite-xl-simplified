-- this file is used by lite-xl to setup the Lua environment when starting
MOD_VERSION = "3"

SCALE = tonumber(os.getenv("LITE_SCALE") or os.getenv("GDK_SCALE") or os.getenv("QT_SCALE_FACTOR")) or SCALE
PATHSEP = package.config:sub(1, 1)

EXEDIR = EXEFILE:match("^(.+)[/\\][^/\\]+$")
if MACOS_RESOURCES then
  DATADIR = MACOS_RESOURCES
else
  local prefix = EXEDIR:match("^(.+)[/\\]bin$")
  DATADIR = prefix and (prefix .. PATHSEP .. 'share' .. PATHSEP .. 'lite-xl') or (EXEDIR .. PATHSEP .. 'data')
end
USERDIR = (system.get_file_info(EXEDIR .. PATHSEP .. 'user') and (EXEDIR .. PATHSEP .. 'user'))
       or os.getenv("LITE_USERDIR")
       or ((os.getenv("XDG_CONFIG_HOME") and os.getenv("XDG_CONFIG_HOME") .. PATHSEP .. "lite-xl"))
       or (HOME and (HOME .. PATHSEP .. '.config' .. PATHSEP .. 'lite-xl'))

package.path = DATADIR .. '/?.lua;'
package.path = DATADIR .. '/?/init.lua;' .. package.path
package.path = USERDIR .. '/?.lua;' .. package.path
package.path = USERDIR .. '/?/init.lua;' .. package.path

local suffix = PLATFORM == "Mac OS X" and 'lib' or (PLATFORM == "Windows" and 'dll' or 'so')
package.cpath =
  USERDIR .. '/?.' .. ARCH .. "." .. suffix .. ";" ..
  USERDIR .. '/?/init.' .. ARCH .. "." .. suffix .. ";" ..
  USERDIR .. '/?.' .. suffix .. ";" ..
  USERDIR .. '/?/init.' .. suffix .. ";" ..
  DATADIR .. '/?.' .. ARCH .. "." .. suffix .. ";" ..
  DATADIR .. '/?/init.' .. ARCH .. "." .. suffix .. ";" ..
  DATADIR .. '/?.' .. suffix .. ";" ..
  DATADIR .. '/?/init.' .. suffix .. ";"

package.native_plugins = {}
local searchers = package.searchers and "searchers" or "loaders"


local function iterate_paths(paths, modname, callback)
  local s = 1
  return function() 
    if s > #paths then return nil end
    local e = paths:find(";", s) or (#paths+1)
    local module_path = modname:gsub("%.", "/")
    local path = paths:sub(s, e - 1):gsub("?", module_path)
    s = e + 1
    return path
  end
end


loadstring = loadstring or load
package[searchers] = { function (modname) 
  for path in iterate_paths(package.path, modname) do
    local internal_file = system.get_internal_file(path)
    if internal_file then return function() return loadstring(internal_file, path)() end, path end
  end
  return nil
end, package[searchers][1], package[searchers][2], function(modname)
  for path in iterate_paths(package.path, modname) do
    if system.get_file_info(path) then return system.load_native_plugin, path end
  end
  return nil
end }

table.pack = table.pack or pack or function(...) return {...} end
table.unpack = table.unpack or unpack

-- For internal files, to let plugins loading work.
local old_io_lines = io.lines
io.lines = function(path)
  if type(path) == "string" and path:find(DATADIR, 1, true) == 1 then
    local internal_file = system.get_internal_file(path)
    if internal_file then return internal_file:gmatch("([^\n]*)\n?") end
  end
  return old_io_lines(path)
end

bit32 = bit32 or require "core.bit"

require "core.utf8string"

-- Because AppImages change the working directory before running the executable,
-- we need to change it back to the original one.
-- https://github.com/AppImage/AppImageKit/issues/172
-- https://github.com/AppImage/AppImageKit/pull/191
local appimage_owd = os.getenv("OWD")
if os.getenv("APPIMAGE") and appimage_owd then
  system.chdir(appimage_owd)
end

-- compatibility with lite-xl
string.ufind = string.find
