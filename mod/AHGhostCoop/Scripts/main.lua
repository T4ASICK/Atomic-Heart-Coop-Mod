-- All Unreal access belongs to the native game-thread tick. Lua only sends controls.
local directory = "Mods/AHGhostCoop/state/"
local session = tostring(os.time()) .. "-" .. tostring(math.random(1, 999999))
local sequence = 0
local busy = false

local function log(text)
    print("[AHGhostCoop] " .. tostring(text) .. "\n")
end

local function command(value)
    if busy then return end
    busy = true
    ExecuteWithDelay(400, function() busy = false end)
    sequence = sequence + 1
    local path = directory .. "native_command.txt"
    local file, error = io.open(path .. ".lua-tmp", "w")
    if not file then log(error); return end
    file:write("AHGC2\t" .. session .. "\t" .. sequence .. "\t" .. value .. "\n")
    file:close()
    os.remove(path)
    local ok, rename_error = os.rename(path .. ".lua-tmp", path)
    if not ok then log(rename_error) end
end

RegisterKeyBindAsync(Key.HOME, function() command("menu_toggle") end)
log("0.10.6-preview controls ready: Home opens/closes the mod menu")
