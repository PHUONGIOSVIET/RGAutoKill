// imgui_impl_uikit.mm — UIKit touch + frame backend for ImGui (iOS)

#include "imgui_impl_uikit.h"
#import <QuartzCore/QuartzCore.h>
#include "../vendor/imgui/imgui.h"

static UIView*      g_View      = nil;
static CFTimeInterval g_LastTime = 0.0;

bool ImGui_ImplUIKit_Init(UIView* view) {
    g_View     = view;
    g_LastTime = CACurrentMediaTime();
    return true;
}

void ImGui_ImplUIKit_Shutdown() {
    g_View = nil;
}

void ImGui_ImplUIKit_NewFrame(UIView* view) {
    ImGuiIO& io = ImGui::GetIO();

    // Display size (points, not pixels)
    CGRect bounds = view.bounds;
    io.DisplaySize = ImVec2((float)bounds.size.width, (float)bounds.size.height);

    // Framebuffer scale (retina)
    float scale = (float)view.contentScaleFactor;
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    // Delta time
    CFTimeInterval now = CACurrentMediaTime();
    io.DeltaTime = g_LastTime > 0 ? (float)(now - g_LastTime) : (1.0f / 60.0f);
    if (io.DeltaTime <= 0) io.DeltaTime = 1.0f / 60.0f;
    g_LastTime = now;
}

// Gọi từ touchesBegan/touchesMoved/touchesEnded
bool ImGui_ImplUIKit_HandleTouch(NSSet<UITouch*>* touches, UIView* view, bool began) {
    ImGuiIO& io = ImGui::GetIO();
    UITouch* touch = [touches anyObject];
    if (!touch) return false;

    CGPoint pt = [touch locationInView:view];
    io.AddMousePosEvent((float)pt.x, (float)pt.y);
    io.AddMouseButtonEvent(0, began);

    return io.WantCaptureMouse;
}
