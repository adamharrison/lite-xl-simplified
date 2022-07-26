local syntax = require "core.syntax"
local common = require "core.common"
local Tokenizer = require "tokenizer"

local tokenizer = {
  syntaxes = {}
}

local total_time_tokenizing = 0.0

function tokenizer.get(syntax_input)
  local syntax_object = type(syntax_input) == "table" and syntax_input or syntax.get(syntax_input)
  if not syntax_object then return end
  local native = tokenizer.syntaxes[syntax_object]
  if not native then 
    native = Tokenizer.new(syntax_object, tokenizer.get)
    tokenizer.syntaxes[syntax_object] = native
    syntax_object.tokenizer = native
  end
  return native
end

local total_lines = 0
function tokenizer.tokenize(syntax, text, state, quick)
  local start_time = system.get_time()
  total_lines = total_lines + 1
  local native = syntax.tokenizer or tokenizer.get(syntax)
  local res, state = native:tokenize(text, state or 0, quick)
  if res then
    local start = 1
    for i = 2, #res, 2 do
      local len = res[i]
      res[i] = text:sub(start, len + start - 1)
      start = len + start
    end
  end
  total_time_tokenizing = total_time_tokenizing + (system.get_time() - start_time)
  return res, state
end


local function iter(t, i)
  i = i + 2
  local type, text = t[i], t[i+1]
  if type then
    return i, type, text
  end
end

function tokenizer.each_token(t)
  return iter, t, -1
end

return tokenizer
