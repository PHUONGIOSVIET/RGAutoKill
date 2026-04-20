#pragma once
#include <string>
#include <vector>
#include "il2cpp.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#import <imgui.h>
#import <Foundation/Foundation.h>

static const char* OBJ_META = "RGObject";
static const char* CLS_META = "RGClass";

// ─── Object Proxy ─────────────────────────────────────────────────────────────
struct ObjProxy { Il2CppObject* obj; Il2CppClass* klass; };
struct ClsProxy { Il2CppClass* klass; std::string name; };

static ObjProxy* checkObj(lua_State* L, int idx = 1) {
    return (ObjProxy*)luaL_checkudata(L, idx, OBJ_META);
}
static ClsProxy* checkCls(lua_State* L, int idx = 1) {
    return (ClsProxy*)luaL_checkudata(L, idx, CLS_META);
}

static void pushObject(lua_State* L, Il2CppObject* obj, Il2CppClass* klass = nullptr) {
    ObjProxy* p = (ObjProxy*)lua_newuserdata(L, sizeof(ObjProxy));
    p->obj   = obj;
    p->klass = klass ? klass : (obj ? IL2CPP::get().getObjectClass(obj) : nullptr);
    luaL_setmetatable(L, OBJ_META);
}

static void pushClass(lua_State* L, Il2CppClass* klass, const std::string& name) {
    ClsProxy* p = (ClsProxy*)lua_newuserdata(L, sizeof(ClsProxy));
    p->klass = klass;
    p->name  = name;
    luaL_setmetatable(L, CLS_META);
}

// ─── http_get ─────────────────────────────────────────────────────────────────
static int lua_http_get(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    __block NSString* result = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    NSURL* nsUrl = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsUrl
        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:6.0];
    [[[NSURLSession sharedSession] dataTaskWithRequest:req
        completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
            if (d && !e) result = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
            dispatch_semaphore_signal(sem);
        }] resume];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 7*NSEC_PER_SEC));
    if (result) lua_pushstring(L, [result UTF8String]);
    else        lua_pushnil(L);
    return 1;
}

// ─── Class.fromName ──────────────────────────────────────────────────────────
static int lua_class_fromName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (!IL2CPP::get().ready()) { lua_pushnil(L); return 1; }
    Il2CppClass* klass = IL2CPP::get().classFromName(name);
    if (!klass) { lua_pushnil(L); return 1; }
    pushClass(L, klass, name);
    return 1;
}

// ─── classProxy:findObjects() ─────────────────────────────────────────────────
static int cls_findObjects(lua_State* L) {
    ClsProxy* cp = checkCls(L);
    if (!cp || !cp->klass || !IL2CPP::get().ready()) {
        lua_newtable(L); return 1;
    }
    auto objs = IL2CPP::get().findObjects(cp->klass);
    lua_createtable(L, (int)objs.size(), 0);
    for (int i = 0; i < (int)objs.size(); i++) {
        pushObject(L, objs[i], cp->klass);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ─── ObjProxy methods ─────────────────────────────────────────────────────────
static int obj_SetHp(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj || !IL2CPP::get().ready()) return 0;
    float v = (float)luaL_checknumber(L, 2);
    void* args[] = { &v };
    IL2CPP::get().invoke(p->obj, "SetHp", args, 1);
    return 0;
}
static int obj_Dead(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj || !IL2CPP::get().ready()) return 0;
    IL2CPP::get().invoke(p->obj, "Dead", nullptr, 0);
    return 0;
}
static int obj_OverDead(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj || !IL2CPP::get().ready()) return 0;
    IL2CPP::get().invoke(p->obj, "OverDead", nullptr, 0);
    return 0;
}
static int obj_Value(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj || !IL2CPP::get().ready()) { lua_pushnil(L); return 1; }
    float v = IL2CPP::get().getField<float>(p->obj, "_value");
    lua_pushnumber(L, v);
    return 1;
}
static int obj_GetValue(lua_State* L) { return obj_Value(L); }

static int obj_Change(lua_State* L) {
    ObjProxy* p = checkObj(L);
    if (!p || !p->obj || !IL2CPP::get().ready()) { lua_pushboolean(L, 0); return 1; }
    int amount = (int)luaL_checkinteger(L, 2);
    void* args[] = { &amount };
    IL2CPP::get().invoke(p->obj, "Change", args, 1);
    lua_pushboolean(L, 1);
    return 1;
}

// ─── ObjProxy __index — trả về field hoặc method closure ─────────────────────
static int obj_index(lua_State* L) {
    ObjProxy* p = checkObj(L);
    const char* key = luaL_checkstring(L, 2);
    if (!p || !p->obj) { lua_pushnil(L); return 1; }

    // Các field biết trước — trả thẳng giá trị
    if (strcmp(key, "is_dead") == 0) {
        if (!IL2CPP::get().ready()) { lua_pushboolean(L, 0); return 1; }
        bool v = IL2CPP::get().getField<bool>(p->obj, "is_dead");
        lua_pushboolean(L, v ? 1 : 0);
        return 1;
    }
    if (strcmp(key, "camp") == 0) {
        if (!IL2CPP::get().ready()) { lua_pushinteger(L, -1); return 1; }
        int v = IL2CPP::get().getField<int>(p->obj, "camp");
        lua_pushinteger(L, v);
        return 1;
    }
    if (strcmp(key, "value") == 0) {
        if (!IL2CPP::get().ready()) { lua_pushnumber(L, 0); return 1; }
        float v = IL2CPP::get().getField<float>(p->obj, "value");
        lua_pushnumber(L, v);
        return 1;
    }

    // Tất cả key khác → trả về method closure
    // Closure capture obj pointer + method name
    lua_pushlightuserdata(L, (void*)p->obj);
    lua_pushstring(L, key);
    lua_pushcclosure(L, [](lua_State* LL) -> int {
        Il2CppObject* obj = (Il2CppObject*)lua_touserdata(LL, lua_upvalueindex(1));
        const char* mname = lua_tostring(LL, lua_upvalueindex(2));
        if (!obj || !mname || !IL2CPP::get().ready()) {
            lua_pushboolean(LL, 0); return 1;
        }
        int nargs = lua_gettop(LL);
        std::vector<float> fargs(nargs);
        std::vector<void*> ptrs(nargs);
        for (int i = 0; i < nargs; i++) {
            fargs[i] = lua_isnumber(LL, i+1) ? (float)lua_tonumber(LL, i+1) : 0.f;
            ptrs[i] = &fargs[i];
        }
        IL2CPP::get().invoke(obj, mname, nargs > 0 ? ptrs.data() : nullptr, nargs);
        lua_pushboolean(LL, 1);
        return 1;
    }, 2);
    return 1;
}

// ─── ImGui bindings ───────────────────────────────────────────────────────────
static int imgui_Begin(lua_State* L) {
    const char* t = luaL_checkstring(L, 1);
    lua_pushboolean(L, ImGui::Begin(t) ? 1 : 0);
    return 1;
}
static int imgui_End(lua_State* L) {
    ImGui::End(); return 0;
}
static int imgui_Checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    bool c = ImGui::Checkbox(label, &v);
    lua_pushboolean(L, c ? 1 : 0);
    lua_pushboolean(L, v ? 1 : 0);
    return 2;
}
static int imgui_SliderFloat(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float v   = (float)luaL_checknumber(L, 2);
    float mn  = (float)luaL_checknumber(L, 3);
    float mx  = (float)luaL_checknumber(L, 4);
    bool c = ImGui::SliderFloat(label, &v, mn, mx);
    lua_pushboolean(L, c ? 1 : 0);
    lua_pushnumber(L, v);
    return 2;
}
static int imgui_Button(lua_State* L) {
    lua_pushboolean(L, ImGui::Button(luaL_checkstring(L, 1)) ? 1 : 0);
    return 1;
}
static int imgui_Text(lua_State* L) {
    ImGui::Text("%s", luaL_checkstring(L, 1)); return 0;
}
static int imgui_SameLine(lua_State* L) {
    ImGui::SameLine(); return 0;
}
static int imgui_Separator(lua_State* L) {
    ImGui::Separator(); return 0;
}
static int imgui_InputText(lua_State* L) {
    const char* label   = luaL_checkstring(L, 1);
    const char* current = luaL_optstring(L, 2, "");
    int maxLen = (int)luaL_optinteger(L, 3, 128);
    std::vector<char> buf(maxLen + 1, 0);
    strncpy(buf.data(), current, maxLen);
    bool c = ImGui::InputText(label, buf.data(), maxLen + 1);
    lua_pushboolean(L, c ? 1 : 0);
    lua_pushstring(L, buf.data());
    return 2;
}
static int imgui_TextColored(lua_State* L) {
    float r = (float)luaL_checknumber(L, 1);
    float g = (float)luaL_checknumber(L, 2);
    float b = (float)luaL_checknumber(L, 3);
    float a = (float)luaL_optnumber(L, 4, 1.0);
    const char* t = luaL_checkstring(L, 5);
    ImGui::TextColored(ImVec4(r,g,b,a), "%s", t);
    return 0;
}
static int imgui_SetNextWindowSize(lua_State* L) {
    float w = (float)luaL_checknumber(L, 1);
    float h = (float)luaL_checknumber(L, 2);
    int cond = (int)luaL_optinteger(L, 3, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), cond);
    return 0;
}
static int imgui_SetNextWindowPos(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    int cond = (int)luaL_optinteger(L, 3, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(x, y), cond);
    return 0;
}
static int imgui_GetDisplaySize(lua_State* L) {
    ImVec2 s = ImGui::GetIO().DisplaySize;
    lua_pushnumber(L, s.x);
    lua_pushnumber(L, s.y);
    return 2;
}
static int imgui_SetNextWindowCollapsed(lua_State* L) {
    bool v = lua_toboolean(L, 1) != 0;
    int cond = (int)luaL_optinteger(L, 2, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(v, cond);
    return 0;
}
static int imgui_BeginEx(lua_State* L) {
    // BeginEx(title, flags)
    const char* t = luaL_checkstring(L, 1);
    int flags = (int)luaL_optinteger(L, 2, 0);
    lua_pushboolean(L, ImGui::Begin(t, nullptr, flags) ? 1 : 0);
    return 1;
}

static const luaL_Reg imgui_lib[] = {
    { "Begin",                 imgui_Begin       },
    { "BeginEx",               imgui_BeginEx     },
    { "End",                   imgui_End         },
    { "Checkbox",              imgui_Checkbox    },
    { "SliderFloat",           imgui_SliderFloat },
    { "Button",                imgui_Button      },
    { "Text",                  imgui_Text        },
    { "SameLine",              imgui_SameLine    },
    { "Separator",             imgui_Separator   },
    { "InputText",             imgui_InputText   },
    { "TextColored",           imgui_TextColored },
    { "SetNextWindowSize",     imgui_SetNextWindowSize },
    { "SetNextWindowPos",      imgui_SetNextWindowPos  },
    { "SetNextWindowCollapsed",imgui_SetNextWindowCollapsed },
    { "GetDisplaySize",        imgui_GetDisplaySize },
    { nullptr, nullptr }
};

// ─── Register tất cả vào Lua state ───────────────────────────────────────────
static void registerBindings(lua_State* L) {
    // ObjProxy metatable
    luaL_newmetatable(L, OBJ_META);          // [M]
    lua_pushcfunction(L, obj_index);
    lua_setfield(L, -2, "__index");          // M.__index = obj_index
    // Gắn thêm method trực tiếp vào meta
    lua_pushcfunction(L, obj_SetHp);    lua_setfield(L, -2, "SetHp");
    lua_pushcfunction(L, obj_Dead);     lua_setfield(L, -2, "Dead");
    lua_pushcfunction(L, obj_OverDead); lua_setfield(L, -2, "OverDead");
    lua_pushcfunction(L, obj_Value);    lua_setfield(L, -2, "Value");
    lua_pushcfunction(L, obj_GetValue); lua_setfield(L, -2, "GetValue");
    lua_pushcfunction(L, obj_Change);   lua_setfield(L, -2, "Change");
    lua_pop(L, 1);                           // []

    // ClsProxy metatable
    luaL_newmetatable(L, CLS_META);          // [M]
    lua_newtable(L);                         // [M, T]
    lua_pushcfunction(L, cls_findObjects);
    lua_setfield(L, -2, "findObjects");      // T.findObjects = ...
    lua_setfield(L, -2, "__index");          // M.__index = T
    lua_pop(L, 1);                           // []

    // http_get global
    lua_pushcfunction(L, lua_http_get);
    lua_setglobal(L, "http_get");

    // Class global
    lua_newtable(L);
    lua_pushcfunction(L, lua_class_fromName);
    lua_setfield(L, -2, "fromName");
    lua_setglobal(L, "Class");

    // ImGui global
    lua_newtable(L);
    luaL_setfuncs(L, imgui_lib, 0);
    lua_setglobal(L, "ImGui");
}
