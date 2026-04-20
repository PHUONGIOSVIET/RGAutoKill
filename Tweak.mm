// RGAutoKill.dylib — SAFE VERSION
// Dùng transparent MTKView overlay, KHÔNG hook render loop game → không crash

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
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
#include "vendor/imgui/imgui_internal.h"   // để truy cập Windows list
#include "vendor/imgui/backends/imgui_impl_metal.h"
#include "backends/imgui_impl_uikit.h"
#include "bridge/il2cpp.h"
#include "bridge/lua_bind.h"
#include "script.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "[RG] " fmt, ##__VA_ARGS__)

static lua_State*          gLua        = nullptr;
static id<MTLDevice>       gDevice     = nil;
static id<MTLCommandQueue> gQueue      = nil;
static bool                gImGuiReady = false;

// ─── Gọi OnDraw Lua ──────────────────────────────────────────────────────────
static void callOnDraw() {
    if (!gLua) return;
    lua_getglobal(gLua, "OnDraw");
    if (!lua_isfunction(gLua, -1)) { lua_pop(gLua, 1); return; }
    if (lua_pcall(gLua, 0, 0, 0) != LUA_OK) {
        const char* e = lua_tostring(gLua, -1);
        LOG("OnDraw: %{public}s", e ? e : "?");
        lua_pop(gLua, 1);
    }
}

// ─── Overlay MTKView (trong suốt, đè lên game) ───────────────────────────────
@interface RGOverlay : MTKView <MTKViewDelegate>
@end

@implementation RGOverlay

- (instancetype)initWithFrame:(CGRect)f device:(id<MTLDevice>)dev {
    self = [super initWithFrame:f device:dev];
    if (self) {
        self.delegate                           = self;
        self.opaque                             = NO;
        self.backgroundColor                    = UIColor.clearColor;
        self.preferredFramesPerSecond           = 60;
        self.clearColor                         = MTLClearColorMake(0,0,0,0);
        self.colorPixelFormat                   = MTLPixelFormatBGRA8Unorm;
        self.depthStencilPixelFormat            = MTLPixelFormatInvalid;
        self.userInteractionEnabled             = YES;
        ((CAMetalLayer*)self.layer).opaque      = NO;
        ((CAMetalLayer*)self.layer).pixelFormat = MTLPixelFormatBGRA8Unorm;
    }
    return self;
}

// Render frame
- (void)drawInMTKView:(MTKView*)view {
    if (!gImGuiReady || !gLua) return;
    MTLRenderPassDescriptor* rpd = view.currentRenderPassDescriptor;
    if (!rpd) return;
    id<MTLCommandBuffer> cmd = [gQueue commandBuffer];
    if (!cmd) return;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    if (!enc) return;

    ImGui_ImplMetal_NewFrame(rpd);
    ImGui_ImplUIKit_NewFrame(view);
    ImGui::NewFrame();
    callOnDraw();
    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);

    [enc endEncoding];
    [cmd presentDrawable:view.currentDrawable];
    [cmd commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)size.width / self.contentScaleFactor,
                            (float)size.height / self.contentScaleFactor);
}

// Chỉ bắt touch khi điểm chạm nằm trong ImGui window, còn lại pass through game
- (UIView*)hitTest:(CGPoint)pt withEvent:(UIEvent*)e {
    if (!gImGuiReady) return nil;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return nil;
    // pt là point theo UIKit (đã scale). ImGui dùng logical pixel cùng scale -> so sánh trực tiếp.
    for (int i = 0; i < ctx->Windows.Size; i++) {
        ImGuiWindow* w = ctx->Windows[i];
        if (!w || !w->WasActive) continue;
        if (w->Flags & ImGuiWindowFlags_NoInputs) continue;
        if (w->Hidden) continue;
        if (pt.x >= w->Pos.x && pt.x < w->Pos.x + w->Size.x &&
            pt.y >= w->Pos.y && pt.y < w->Pos.y + w->Size.y) {
            return [super hitTest:pt withEvent:e];
        }
    }
    return nil;
}
- (void)touchesBegan:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e {
    ImGui_ImplUIKit_HandleTouch(t, self, true);
}
- (void)touchesMoved:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e {
    ImGui_ImplUIKit_HandleTouch(t, self, true);
}
- (void)touchesEnded:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e {
    ImGui_ImplUIKit_HandleTouch(t, self, false);
}
- (void)touchesCancelled:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e {
    ImGui_ImplUIKit_HandleTouch(t, self, false);
}
@end

// ─── Lấy window đang active ───────────────────────────────────────────────────
static UIWindow* getWindow() {
    for (UIWindowScene* sc in [UIApplication sharedApplication].connectedScenes) {
        if ([sc isKindOfClass:[UIWindowScene class]] &&
            sc.activationState == UISceneActivationStateForegroundActive) {
            for (UIWindow* w in ((UIWindowScene*)sc).windows)
                if (!w.isHidden) return w;
        }
    }
    return [UIApplication sharedApplication].windows.firstObject;
}

// ─── Setup overlay + ImGui ────────────────────────────────────────────────────
static void setupOverlay() {
    UIWindow* win = getWindow();
    if (!win) { LOG("No window"); return; }

    gDevice = MTLCreateSystemDefaultDevice();
    if (!gDevice) { LOG("No MTLDevice"); return; }
    gQueue = [gDevice newCommandQueue];

    CGRect frame = win.bounds;
    RGOverlay* ov = [[RGOverlay alloc] initWithFrame:frame device:gDevice];
    ov.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

    // Thêm lên trên cùng
    [win addSubview:ov];
    [win bringSubviewToFront:ov];

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale                    = 1.5f;
    io.ConfigFlags                       |= ImGuiConfigFlags_NoMouseCursorChange;
    io.DisplaySize                        = ImVec2(frame.size.width, frame.size.height);
    io.DisplayFramebufferScale            = ImVec2(win.screen.scale, win.screen.scale);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(1.3f);
    ImGui::GetStyle().WindowRounding   = 8.0f;
    ImGui::GetStyle().FrameRounding    = 4.0f;
    ImGui::GetStyle().Alpha            = 0.95f;

    ImGui_ImplMetal_Init(gDevice);
    ImGui_ImplUIKit_Init(ov);

    gImGuiReady = true;
    LOG("Overlay ready");
}

// ─── Init Lua ─────────────────────────────────────────────────────────────────
static void initLua() {
    gLua = luaL_newstate();
    luaL_openlibs(gLua);
    registerBindings(gLua);
    if (luaL_dostring(gLua, RG_SCRIPT) != LUA_OK) {
        const char* e = lua_tostring(gLua, -1);
        LOG("Script error: %{public}s", e ? e : "?");
        lua_pop(gLua, 1);
    } else {
        LOG("Script OK");
    }
}

// ─── Tìm IL2CPP base ─────────────────────────────────────────────────────────
static uintptr_t findIL2CppBase() {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char* n = _dyld_get_image_name(i);
        if (n && (strstr(n, "UnityFramework") || strstr(n, "GameAssembly"))) {
            return (uintptr_t)_dyld_get_image_header(i);
        }
    }
    return 0;
}

// ─── Khởi động tuần tự có retry ──────────────────────────────────────────────
static void tryInit(int attempt) {
    uintptr_t base = findIL2CppBase();
    if (base == 0 || !IL2CPP::get().init(base)) {
        LOG("IL2CPP not ready (attempt %d)", attempt);
        if (attempt < 10) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC),
                           dispatch_get_main_queue(), ^{ tryInit(attempt + 1); });
        }
        return;
    }
    LOG("IL2CPP OK (attempt %d)", attempt);

    // Lua trước
    initLua();

    // Overlay sau 0.5s
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5*NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        setupOverlay();
    });
}

// ─── Entry point ─────────────────────────────────────────────────────────────
__attribute__((constructor))
static void rg_main() {
    LOG("RGAutoKill injected — SKP");
    // Chờ 4s để game load xong rồi mới init
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4*NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{ tryInit(0); });
}
