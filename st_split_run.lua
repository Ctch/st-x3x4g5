-- Neovim helper for this st fork.
--
-- Usage:
--   dofile("/home/faubo/temporary/downloads/my-st/st_split_run.lua").setup()
--
-- Or copy this file to ~/.config/nvim/lua/st_split_run.lua and use:
--   require("st_split_run").setup()

local M = {}

local b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

local function base64_encode(data)
  if vim.base64 and vim.base64.encode then
    return vim.base64.encode(data)
  end

  return ((data:gsub(".", function(char)
    local byte = char:byte()
    local bits = {}

    for i = 8, 1, -1 do
      bits[#bits + 1] = byte % 2 ^ i - byte % 2 ^ (i - 1) > 0 and "1" or "0"
    end

    return table.concat(bits)
  end) .. "0000"):gsub("%d%d%d?%d?%d?%d?", function(bits)
    if #bits < 6 then
      return ""
    end

    local n = 0
    for i = 1, 6 do
      if bits:sub(i, i) == "1" then
        n = n + 2 ^ (6 - i)
      end
    end

    return b64chars:sub(n + 1, n + 1)
  end) .. ({ "", "==", "=" })[#data % 3 + 1])
end

local function visual_text()
  local mode = vim.fn.mode()
  local start = vim.fn.getpos("v")
  local finish = vim.fn.getpos(".")
  local sr, sc = start[2], start[3]
  local er, ec = finish[2], finish[3]

  if sr > er or (sr == er and sc > ec) then
    sr, sc, er, ec = er, ec, sr, sc
  end

  local lines = vim.api.nvim_buf_get_lines(0, sr - 1, er, false)
  if #lines == 0 then
    return ""
  end

  if mode == "V" then
    return table.concat(lines, "\n")
  end

  if mode == "\022" then
    local left = math.min(sc, ec)
    local right = math.max(sc, ec)
    local out = {}

    for i, line in ipairs(lines) do
      out[i] = line:sub(left, right)
    end

    return table.concat(out, "\n")
  end

  if #lines == 1 then
    return lines[1]:sub(sc, ec)
  end

  lines[1] = lines[1]:sub(sc)
  lines[#lines] = lines[#lines]:sub(1, ec)

  return table.concat(lines, "\n")
end

local function send_osc(action, payload)
  payload = payload or ""
  io.stdout:write("\027]777;" .. action .. payload .. "\007")
  io.stdout:flush()
end

local function send_run(text)
  send_osc("split-run;", base64_encode(text))
end

local function send_clear()
  send_osc("split-clear")
end

local function current_line_text()
  return vim.api.nvim_get_current_line()
end

local function is_blank(text)
  return text:match("^%s*$") ~= nil
end

function M.send_visual()
  local text = visual_text()
  if text == "" then
    return
  end

  send_run(text)
  vim.api.nvim_feedkeys(vim.api.nvim_replace_termcodes("<Esc>", true, false, true), "n", false)
end

function M.send_current_line()
  local text = current_line_text()

  if is_blank(text) then
    send_clear()
    return
  end

  send_run(text)
end

function M.setup(opts)
  opts = opts or {}
  local key = opts.key or "<M-o>"

  if opts.map_normal ~= false then
    vim.keymap.set("n", key, M.send_current_line, {
      desc = "Send current line to st split",
      silent = true,
    })
  end

  vim.keymap.set("x", key, M.send_visual, {
    desc = "Send visual selection to st split",
    silent = true,
  })
end

return M
