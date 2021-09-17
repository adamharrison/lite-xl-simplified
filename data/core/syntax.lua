local common = require "core.common"

local syntax = {}
syntax.items = {}

local plain_text_syntax = { name = "Plain Text", patterns = {}, symbols = {} }


function syntax.add(t)
  table.insert(syntax.items, t)
end


local function find(string, field)
  local best_match = 0
  local best_syntax
  for i = #syntax.items, 1, -1 do
    local t = syntax.items[i]
    local s, e = common.match_pattern(string, t[field] or {})
    if s and e - s > best_match then
      best_match = e - s
      best_syntax = t
    end
  end
  return best_syntax
end

function syntax.get(filename, header)
  return find(common.basename(filename), "files")
      or (header and find(header, "headers"))
      or plain_text_syntax
end


return syntax
