-- mod-version:3 -- lite-xl 2.1
local core = require "core"
local Doc = require "core.doc"
local DocView = require "core.docview"
local json = require "plugins.json"

local lsp = {
  process = nil
  awaiting = {},
  server = "clangd"
}
lsp.process = Process.new("clangd", { stderr = Process.REDIRECT_PARENT })

local command_idx = 1
function lsp.send_command(method, params, on_done)
  local body = json.encode({
    jsonrpc = "2.0",
    id = command_idx,
    method = cmd,
    params = params
  })
  lsp.process.write("Content-Length: " .. #body .. "\r\n\r\n" .. body)
  if on_done then awaiting[command_idx] = on_done end
  command_idx = command_idx + 1
end

function lsp.receive_command()
  local responses = Process.read(32)
  
end

local function path_from_uri(uri)
  return path:gsub("^file:///", "")
end
local function uri_from_path(path)
  return "file:///" .. path
end
local function table_from_range(range)
  return range.start.line, range.start.character, result.range.end.line, result.range.end.character
end

function lsp.open_file(path)
  lsp.send_command("textDocument/didOpen", { textDocument = { uri = uri_from_path(path) } })
end

function lsp.close_file(path)
  lsp.send_command("textDocument/didClose", { textDocument = { uri = uri_from_path(path) } })
end

local version = 1
function lsp.change_file(path, changes)
  lsp.send_command("textDocument/didChange", { textDocument = { uri = uri_from_path(path), version = version }, contentChanges = changes })
  version = version + 1
end

function lsp.goto_generic(path, type, line, col, on_done) 
  lsp.send_command("textDocument/" .. type, { textDocument = { uri = uri_from_path(path) }, position = { line = line, character = col } }, function(res)
    on_done(uri_from_path(res.result.uri), table_from_range(res.result.range))
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

local old_set_active_view = core.set_active_view
function core.set_active_view(view)
  old_set_active_view(view)
  if (view.doc and view.doc.abs_filename) then
    
  end
end


command.add({ 

});


return lsp



