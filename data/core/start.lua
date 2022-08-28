-- this file is used by lite-xl to setup the Lua environment when starting
MOD_VERSION = "3"

SCALE = tonumber(os.getenv("LITE_SCALE") or os.getenv("GDK_SCALE") or os.getenv("QT_SCALE_FACTOR")) or SCALE
PATHSEP = package.config:sub(1, 1)

EXEDIR = EXEFILE:match("^(.+)[/\\][^/\\]+$")
DATADIR = "%INTERNAL%/data"
USERDIR = (system.get_file_info(EXEDIR .. '/user') and (EXEDIR .. '/user'))
       or ((os.getenv("XDG_CONFIG_HOME") and os.getenv("XDG_CONFIG_HOME") .. "/lite-xl"))
       or (HOME and (HOME .. '/.config/lite-xl'))

package.path = DATADIR .. '/?.lua;'
package.path = DATADIR .. '/?/init.lua;' .. package.path
package.path = USERDIR .. '/?.lua;' .. package.path
package.path = USERDIR .. '/?/init.lua;' .. package.path

local dynamic_suffix = PLATFORM == "Mac OS X" and 'lib' or (PLATFORM == "Windows" and 'dll' or 'so')
package.cpath = DATADIR .. '/?.' .. dynamic_suffix .. ";" .. USERDIR .. '/?.' .. dynamic_suffix
package.native_plugins = {}
local searchers = package.searchers and "searchers" or "loaders"

package[searchers] = { function (modname) 
  local s = 1
  while s < #package.path do
    local e = package.path:find(";", s) or (#package.path+1)
    local module_path = modname:gsub("%.", PATHSEP)
    local path = package.path:sub(s, e - 1):gsub("?", module_path)
    local internal_file = system.get_internal_file(path)
    if internal_file then
      return function(modname)
        local i = 0
        local func, err = load(function() 
          if i == 0 then 
            i = 1 
            return internal_file
          end
          return nil
        end, path)
        if err then error(err) end
        return func()
      end, path
    end
    s = e + 1
  end
  return nil
end, package[searchers][1], package[searchers][2], function(modname)
  local s, e = 1, 0
  while e < #package.cpath do
    e = package.cpath:find(";", s) or #package.cpath
    local path = package.cpath:sub(s, e):gsub("?", modname)
    if system.get_file_info(path) then
      return system.load_native_plugin, path
    end
  end
  return nil
end }


local old_io_open = io.open
io.open = function(path, mode)
  if type(path) == "string" and path:find(DATADIR, 1, true) == 1 then
    local internal_file = system.get_internal_file(path)
    if internal_file then
      return {
        offset = 0,
        str = system.get_internal_file(path),
        close = function()  end,
        lines = function(self)
          if self.offset > #self.str then return nil end
          local lines = { }
          local ns, ne = self.str:find("\r?\n", self.offset)
          if not ns then 
            return function() 
              local str = self.str:sub(self.offset) 
              self.offset = #self.str + 1
              return str
            end 
          end
          return function() 
            local str = self.str:sub(self.offset, ns - 1)
            self.offset = ne + 1
            return str
          end
        end
      }
    end
  end
  return old_io_open(path, mode)
end

table.pack = table.pack or pack or function(...) return {...} end
table.unpack = table.unpack or unpack

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
