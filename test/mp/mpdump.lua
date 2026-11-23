-- mpdump.lua: run by test/mp/mamerun.sh in a headless MAME. Each frame (at
-- the VBLANK start, where MAME's frame_done runs) it applies the inputs the
-- script file gives for that frame and appends the board's state to the
-- dump file, the same records mp_host writes:
--   'MPFR', frame (u32), RAM 0x0000-0x27ff, the 15XX 0x4000-0x43ff, the
--   58XXs 0x4800-0x481f as read, then for the main CPU and the sub CPU:
--   PC S U X Y D as u16, DP CC as u8
-- MP_SCRIPT, MP_DUMP, MP_FRAMES, MP_PIX and MP_PIXEVERY (every Nth frame's
-- screen:pixels(), after a u32 frame number and u16 width and height) come
-- from the environment. Script lines: "frame port hex", 1 for pressed, held
-- from that frame on: coins (0x01 coin 1), p1 (0x02 right, 0x08 left),
-- buttons (0x01 the button, 0x04 start 1, 0x08 start 2).
-- MP_TRACE (a file), MP_TRACE_CPU (:maincpu or :sub), MP_TRACE_FROM and
-- MP_TRACE_TO (frames): that CPU's instructions from VBLANK FROM to VBLANK TO,
-- each with its registers and total cycles (MAME runs with -debug).
if not _G.mp_dump then
  _G.mp_dump = true
  local script = os.getenv("MP_SCRIPT")
  local dumpname = os.getenv("MP_DUMP")
  local pixname = os.getenv("MP_PIX")
  if pixname == "" then pixname = nil end
  local pixevery = tonumber(os.getenv("MP_PIXEVERY") or "0")
  local frames = tonumber(os.getenv("MP_FRAMES") or "600")
  local ports = manager.machine.ioport.ports
  local function fields_of(port, masks)
    local t = {}
    for _, f in pairs(ports[port].fields) do
      for _, m in ipairs(masks) do if f.mask == m and not t[m] then t[m] = f end end
    end
    for _, m in ipairs(masks) do assert(t[m], "no field " .. port .. " " .. m) end
    return t
  end
  local fields = { coins = fields_of(":COINS", { 0x01 }),
                   p1 = fields_of(":P1", { 0x02, 0x08 }),
                   buttons = fields_of(":BUTTONS", { 0x01, 0x04, 0x08 }) }
  local events = {}
  if script then
    for line in io.lines(script) do
      local f, port, v = line:match("^(%d+)%s+(%a+%d?)%s+(%x+)")
      if f then
        f = tonumber(f)
        events[f] = events[f] or {}
        table.insert(events[f], { port, tonumber(v, 16) })
      end
    end
  end
  local main = manager.machine.devices[":maincpu"]
  local sub = manager.machine.devices[":sub"]
  local mp = main.spaces["program"]
  local out = dumpname and assert(io.open(dumpname, "wb"))
  local pix = pixname and assert(io.open(pixname, "wb"))
  local screen = manager.machine.screens[":screen"]
  local r16 = { "PC", "S", "U", "X", "Y", "D" }
  local r8 = { "DP", "CC" }
  local n = 0
  local function regs(cpu, t)
    for _, r in ipairs(r16) do t[#t + 1] = string.pack("<I2", cpu.state[r].value) end
    for _, r in ipairs(r8) do t[#t + 1] = string.pack("B", cpu.state[r].value) end
  end
  local function range(a, b)
    local t = {}
    for i = a, b do t[#t + 1] = string.char(mp:read_u8(i)) end
    return table.concat(t)
  end
  local trace = os.getenv("MP_TRACE")
  if trace == "" then trace = nil end
  local trace_cpu = os.getenv("MP_TRACE_CPU") or ":maincpu"
  local trace_from = tonumber(os.getenv("MP_TRACE_FROM") or "2")
  local trace_to = tonumber(os.getenv("MP_TRACE_TO") or "0")
  _G.mp_sub = emu.register_frame_done(function()
    -- (with -debug, MAME's debugger calls frame_done once at time 0 too)
    if manager.machine.time:as_double() < 0.001 then return end
    n = n + 1
    if trace and n == trace_from then
      manager.machine.debugger.visible_cpu = manager.machine.devices[trace_cpu]
      manager.machine.debugger:command('trace ' .. trace .. ',' .. trace_cpu ..
        ',noloop,{tracelog "@ %04X %04X %04X %04X %04X %04X %02X %d\\n",pc,s,u,x,y,d,cc,totalcycles}')
    end
    if trace and n == trace_to then
      manager.machine.debugger:command('trace off,' .. trace_cpu)
    end
    if pix and pixevery > 0 and n % pixevery == 0 then
      local px, w, h = screen:pixels()
      pix:write(string.pack("<I4I2I2", n, w or 0, h or 0), px)
    end
    for _, e in ipairs(events[n] or {}) do
      for mask, f in pairs(fields[e[1]]) do f:set_value((e[2] & mask) ~= 0 and 1 or 0) end
    end
    if out then
      local t = { "MPFR", string.pack("<I4", n), range(0x0000, 0x27ff), range(0x4000, 0x43ff), range(0x4800, 0x481f) }
      regs(main, t)
      regs(sub, t)
      out:write(table.concat(t))
    end
    if n >= frames then
      if out then out:close() end
      if pix then pix:close() end
      manager.machine:exit()
    end
  end)
end
