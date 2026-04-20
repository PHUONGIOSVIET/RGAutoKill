// RGAutoKill.dylib
// No substrate dependency — pure ObjC swizzling + Metal hook

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <os/log.h>

extern "C" {
#include "vendor/lua/include/lua.h"
#include "vendor/lua/include/lualib.h"
#include "vendor/lua/include/lauxlib.h"
}

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/backends/imgui_impl_metal.h"
#include "backends/imgui_impl_uikit.h"
#include "bridge/il2cpp.h"
#include "bridge/lua_bind.h"
#include "script.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "[RG] " fmt, ##__VA_ARGS__)

// ─── Globals ─────────────────────────────────────────────────────────────────
static lua_State*        gLua        = nullptr;
static bool              gImGuiReady = false;
static id<MTLDevice>     gDevice     = nil;
static id<MTLCommandQueue> gQueue    = nil;
static IMP               gOrigDrawRect = nullptr;

// ─── Lua OnDraw ──────────────────────────────────────────────────────────────
static void callOnDraw() {
    if (!gLua) return;
    lua_getglobal(gLua, "OnDraw");
    if (!lua_isfunction(gLua, -1)) { lua_pop(gLua, 1); return; }
    if (lua_pcall(gLua, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(gLua, -1);
        LOG("OnDraw err: %{public}s", err ? err : "?");
        lua_pop(gLua, 1);
    }
}

// ─── Hook: MTKView drawRect ───────────────────────────────────────────────────
static void hook_drawRect(id self, SEL sel, CGRect rect) {
    // Gọi drawRect gốc của game
    ((void(*)(id, SEL, CGRect))gOrigDrawRect)(self, sel, rect);

    if (!gImGuiReady || !gLua || !gDevice) return;

    // Lấy layer và drawable
    CAMetalLayer* layer = (CAMetalLayer*)[self layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) return;

    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionLoad;   // giữ frame game
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> cmd = [gQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

    // ImGui frame
    ImGui_ImplMetal_NewFrame(rpd);
    ImGui_ImplUIKit_NewFrame((__bridge UIView*)(__bridge void*)self);
    ImGui::NewFrame();

    callOnDraw();   // ← chạy script Lua

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);

    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

// ─── Swizzle MTKView ─────────────────────────────────────────────────────────
static bool hookMTKView() {
    Class cls = NSClassFromString(@"MTKView");
    if (!cls) { LOG("MTKView not found"); return false; }

    Method m = class_getInstanceMethod(cls, @selector(drawRect:));
    if (!m) { LOG("drawRect: not found"); return false; }

    gOrigDrawRect = method_setImplementation(m, (IMP)hook_drawRect);
    LOG("MTKView hooked OK");
    return true;
}

// ─── Init ImGui ───────────────────────────────────────────────────────────────
static void initImGui(UIView* view) {
    if (gImGuiReady) return;

    gDevice = MTLCreateSystemDefaultDevice();
    if (!gDevice) { LOG("No Metal device"); return; }

    gQueue = [gDevice newCommandQueue];

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 2.5f;             // chữ to trên màn hình nhỏ
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(2.2f);
    ImGui::GetStyle().WindowRounding   = 8.0f;
    ImGui::GetStyle().FrameRounding    = 4.0f;

    ImGui_ImplMetal_Init(gDevice);
    ImGui_ImplUIKit_Init(view);

    gImGuiReady = true;
    LOG("ImGui ready");
}

// ─── Init Lua ─────────────────────────────────────────────────────────────────
static void initLua() {
    if (gLua) return;
    gLua = luaL_newstate();
    luaL_openlibs(gLua);
    registerBindings(gLua);

    if (luaL_dostring(gLua, RG_SCRIPT) != LUA_OK) {
        const char* err = lua_tostring(gLua, -1);
        LOG("Script error: %{public}s", err ? err : "?");
        lua_pop(gLua, 1);
    } else {
        LOG("Script loaded OK");
    }
}

// ─── Tìm base IL2CPP ─────────────────────────────────────────────────────────
static uintptr_t findIL2CppBase() {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char* name = _dyld_get_image_name(i);
        if (name && (strstr(name, "UnityFramework") || strstr(name, "GameAssembly"))) {
            return (uintptr_t)_dyld_get_image_header(i);
        }
    }
    return 0;
}

// ─── Khởi động có delay ──────────────────────────────────────────────────────
static void tryInit(int attempt) {
    uintptr_t base = findIL2CppBase();
    if (base == 0 || !IL2CPP::get().init(base)) {
        if (attempt < 10) {
            dispatch_after(
                dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC),
                dispatch_get_main_queue(),
                ^{ tryInit(attempt + 1); });
        } else {
            LOG("IL2CPP init failed after 10 attempts");
        }
        return;
    }
    LOG("IL2CPP OK, base=0x%lx (attempt %d)", base, attempt);

    initLua();

    if (!hookMTKView()) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1*NSEC_PER_SEC),
                       dispatch_get_main_queue(), ^{ hookMTKView(); });
    }

    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.8 * NSEC_PER_SEC)),
        dispatch_get_main_queue(),
    ^{
        UIView* view = [UIApplication sharedApplication].keyWindow.rootViewController.view;
        if (view) initImGui(view);
    });
}

// ─── Constructor ─────────────────────────────────────────────────────────────
__attribute__((constructor))
static void rg_main() {
    LOG("RGAutoKill injected — com.chillyroom.soulknightprequel.gl");
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, 3*NSEC_PER_SEC),
        dispatch_get_main_queue(),
        ^{ tryInit(0); });
}
