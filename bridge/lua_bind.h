#pragma once
// Lua bindings: Class, ImGui, game object proxy
// Provides: Class.fromName(), obj:Method(), obj.field, ImGui.*

#include <string>
#include <vector>
#include <memory>
#include "il2cpp.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#import <imgui.h>

// ─── Object Proxy ────────────────────────────────────────────────────────────
// Lua userdata wrapping an Il2CppObject*
struct ObjProxy {
    Il2CppObject* obj;
    Il2CppClass*  klass;
};

static const char* OBJ_META  = "RGObject";
static const char* CLS_META  = "RGClass";

static ObjProxy* checkObj(lua_State* L, int idx = 1) {
    return (ObjProxy*)luaL_checkudata(L, idx, OBJ_META);
}

// Push a new ObjProxy userdata onto the Lua stack
static void pushObject(lua_State* L, Il2CppObject* obj, Il2CppClass* klass = nullptr) {
    ObjProxy* p = (ObjProxy*)lua_newuserdata(L, sizeof(ObjProxy));
    p->obj   = obj;
    p->klass = klass ? klass : (obj ? IL2CPP::get().il2cpp_object_get_class_stub(obj) : nullptr);
    luaL_setmetatable(L, OBJ_META);
}

// ─── Class Proxy ─────────────────────────────────────────────────────────────
struct ClsProxy {
    Il2CppClass* klass;
    std::string  name;
};

static ClsProxy* checkCls(lua_State* L, int idx = 1) {
    return (ClsProxy*)luaL_checkudata(L, idx, CLS_META);
}

static void pushClass(lua_State* L, Il2CppClass* klass, const std::string& name) {
    ClsProxy* p = (ClsProxy*)lua_newuserdata(L, sizeof(ClsProxy));
    p->klass = klass;
    p->name  = name;
    luaL_setmetatable(L, CLS_META);
}

// ─── Class.fromName ──────────────────────────────────────────────────────────
static int lua_class_fromName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Il2CppClass* klass = IL2CPP::get().classFromName(name);
    if (!klass) {
        lua_pushnil(L);
        return 1;
    }
    pushClass(L, klass, name);
    return 1;
}

// ─── classProxy:findObjects() ────────────────────────────────────────────────
static int cls_findObjects(lua_State* L) {
    ClsProxy* cp = checkCls(L);
    if (!cp || !cp->klass) { lua_newtable(L); return 1; }

    auto objs = IL2CPP::get().findObjects(cp->klass);
    lua_createtable(L, (int)objs.size(), 0);
    for (int i = 0; i < (int)objs.size(); i++) {
        pushObject(L, objs[i], cp->klass);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ─── ObjProxy: __index (obj.field or obj:Method) ─────────────────────────────
static int obj_index(lua_State* L) {
    ObjProxy* p = checkObj(L);
    const char* key = luaL_checkstring(L, 2);
    if (!p || !p->obj) { lua_pushnil(L); return 1; }

    // Try bool field (is_dead, etc.)
    bool bVal = IL2CPP::get().getField<bool>(p->obj, key);
    // Heuristic: if field found return it; otherwise return method closure
    // For simplicity, return a method closure — callers use pcall anyway
    lua_pushlightuserdata(L, p->obj);
    lua_pushstring(L, key);
    lua_pushcclosure(L, [](lua_State* LL) -> int {
        Il2CppObject* obj = (Il2CppObject*)lua_touserdata(LL, lua_upvalueindex(1));
        const char* mname = lua_tostring(LL, lua_upvalueindex(2));
        if (!obj || !mname) { lua_pushnil(LL); return 1; }

        // Collect numeric arguments
        int nargs = lua_gettop(LL);
        std::vector<void*> args;
        std::vector<float> floatArgs(nargs);
        std::vector<int>   intArgs(nargs);

        for (int i = 1; i <= nargs; i++) {
            if (lua_isnumber(LL, i)) {
                floatArgs[i-1] = (float)lua_tonumber(LL, i);
                args.push_back(&floatArgs[i-1]);
            }
        }

        void** argPtr = args.empty() ? nullptr : args.data();
        IL2CPP::get().invoke(obj, mname, argPtr, (int)args.size());
        lua_pushboolean(LL, 1);
        return 1;
    }, 2);
    return 1;
}

// Special __index for known fields
static int obj_index_field(lua_State* L) {
    ObjProxy* p = checkObj(L);
    const char* key = luaL_checkstring(L, 2);
    if (!p || !p->obj) { lua_pushnil(L); return 1; }

    // is_dead → bool
    if (strcmp(key, "is_dead") == 0) {
        bool v = IL2CPP::get().getField<bool>(p->obj, "is_dead");
        lua_pushboolean(L, v ? 1 : 0);
        return 1;
    }
    // camp → int
    if (strcmp(key, "camp") == 0) {
        int v = IL2CPP::get().getField<int>(p->obj, "camp");
        lua_pushinteger(L, v);
        return 1;
    }
    // value → float
    if (strcmp(key, "value") == 0) {
        float v = IL2CPP::get().getField<float>(p->obj, "value");
        lua_pushnumber(L, v);
        return 1;
    }
    // Fallback: method closure
    return obj_index(L);
}

// ─── ObjProxy special methods via __index ─────────────────────────────────────
// obj:Value(), obj:GetValue(), obj:Change(), obj:SetHp(), obj:Dead(), etc.
// All handled by the closure in obj_index. Specialized ones below for accuracy:

static int obj_SetHp(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj) return 0;
    float hp = (float)luaL_checknumber(L, 2);
    void* args[] = { &hp };
    IL2CPP::get().invoke(p->obj, "SetHp", args, 1);
    return 0;
}

static int obj_Dead(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj) return 0;
    IL2CPP::get().invoke(p->obj, "Dead", nullptr, 0);
    return 0;
}

static int obj_OverDead(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj) return 0;
    IL2CPP::get().invoke(p->obj, "OverDead", nullptr, 0);
    return 0;
}

static int obj_Value(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj) { lua_pushnil(L); return 1; }
    float v = IL2CPP::get().getField<float>(p->obj, "_value");
    lua_pushnumber(L, v);
    return 1;
}

static int obj_GetValue(lua_State* L) {
    return obj_Value(L);
}

static int obj_Change(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj) { lua_pushboolean(L, 0); return 1; }
    int amount = (int)luaL_checkinteger(L, 2);
    // reason string arg (3rd) — pass as Il2CppString if needed, skip for now
    void* args[] = { &amount };
    IL2CPP::get().invoke(p->obj, "Change", args, 1);
    lua_pushboolean(L, 1);
    return 1;
}

// ─── ObjProxy metatable ───────────────────────────────────────────────────────
static const luaL_Reg obj_methods[] = {
    { "SetHp",      obj_SetHp     },
    { "Dead",       obj_Dead      },
    { "OverDead",   obj_OverDead  },
    { "Value",      obj_Value     },
    { "GetValue",   obj_GetValue  },
    { "Change",     obj_Change    },
    { nullptr, nullptr }
};

// ─── ImGui Lua Bindings ───────────────────────────────────────────────────────
static int imgui_Begin(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool open = ImGui::Begin(name);
    lua_pushboolean(L, open ? 1 : 0);
    return 1;
}
static int imgui_End(lua_State* L) {
    ImGui::End(); return 0;
}
static int imgui_Checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    bool changed = ImGui::Checkbox(label, &v);
    lua_pushboolean(L, changed ? 1 : 0);
    lua_pushboolean(L, v ? 1 : 0);
    return 2;
}
static int imgui_SliderFloat(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float v     = (float)luaL_checknumber(L, 2);
    float vmin  = (float)luaL_checknumber(L, 3);
    float vmax  = (float)luaL_checknumber(L, 4);
    bool changed = ImGui::SliderFloat(label, &v, vmin, vmax);
    lua_pushboolean(L, changed ? 1 : 0);
    lua_pushnumber(L, v);
    return 2;
}
static int imgui_Button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool pressed = ImGui::Button(label);
    lua_pushboolean(L, pressed ? 1 : 0);
    return 1;
}
static int imgui_Text(lua_State* L) {
    const char* txt = luaL_checkstring(L, 1);
    ImGui::Text("%s", txt);
    return 0;
}
static int imgui_SameLine(lua_State* L) {
    ImGui::SameLine(); return 0;
}

static const luaL_Reg imgui_lib[] = {
    { "Begin",       imgui_Begin      },
    { "End",         imgui_End        },
    { "Checkbox",    imgui_Checkbox   },
    { "SliderFloat", imgui_SliderFloat},
    { "Button",      imgui_Button     },
    { "Text",        imgui_Text       },
    { "SameLine",    imgui_SameLine   },
    { nullptr, nullptr }
};

// ─── Register everything into the Lua state ───────────────────────────────────
static void registerBindings(lua_State* L) {
    // ObjProxy metatable
    luaL_newmetatable(L, OBJ_META);
    lua_pushstring(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, obj_methods, 0);
    // Add field-aware __index
    lua_pushstring(L, "__index");
    lua_pushcfunction(L, obj_index_field);
    lua_settable(L, -4); // metatable.__index = obj_index_field
    lua_pop(L, 2);

    // ClsProxy metatable
    luaL_newmetatable(L, CLS_META);
    lua_pushstring(L, "__index");
    lua_newtable(L);
    lua_pushstring(L, "findObjects");
    lua_pushcfunction(L, cls_findObjects);
    lua_settable(L, -3);
    lua_settable(L, -3);
    lua_pop(L, 1);

    // Class table
    lua_newtable(L);
    lua_pushstring(L, "fromName");
    lua_pushcfunction(L, lua_class_fromName);
    lua_settable(L, -3);
    lua_setglobal(L, "Class");

    // ImGui table
    lua_newtable(L);
    luaL_setfuncs(L, imgui_lib, 0);
    lua_setglobal(L, "ImGui");
}
