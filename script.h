#pragma once
// Auto-embedded RG Lua script — do not edit manually

static const char* const RG_SCRIPT = R"LUASCRIPT(
-- RG AutoKill + OneHit + EXP x1000 + Tested Speed | PHUONGIOS KEY SYSTEM

-- ==========================================
-- KEY SYSTEM
-- ==========================================
local KEY_URL = "https://raw.githubusercontent.com/phuongiosviet/keys/main/keys.txt"
local keyInput    = ""
local keyVerified = false
local keyStatus   = "Chua co key"
local keyChecking = false

local function trim(s)
    return s:match("^%s*(.-)%s*$")
end

local function verifyKey(k)
    k = trim(k)
    if k == "" then
        keyStatus = "Vui long nhap key"
        return false
    end
    keyStatus = "Dang kiem tra..."
    keyChecking = true
    local response = http_get(KEY_URL)
    keyChecking = false
    if not response then
        keyStatus = "Loi mang, thu lai"
        return false
    end
    for line in response:gmatch("[^\r\n]+") do
        if trim(line) == k then
            keyStatus = "KEY HOP LE - PHUONGIOS"
            return true
        end
    end
    keyStatus = "Key sai hoac het han"
    return false
end

-- ==========================================
-- MAIN VARS
-- ==========================================
local RGCharacter = Class.fromName("RGCharacter")
local CurrencyExp = Class.fromName("CurrencyExp")
local CurrencyPeakExp = Class.fromName("CurrencyPeakExp")

local autoKill = false
local oneHit = false
local expBoost = true
local expMult = 1000.0

local speedOn = true
local speedValue = 3.0
local speedStatusText = "Ready"
local speedFrameTick = 0

local targetCamp8 = true
local targetCamp2 = true

local scanInterval = 0.30
local tickInterval = 0.06
local expPollInterval = 0.10
local bindInterval = 1.0
local maxPerTick = 16

local lastScan = 0
local lastTick = 0
local lastExpPoll = 0
local lastBind = 0

local monsters = {}
local expObj = nil
local peakObj = nil
local lastExp = nil
local lastPeak = nil
local expLockUntil = 0
local peakLockUntil = 0

local function now()
    return os.clock()
end

local function getCamp(obj)
    local ok, v = pcall(function() return obj.camp end)
    if ok and v ~= nil then return tonumber(v) or -1 end
    return -1
end

local function isTarget(obj)
    local camp = getCamp(obj)
    if targetCamp8 and camp == 8 then return true end
    if targetCamp2 and camp == 2 then return true end
    return false
end

local function isAlive(obj)
    local ok, dead = pcall(function() return obj.is_dead end)
    if ok and dead == true then return false end
    return true
end

local function rescanMonsters()
    monsters = {}
    if not RGCharacter then return 0 end
    local ok, arr = pcall(function() return RGCharacter:findObjects() end)
    if not ok or not arr then return 0 end
    local n = 0
    for i, obj in ipairs(arr) do
        if obj ~= nil and isTarget(obj) then
            monsters[#monsters + 1] = obj
            n = n + 1
        end
    end
    return n
end

local function killTick()
    local t = now()
    if (t - lastTick) < tickInterval then return end
    lastTick = t
    if not autoKill and not oneHit then return end
    if (#monsters == 0) or ((t - lastScan) >= scanInterval) then
        lastScan = t
        rescanMonsters()
    end
    local hit = 0
    for i = #monsters, 1, -1 do
        local obj = monsters[i]
        if obj == nil then
            table.remove(monsters, i)
        else
            if isAlive(obj) and isTarget(obj) then
                if oneHit then pcall(function() obj:SetHp(1.0) end) end
                if autoKill then
                    pcall(function() obj:Dead() end)
                    pcall(function() obj:OverDead() end)
                end
                hit = hit + 1
                if hit >= maxPerTick then break end
            end
        end
    end
end

local function findFirstObj(cls)
    if not cls then return nil end
    local ok, arr = pcall(function() return cls:findObjects() end)
    if not ok or not arr then return nil end
    return arr[1]
end

local function getValue(obj)
    if not obj then return nil end
    local ok, v = pcall(function() return obj:Value() end)
    if ok and type(v) == "number" then return v end
    ok, v = pcall(function() return obj.value end)
    if ok and type(v) == "number" then return v end
    ok, v = pcall(function() return obj:GetValue() end)
    if ok and type(v) == "number" then return v end
    return nil
end

local function addValue(obj, amount, reason)
    if not obj then return false end
    local a = math.floor(tonumber(amount) or 0)
    if a == 0 then return false end
    local ok = pcall(function() return obj:Change(a, reason or "RG_EXP") end)
    return ok
end

local function bindExpObjects(force)
    local t = now()
    if (not force) and ((t - lastBind) < bindInterval) then return end
    lastBind = t
    if expObj == nil then expObj = findFirstObj(CurrencyExp) end
    if peakObj == nil then peakObj = findFirstObj(CurrencyPeakExp) end
    if expObj then
        local v = getValue(expObj)
        if v ~= nil and lastExp == nil then lastExp = v end
    end
    if peakObj then
        local v = getValue(peakObj)
        if v ~= nil and lastPeak == nil then lastPeak = v end
    end
end

local function processOneCurrency(obj, lastVal, lockUntil, reason)
    local t = now()
    local cur = getValue(obj)
    if cur == nil then return lastVal, lockUntil end
    if lastVal == nil then return cur, lockUntil end
    if expBoost and (t > lockUntil) and (cur > lastVal) then
        local delta = cur - lastVal
        local bonus = math.floor(delta * (expMult - 1.0))
        if bonus > 0 then
            if addValue(obj, bonus, reason) then
                lockUntil = t + 0.20
                cur = cur + bonus
            end
        end
    end
    return cur, lockUntil
end

local function expTick()
    local t = now()
    if (t - lastExpPoll) < expPollInterval then return end
    lastExpPoll = t
    bindExpObjects(false)
    lastExp, expLockUntil = processOneCurrency(expObj, lastExp, expLockUntil, "RG_EXP_X1000")
    lastPeak, peakLockUntil = processOneCurrency(peakObj, lastPeak, peakLockUntil, "RG_PEAK_X1000")
end

local function applySpeed(scale)
    local ok, sOk, sMsg = pcall(function()
        local cls = Class.fromName("Module.GameSpeedM")
        if not cls then return false, "Khong tim thay Module.GameSpeedM" end
        local objs = cls:findObjects()
        if not objs or #objs == 0 then return false, "Khong tim thay GameSpeedM object" end
        local v = math.floor(scale + 0.5)
        if v < 1 then v = 1 end
        for i, o in ipairs(objs) do
            pcall(function() o:SetTimeScaleNoEncrypt(v) end)
            pcall(function() o:SetTimeScale(v) end)
        end
        return true, "Da ap dung speed x" .. tostring(v) .. " | objs=" .. tostring(#objs)
    end)
    if ok then return sOk, sMsg end
    return false, tostring(sOk)
end

function OnDraw()
    speedFrameTick = speedFrameTick + 1
    killTick()
    expTick()
    if ImGui.Begin("RG AutoKill + OneHit + EXP + Speed") then
        ImGui.TextColored(1, 0.84, 0, 1, "== PHUONGIOS ==")
        ImGui.Separator()

        -- KEY SECTION
        if not keyVerified then
            ImGui.Text("Nhap key de su dung:")
            local changed, newVal = ImGui.InputText("##key", keyInput, 64)
            if changed then keyInput = newVal end
            ImGui.SameLine()
            if ImGui.Button("Xac nhan") then
                keyVerified = verifyKey(keyInput)
            end
            if keyStatus == "KEY HOP LE - PHUONGIOS" then
                ImGui.TextColored(0, 1, 0, 1, "[✓] " .. keyStatus)
            else
                ImGui.TextColored(1, 0.3, 0.3, 1, "[!] " .. keyStatus)
            end
            ImGui.Separator()
            ImGui.Text("Lien he PHUONGIOS de mua key")
        else
            ImGui.TextColored(0, 1, 0, 1, "[✓] Da xac thuc key")
            ImGui.Separator()
        end

        -- CHỈ HIỆN TÍNH NĂNG NẾU CÓ KEY
        if keyVerified then
        local c1, v1 = ImGui.Checkbox("Auto Kill", autoKill)
        if c1 then autoKill = v1 end
        local c2, v2 = ImGui.Checkbox("One Hit (Set HP=1)", oneHit)
        if c2 then oneHit = v2 end
        local c3, v3 = ImGui.Checkbox("EXP x1000", expBoost)
        if c3 then expBoost = v3 end
        local c4, v4 = ImGui.Checkbox("Bat Speed Hack", speedOn)
        if c4 then speedOn = v4 end
        local c5, v5 = ImGui.SliderFloat("Speed x", speedValue, 1.0, 10.0)
        if c5 then speedValue = v5 end
        local targetSpeed = speedOn and speedValue or 1.0
        if ImGui.Button("Ap dung speed ngay") then
            local ok, msg = applySpeed(targetSpeed)
            speedStatusText = (ok and "OK: " or "ERR: ") .. tostring(msg)
        end
        ImGui.SameLine()
        if ImGui.Button("Reset EXP Baseline") then
            expObj = nil; peakObj = nil
            lastExp = nil; lastPeak = nil
            expLockUntil = 0; peakLockUntil = 0
            bindExpObjects(true)
        end
        ImGui.SameLine()
        if ImGui.Button("Rescan") then
            rescanMonsters()
            lastScan = now()
        end
        if c4 or c5 then
            local ok, msg = applySpeed(targetSpeed)
            speedStatusText = (ok and "OK: " or "ERR: ") .. tostring(msg)
        end
        if speedFrameTick % 30 == 0 then
            local ok, msg = applySpeed(targetSpeed)
            speedStatusText = (ok and "OK: " or "ERR: ") .. tostring(msg)
        end
        ImGui.Text("Target camp: 8 + 2")
        ImGui.Text("EXP: x" .. tostring(math.floor(expMult)))
        ImGui.Text("Trang thai speed: " .. speedStatusText)
        end -- end keyVerified
    end
    ImGui.End()
end

bindExpObjects(true)
rescanMonsters()
print("[RG] Script loaded OK")
)LUASCRIPT";
