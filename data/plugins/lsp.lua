-- mod-version:3 -- lite-xl 2.1
local core = require "core"
local common = require "core.common"
local Doc = require "core.doc"
local DocView = require "core.docview"
local json = require "plugins.json"
local command = require "core.command"
local keymap = require "core.keymap"
local config = require "core.config"
local ContextMenu = require "core.contextmenu"
local menu = require "plugins.contextmenu"

local lsp = common.merge({
  process = nil,
  awaiting = {},
  server = "clangd",
  verbose = true,
  initialized = false
}, config.plugins.lsp)
lsp.process = process.start({ "clangd", "--log=verbose", "--offset-encoding=utf-8" }, { stderr = lsp.verbose and process.REDIRECT_PARENT or process.REDIRECT_DISCARD })

-- Retrieves at least one command.
local response_buffer = ""
function lsp.receive_command()
  local response_start = 1
  response_buffer = response_buffer .. lsp.process:read_stdout(16777216)
  if response_buffer == "" then return end
  while true do
    local headers = {}
    local _, header_boundary = response_buffer:find("\r\n\r\n")
    if not header_boundary then return end
    local header_chunk = response_buffer:sub(1, header_boundary - 2)
    local offset = 1
    while true do
      local s, e, header, value = header_chunk:find("([%w%-]+)%s*:%s*(.+)\r\n", offset)
      if not s then break end
      headers[header] = value
      offset = e + 1
    end
    if not headers['Content-Length'] then return end
    if header_boundary + 1 + headers['Content-Length'] < #response_buffer then return end
    local body = response_buffer:sub(header_boundary + 1, header_boundary + 1 + headers["Content-Length"])
    local res = json.decode(body)
    if res.error then
      core.error(res.error.message)
    elseif res.id and lsp.awaiting[res.id] then
      lsp.awaiting[res.id](res)
      lsp.awaiting[res.id] = nil
      local empty = true
      for k,v in pairs(lsp.awaiting) do if v ~= nil then empty = false end end
      if empty then lsp.awaiting = {} end
    end
    response_buffer = response_buffer:sub(header_boundary + 1 + headers["Content-Length"])
  end
end

local command_idx = 1
function lsp.send_command(type, method, params, on_done)
  local request = {
    jsonrpc = "2.0",
    method = method,
    params = params
  }
  if type ~= "NOTIFICATION" then request.id = command_idx end
  local body = json.encode(request)
  lsp.process:write("Content-Length: " .. #body .. "\r\n\r\n" .. body)
  if on_done then lsp.awaiting[command_idx] = on_done end
  command_idx = command_idx + 1
end

lsp.send_command("REQUEST", "initialize", {
  processId = system.get_process_id(),
  clientInfo = { name = "LSP Plugin for lite-xl", verison = "1.0" },
  rootPath = core.project_dir,
  clientCapabilities = { textDocument = { declaration = {  }, synchronization = { } }, general = { positionEncodings = { "utf-8", "utf-16" } } }
}, function() 
  lsp.initialized = true
  for _, doc in ipairs(core.docs) do
    lsp.open_file(doc)
  end
  local old_try_close = DocView.try_close
  function DocView:try_close(on_close)
    old_try_close(self, function() 
      if self.doc.abs_filename then lsp.close_file(self.doc) end
      on_close()
    end)
  end
  local old_docview_new = DocView.new
  function DocView:new(doc)
    if doc and doc.abs_filename then 
      lsp.open_file(doc) 
    end
    return old_docview_new(self, doc)
  end
end)

core.add_thread(function()
  while true do
    lsp.receive_command()
    coroutine.yield(0.01)
  end
end)


local function path_from_uri(uri)
  return uri:gsub("^file://", "")
end
local function uri_from_path(path)
  return "file://" .. path
end
local function table_from_range(range)
  return range.start.line + 1, range.start.character + 1, range["end"].line + 1, range["end"].character + 1
end

local version = 1
function lsp.open_file(doc)
  local contents = ''
  for _, line in ipairs(doc.lines) do
    contents = contents .. line
  end
  if doc.syntax and doc.syntax.name == "C++" or doc.syntax.name == "C" then
    lsp.send_command("NOTIFICATION", "textDocument/didOpen", { textDocument = { uri = uri_from_path(doc.abs_filename), version = version, text = contents, languageId = "cpp" } })
  end
end

function lsp.close_file(doc)
  if doc.syntax and doc.syntax.name == "C++" or doc.syntax.name == "C" then
    lsp.send_command("NOTIFICATION", "textDocument/didClose", { textDocument = { uri = uri_from_path(doc.abs_filename) } })
  end
end

local version = 1
function lsp.change_file(path, changes)
  lsp.send_command("NOTIFICATION", "textDocument/didChange", { textDocument = { uri = uri_from_path(path), version = version }, contentChanges = changes })
  version = version + 1
end

function lsp.goto_generic(path, type, line, col, on_done)
  lsp.send_command("REQUEST", "textDocument/" .. type, { textDocument = { uri = uri_from_path(path) }, position = { line = line, character = col } }, function(res)
    if #res.result > 0 then on_done(path_from_uri(res.result[1].uri), table_from_range(res.result[1].range)) end
  end)
end

function lsp.goto_definition(path, line, col, on_done) lsp.goto_generic(path, "definition", line, col, on_done) end
function lsp.goto_declaration(path, line, col, on_done) lsp.goto_generic(path, "declaration", line, col, on_done) end
function lsp.goto_implementation(path, line, col, on_done) lsp.goto_generic(path, "implementation", line, col, on_done) end
-- function lsp.completion(path, line, col, on_done)
--   lsp.send_command("textDocument/completion", {
--     textDocument = { uri = uri_from_path(path) },
--     position = { line = line, character = cool },
--     context = { triggerKind = 1 } 
--   }, function(res)
--     res.result.
--   end)
-- end
-- function lsp.get_signature_information(path, line, col, on_done)
--   lsp.send_command("textDocument/signatureHelp", {
--     textDocument = { uri = uri_from_path(path) },
--     position = { line = line, character = cool },
--     context = { triggerKind = 1 }
--   }, function(res) 
    
--   end)
-- end


local function jump_to_file(file, line, col)
  local view = nil
  if not core.active_view or not core.active_view.doc or core.active_view.doc.abs_filename ~= file then
    view = core.root_view:open_doc(core.open_doc(file))
  else
    view = core.active_view
  end
  if view and line then
    view:scroll_to_make_visible(math.max(1, line - 10), true)
    view.doc:set_selection(line, col, line, col)
  end
end

local function lsp_goto(type)
  local doc = core.active_view.doc
  local line, col = doc:get_selection(true) 
  if doc and doc.abs_filename then lsp["goto_" .. type](doc.abs_filename, line - 1, col - 1, jump_to_file) end
end

command.add(DocView, { 
  ["lsp:goto-definition"] = function() lsp_goto("definition") end,
  ["lsp:goto-declaration"] = function() lsp_goto("declaration") end,
  ["lsp:goto-implementation"] = function() lsp_goto("implementation") end
});

menu:register("core.docview", {
  ContextMenu.DIVIDER,
  { text = "Goto Definition",             command = "lsp:goto-definition"     },
  { text = "Goto Declaration",            command = "lsp:goto-declaration"    },
  { text = "Goto Implementation",         command = "lsp:goto-implementation" }
})

keymap.add({
  ["ctrl+f11"] = "lsp:goto-definition"
});

return lsp



