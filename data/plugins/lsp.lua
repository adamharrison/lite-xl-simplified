-- mod-version:3 -- lite-xl 2.1 --priority:20
local core = require "core"
local common = require "core.common"
local Doc = require "core.doc"
local DocView = require "core.docview"
local json = require "plugins.json"
local command = require "core.command"
local keymap = require "core.keymap"
local config = require "core.config"
local ContextMenu = require "core.contextmenu"
local RootView = require "core.rootview"
local menu = require "plugins.contextmenu"
local style = require "core.style"
local tokenizer = require "core.tokenizer"

local lsp = common.merge({
  -- How big a symbol needs to be before we autocomplete on it.
  min_symbol_length = 2,
  -- How many items we show in the dialog before it turns to scrolling.
  max_suggested_items = 5,
  -- The LSP server to use.
  server = "clangd",
  -- Whether or not to turn on verbose for that server.
  verbose = true,
  symbol_pattern = config.symbol_pattern,
  ---------------------------
  process = nil,
  awaiting = {},
  initialized = false,
  change_queue = { },
  signature_info = nil,
  completion_info = nil,
}, config.plugins.lsp)
lsp.process = process.start({ lsp.server, "--log=verbose", "--offset-encoding=utf-8" }, { env = { FLAGS = "-I/usr/include/SDL2" }, stderr = lsp.verbose and process.REDIRECT_PARENT or process.REDIRECT_DISCARD })
config.plugins.autocomplete = false

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
    if #response_buffer < header_boundary + headers['Content-Length'] then return end
    local body = response_buffer:sub(header_boundary + 1, header_boundary + headers["Content-Length"])
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

function lsp.queue_command(...)
  lsp.flush_change_queue()
  lsp.send_command(...)
end

local function lsp_is_valid_doc(doc)
  return doc and doc.abs_filename and doc.syntax and doc.syntax.name == "C++" or doc.syntax.name == "C"
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
      if lsp_is_valid_doc(self.doc) then lsp.close_file(self.doc) end
      on_close()
    end)
  end
  local old_docview_new = DocView.new
  function DocView:new(doc)
    if lsp_is_valid_doc(doc) then lsp.open_file(doc) end
    return old_docview_new(self, doc)
  end
  local handling = false
  local old_text_input = DocView.on_text_input
  function DocView:on_text_input(text)
    handling = true
    local res = { old_text_input(self, text) }
    handling = false
    if lsp_is_valid_doc(self.doc) then lsp.text_input(self, text) end
    return table.unpack(res)
  end
  local old_doc_raw_insert = Doc.raw_insert
  function Doc:raw_insert(line, col, text, ...)
    if lsp_is_valid_doc(self) then lsp.insert_file(self, line - 1, col - 1, text) end
    return old_doc_raw_insert(self, line, col, text, ...)
  end
  local old_doc_raw_remove = Doc.raw_remove
  function Doc:raw_remove(line1, col1, line2, col2, ...)
    if lsp_is_valid_doc(self) then lsp.remove_file(self, line1 - 1, col1 - 1, line2 - 1, col2 - 1) end
    return old_doc_raw_remove(self, line1, col1, line2, col2, ...)
  end
  local old_doc_set_selections = Doc.set_selections
  function Doc:set_selections(idx, ...)
    local line, col = self:get_selection(true)
    local res = { old_doc_set_selections(self, idx, ...) }
    if idx == 1 and not handling and lsp_is_valid_doc(self) and core.active_view and core.active_view.doc == self 
  then 
      lsp.set_selections(core.active_view, ...) 
    end
    return table.unpack(res)
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
  lsp.send_command("NOTIFICATION", "textDocument/didOpen", { textDocument = { uri = uri_from_path(doc.abs_filename), version = version, text = contents, languageId = "cpp" } })
end

function lsp.close_file(doc)
  lsp.send_command("NOTIFICATION", "textDocument/didClose", { textDocument = { uri = uri_from_path(doc.abs_filename) } })
end

function lsp.change_file(path, changes)
  lsp.send_command("NOTIFICATION", "textDocument/didChange", { textDocument = { uri = uri_from_path(path), version = version }, contentChanges = changes })
  version = version + 1
end

local change_queue_size = 0
function lsp.flush_change_queue()
  for path, changes in pairs(lsp.change_queue) do
    lsp.change_file(path, changes)
  end
  lsp.change_queue = { }
  change_queue_size = 0
end

function lsp.push_change_queue(doc, change)
  if not lsp.change_queue[doc.abs_filename] then lsp.change_queue[doc.abs_filename] = { } end
  table.insert(lsp.change_queue[doc.abs_filename], change)
  change_queue_size = change_queue_size + 1
  if change_queue_size > 30 then lsp.flush_change_queue() end
end

function lsp.insert_file(doc, line, col, text)
  lsp.push_change_queue(doc, { range = { start = { line = line, character = col }, ["end"] = { line = line, character = col } }, text = text })
end

function lsp.remove_file(doc, line1, col1, line2, col2)
  lsp.push_change_queue(doc, { range = { start = { line = line1, character = col1 }, ["end"] = { line = line2, character = col2 } }, text = "" })
end

local function lsp_get_generic(path, type, line, col, on_done)
  lsp.queue_command("REQUEST", "textDocument/" .. type, { textDocument = { uri = uri_from_path(path) }, position = { line = line, character = col } }, function(res)
    if #res.result > 0 then on_done(path_from_uri(res.result[1].uri), table_from_range(res.result[1].range)) end
  end)
end

function lsp.get_definition(path, line, col, on_done) lsp_get_generic(path, "definition", line, col, on_done) end
function lsp.get_declaration(path, line, col, on_done) lsp_get_generic(path, "declaration", line, col, on_done) end
function lsp.get_implementation(path, line, col, on_done) lsp_get_generic(path, "implementation", line, col, on_done) end
function lsp.get_completion(path, line, col, on_done)
  lsp.queue_command("REQUEST", "textDocument/completion", {
    textDocument = { uri = uri_from_path(path) },
    position = { line = line, character = col },
    context = { triggerKind = 1 } 
  }, function(res)
    on_done(res.result.items)
  end)
end
function lsp.get_signature_information(path, line, col, on_done)
  lsp.queue_command("REQUEST", "textDocument/signatureHelp", {
    textDocument = { uri = uri_from_path(path) },
    position = { line = line, character = col },
    context = { triggerKind = 1 }
  }, function(res) 
    on_done(res.result.signatures, res.result.activeSignature, res.result.activeParameter)
  end)
end


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

local function lsp_goto(type, dv, x, y)
  dv = dv or core.active_view
  if lsp_is_valid_doc(dv.doc) then
    local line1, col1, line2, col2 = dv.doc:get_selection(true) 
    local line, col
    if not x or not y or line1 ~= line2 or col1 ~= col2 then
      line, col = line1, col1
    else
      line, col = dv:resolve_screen_position(x, y)
    end
    lsp["get_" .. type](dv.doc.abs_filename, line - 1, col - 1, jump_to_file)
  end
end

local function lsp_show_signature(dv, on_done)
  dv = dv or core.active_view
  if lsp_is_valid_doc(dv.doc) then
    local line, col = dv.doc:get_selection(true) 
    lsp.get_signature_information(dv.doc.abs_filename, line - 1, col - 1, function(signatures, active_signature, active_parameter)
      if #signatures > 0 then
        local x, y
        if lsp.signature_info then
          x, y = lsp.signature_info.position.x, lsp.signature_info.position.y
        else
          local s, e = dv.doc.lines[line]:reverse():find(signatures[1].label:gsub("%W.*", ""):reverse())
          x, y = dv:get_line_screen_position(line, e and (#dv.doc.lines[line] - e + 1) or col)
          x = x + dv.scroll.x - style.padding.x
          y = y + dv:get_line_height() + dv.scroll.y 
        end
        local items = { }
        for i, v in ipairs(signatures) do 
          local label = v.label:gsub("^%s+", "")
          local item = { label = label, tokens = select(1, tokenizer.tokenize(dv.doc.syntax, label)), selected = {} }
          -- bold active parameter
          if i == active_signature + 1 then
            local s, e = nil, 0
            for j, parameter in ipairs(v.parameters) do
              s, e = label:find(parameter.label, e + 1, true)
              if j == active_parameter + 1 then break end
            end
            local idx = 1
            if s then 
              for j = 2, #item.tokens, 2 do
                table.insert(item.selected, idx >= s and idx <= e + 1)
                idx = idx + #item.tokens[j]
              end
            end
          end
          table.insert(items, item)
        end
        lsp.signature_info = { dv = dv, position = { x = x, y = y }, tokenize = true, items = items, offset = 1 }
      else
        lsp.signature_info = nil
      end
      if on_done then on_done(signatures, active_signature, active_parameter) end
    end)
  end
end

local function lsp_show_autocomplete(dv, on_done)
  if lsp_is_valid_doc(dv.doc) then
    local line, col = dv.doc:get_selection(true) 
    lsp.get_completion(dv.doc.abs_filename, line - 1, col - 1, function(completions)
      if #completions > 0 then
        local x, y
        if lsp.completion_info then
          x, y = lsp.completion_info.position.x, lsp.completion_info.position.y
        else
          local s, e = dv.doc.lines[line]:sub(1, col - 1):reverse():find(lsp.symbol_pattern)
          x, y = dv:get_line_screen_position(line, col - (e or 0))
          x = x + dv.scroll.x - style.padding.x
          y = y + dv:get_line_height() + dv.scroll.y 
        end
        local items = { }
        for i, v in ipairs(completions) do 
          local label = v.filterText:gsub("^%s+", "")
          local detail = v.detail and v.detail:gsub("\n.*", "")
          if detail and detail:find("%S") then label = label .. ": " .. detail end
          table.insert(items, { label = label, tokens = select(1, tokenizer.tokenize(dv.doc.syntax, label)), textEdit = v.textEdit, selected = {} }) 
        end
        lsp.completion_info = { dv = dv, position = { x = x, y = y }, items = items, tokenize = true, selected = 1, offset = 1 }
      else
        lsp.completion_info = nil
      end
      if on_done then on_done(completions) end
    end)
  end
end

local function lsp_check_signature(dv, on_done)
  local line, col = dv.doc:get_selection(true) 
  local prefix = dv.doc.lines[line]:sub(1, col - 1)
  local _, count_open = prefix:gsub("%(", "")
  local _, count_closed = prefix:gsub("%)", "")
  if count_open > count_closed then
    lsp_show_signature(dv, on_done)
  else
    lsp.signature_info = nil
    if on_done then on_done(nil) end
  end
end

local function lsp_check_completion(dv, on_done)
  local line, col = dv.doc:get_selection(true) 
  local prefix = dv.doc.lines[line]:sub(1, col - 1):reverse()
  local trigger_completion = prefix:sub(1, 1) == "." or prefix:sub(1, 2) == "->"
  if not trigger_completion then
    local _, e = prefix:find("^" .. lsp.symbol_pattern)
    local _, ne = prefix:find("%.")
    if not ne then prefix:find(">-") end
    trigger_completion = (e and e >= lsp.min_symbol_length) or (ne and ne <= lsp.min_symbol_length)
  end
  if trigger_completion then
    lsp_show_autocomplete(dv, on_done)
  else
    lsp.completion_info = nil
    if on_done then on_done(nil) end
  end
end

local function lsp_update_completion(dv, text)
  -- count open parentheses on this line; if we have an open one, then show signature until we close up, and refresh it if we type parentheses or comma.
  local line, col = dv.doc:get_selection(true) 
  lsp_check_completion(dv, function(completion)
    if not completion and ((text and text:find("[%)%(,]")) or not lsp.signature_info) then
      lsp_check_signature(dv)
    end
  end)
end

function lsp.text_input(dv, text) lsp_update_completion(dv, text) end
function lsp.set_selections(dv, idx, line1, col1, line2, col2) 
  if line1 ~= line2 or col1 ~= col2 then 
    lsp.signature_info = nil
    lsp.completion_info = nil
  else
    lsp_update_completion(dv) 
  end
end

local function draw_boxes(av)
  local bi = lsp.completion_info or lsp.signature_info
  if bi and av == bi.dv then
    local ox, oy = bi.position.x - bi.dv.scroll.x, bi.position.y - bi.dv.scroll.y
    local lh = style.code_font:get_height()
    local height = (style.padding.y * 2 + lh) * math.min(#bi.items, lsp.max_suggested_items)
    local y, w = oy, 0
    for i, v in ipairs(bi.items) do
      w = math.max(w, style.code_font:get_width(v.label))
    end
    w = w + style.padding.x * 2
    renderer.draw_rect(ox, oy, w, height, style.background3)
    local default_font = bi.dv:get_font()
    local first, last = bi.offset, bi.offset + lsp.max_suggested_items - 1
    for i, v in ipairs(bi.items) do
      if i >= first and i <= last then
        if i == bi.selected then
          renderer.draw_rect(ox, y, w, lh + style.padding.y * 2, style.selection)
        end
        y = y + style.padding.y
        local x = ox + style.padding.x
        for idx, type, text in tokenizer.each_token(v.tokens) do
          local color = style.syntax[type]
          local font = style.syntax_fonts[type] or default_font
          if v.selected[((idx - 1) / 2) + 1] then 
            color = style.accent
          end
          x = renderer.draw_text(font, text, x, y, color)
        end
        y = y + lh + style.padding.y
      end
    end
  end
end

local old_draw = RootView.draw
function RootView:draw(...)
  old_draw(self, ...)
  if core.active_view then
    self:defer_draw(draw_boxes, core.active_view)
  end
end

command.add(function()
  return core.active_view and lsp_is_valid_doc(core.active_view.doc), core.active_view, core.active_view
end, { 
  ["lsp:goto-definition"] = function(dv, x, y) lsp_goto("definition", dv, x, y) end,
  ["lsp:goto-declaration"] = function(dv, x, y) lsp_goto("declaration", dv, x, y) end,
  ["lsp:goto-implementation"] = function(dv, x ,y) lsp_goto("implementation", dv, x, y) end,
  ["lsp:show-completions"] = function(dv) lsp_show_completions(dv) end,
  ["lsp:show-signature"] = function(dv) lsp_show_signature(dv) end,
  ["lsp:contextual-complete"] = function(dv) 
    local line, col = dv.doc:get_selection(true)
    if dv.doc.lines[line]:sub(1, col - 1):reverse():find("^%s*[,%(]") then
      lsp_show_signature(dv)
    else
      lsp_show_autocomplete(dv, function(completions)
        if #completions == 0 then
          lsp_show_signature(dv)
        end
      end)
    end
  end
})
local function snap_completion_offset()
  if lsp.completion_info.selected < lsp.completion_info.offset then
    lsp.completion_info.offset = lsp.completion_info.selected
  elseif lsp.completion_info.selected >= lsp.completion_info.offset + lsp.max_suggested_items then
    lsp.completion_info.offset = math.max(lsp.completion_info.selected - lsp.max_suggested_items + 1, 1)
  end
end
command.add(function() 
  return lsp.completion_info
end, {
  ["lsp:completion-up"] = function()
    lsp.completion_info.selected = ((lsp.completion_info.selected - 2) % #lsp.completion_info.items) + 1
    snap_completion_offset()
  end,
  ["lsp:completion-down"] = function() 
    lsp.completion_info.selected = (lsp.completion_info.selected % #lsp.completion_info.items) + 1
    snap_completion_offset()
  end,
  ["lsp:completion-select"] = function()  
    local item = lsp.completion_info.items[lsp.completion_info.selected]
    if item.textEdit then
      local dv = lsp.completion_info.dv
      dv.doc:remove(item.textEdit.range.start.line + 1, item.textEdit.range.start.character + 1, item.textEdit.range['end'].line + 1, item.textEdit.range['end'].character + 1)
      dv.doc:insert(item.textEdit.range.start.line + 1, item.textEdit.range.start.character + 1, item.textEdit.newText)
      dv.doc:set_selection(item.textEdit.range.start.line + 1, item.textEdit.range.start.character + 1 + #item.textEdit.newText)
      lsp.completion_info = nil
    end
  end,
})
command.add(function() 
  return lsp.completion_info or lsp.signature_info
end, {
  ["lsp:contextual-cancel"] = function()
    if lsp.completion_info then 
      lsp.completion_info = nil 
    else
      lsp.signature_info = nil
    end
  end
})

menu:register("core.docview", {
  ContextMenu.DIVIDER,
  { text = "Goto Definition",             command = "lsp:goto-definition"     },
  { text = "Goto Declaration",            command = "lsp:goto-declaration"    },
  { text = "Goto Implementation",         command = "lsp:goto-implementation" }
})

keymap.add({
  ["ctrl+f11"] = "lsp:goto-definition",
  ["ctrl+space"] = "lsp:contextual-complete",
  ["up"] = "lsp:completion-up",
  ["down"] = "lsp:completion-down",
  ["tab"] = "lsp:completion-select",
  ["escape"] = "lsp:contextual-cancel"
});

return lsp
