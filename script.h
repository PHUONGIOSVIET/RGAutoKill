#pragma once
// Auto-embedded RG Lua script — do not edit manually

static const char* const RG_SCRIPT = R"LUASCRIPT(
-- RG AutoKill + OneHit + EXP x1000 + Tested Speed | PHUONGIOS KEY SYSTEM

-- ==========================================
-- KEY SYSTEM — PHUONGIOS.COM
-- ==========================================
-- Thử lần lượt cac endpoint cua phuongios.com
local KEY_URLS = {
    "https://phuongios.com/keys/soulknight.txt",
    "https://phuongios.com/keys/soulknight-prequel.txt",
    "https://phuongios.com/key/soulknight.txt",
    "https://phuongios.com/api/keys/soulknight.txt",
    "https://www.phuongios.com/keys/soulknight.txt",
}
-- Endpoint API verify (neu phuongios.com dung dang GET ?key=XXX)
local VERIFY_API_URLS = {
    "https://phuongios.com/api/verify?game=soulknight&key=",
    "https://phuongios.com/verify?key=",
}

local keyInput    = ""
local keyVerified = false
local keyStatus   = "Chua co key"
local keyChecking = false

local function trim(s)
    return s:match("^%s*(.-)%s*$")
end

-- Thu tai danh sach key tu nhieu URL, tra ve response dau tien co noi dung
local function fetchKeyList()
    for _, url in ipairs(KEY_URLS) do
        local r = http_get(url)
        if r and #r > 0 and not r:find("<!DOCTYPE", 1, true) and not r:find("<html", 1, true) then
            return r, url
        end
    end
    return nil, nil
end

-- Thu goi API verify dang GET (neu tra ve "OK"/"VALID"/"1" => hop le)
local function tryVerifyAPI(k)
    for _, base in ipairs(VERIFY_API_URLS) do
        local r = http_get(base .. k)
        if r then
            local low = r:lower()
            if low:find("ok") or low:find("valid") or low:find("true") or trim(r) == "1" then
                return true
            end
        end
    end
    return false
end

local function verifyKey(k)
    k = trim(k)
    if k == "" then
        keyStatus = "Vui long nhap key"
        return false
    end
    keyStatus = "Dang kiem tra voi phuongios.com..."
    keyChecking = true

    -- 1) Thu tai danh sach key tu phuongios.com
    local list, usedUrl = fetchKeyList()
    if list then
        for line in list:gmatch("[^\r\n]+") do
            if trim(line) == k then
                keyStatus = "KEY HOP LE - PHUONGIOS"
                keyChecking = false
                return true
            end
        end
    end

    -- 2) Thu API verify
    if tryVerifyAPI(k) then
        keyStatus = "KEY HOP LE - PHUONGIOS"
        keyChecking = false
        return true
    end

    keyChecking = false
    if not list then
        keyStatus = "Khong ket noi duoc phuongios.com"
    else
        keyStatus = "Key sai hoac het han"
    end
    return false
end

-- ==========================================
-- MAIN VARS
-- ==========================================
local RGCharacter     = Class.fromName("RGCharacter")
local CurrencyExp     = Class.fromName("CurrencyExp")
local CurrencyPeakExp = Class.fromName("CurrencyPeakExp")

-- Debug search state
local dbgPattern  = ""
local dbgResults  = {}
local dbgAsmList  = {}
local dbgAsmCount = 0
local dbgShow     = false

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
    -- Canh kich thuoc menu vua voi man hinh
    local sw, sh = ImGui.GetDisplaySize()
    local mw = math.min(420, sw * 0.75)
    local mh = math.min(520, sh * 0.80)
    ImGui.SetNextWindowSize(mw, mh)
    ImGui.SetNextWindowPos((sw - mw) * 0.5, (sh - mh) * 0.5)
    if ImGui.Begin("RG AutoKill + OneHit + EXP + Speed") then
        ImGui.TextColored(1, 0.84, 0, 1, "== PHUONGIOS ==")
        ImGui.Separator()

        -- KEY SECTION
        if not keyVerified then
            ImGui.Text("Nhap key de su dung:")
            local changed, newVal = ImGui.InputText("##key", keyInput, 128)
            if changed then keyInput = newVal end
            if ImGui.Button("Dan tu clipboard") then
                local s = paste_clipboard()
                if s and s ~= "" then
                    keyInput = s
                    keyStatus = "Da dan key - bam Xac nhan"
                else
                    keyStatus = "Clipboard trong"
                end
            end
            ImGui.SameLine()
            if ImGui.Button("Xac nhan") then
                keyVerified = verifyKey(keyInput)
            end
            ImGui.SameLine()
            if ImGui.Button("Xoa") then
                keyInput = ""
                keyStatus = "Chua co key"
            end
            -- Preview key hien tai (an bot de gon)
            if keyInput ~= "" then
                local preview = keyInput
                if #preview > 24 then preview = preview:sub(1, 20) .. "..." end
                ImGui.TextColored(0.7, 0.85, 1, 1, "Key: " .. preview)
            end
            if keyStatus == "KEY HOP LE - PHUONGIOS" then
                ImGui.TextColored(0, 1, 0, 1, "[OK] " .. keyStatus)
            else
                ImGui.TextColored(1, 0.3, 0.3, 1, "[!] " .. keyStatus)
            end
            ImGui.Separator()
            ImGui.TextColored(1, 0.84, 0, 1, "Mua key tai: phuongios.com")
            ImGui.Text("Soul Knight Prequel - Key hack game")
        else
            ImGui.TextColored(0, 1, 0, 1, "[OK] Da xac thuc key")
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

        ImGui.Separator()
        -- ===== DEBUG: tim class that cua game (HybridCLR hot-update) =====
        local dbgToggle, dbgV = ImGui.Checkbox("Debug: tim class that", dbgShow)
        if dbgToggle then dbgShow = dbgV end
        if dbgShow then
            ImGui.TextColored(1, 0.84, 0, 1, "Game dung HybridCLR — class thuc te load sau")
            if ImGui.Button("Refresh assemblies") then
                dbgAsmCount = Class.refresh()
                dbgAsmList  = Class.assemblies()
            end
            ImGui.SameLine()
            ImGui.Text("So assembly: " .. tostring(dbgAsmCount))
            local ch, nv = ImGui.InputText("Tu khoa", dbgPattern, 64)
            if ch then dbgPattern = nv end
            if ImGui.Button("Tim class") then
                dbgResults = Class.list(dbgPattern, 120)
            end
            ImGui.SameLine()
            if ImGui.Button("Copy ket qua") then
                local txt = ""
                for _, n in ipairs(dbgResults) do txt = txt .. n .. "\n" end
                copy_clipboard(txt)
            end
            ImGui.Text("Tim duoc " .. tostring(#dbgResults) .. " class:")
            local showN = math.min(#dbgResults, 30)
            for i = 1, showN do
                ImGui.Text("  " .. dbgResults[i])
            end
            if #dbgResults > showN then
                ImGui.Text("  ... (" .. tostring(#dbgResults - showN) .. " them - bam Copy)")
            end
            if #dbgAsmList > 0 then
                ImGui.Separator()
                ImGui.Text("Danh sach DLL (" .. tostring(#dbgAsmList) .. "):")
                local showA = math.min(#dbgAsmList, 12)
                for i = 1, showA do
                    ImGui.Text("  " .. dbgAsmList[i])
                end
                if #dbgAsmList > showA then
                    ImGui.Text("  ... (" .. tostring(#dbgAsmList - showA) .. " them)")
                end
            end
        end
        end -- end keyVerified
    end
    ImGui.End()
end

bindExpObjects(true)
rescanMonsters()
print("[RG] Script loaded OK")
)LUASCRIPT";
