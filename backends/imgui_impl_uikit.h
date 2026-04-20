#pragma once
#import <UIKit/UIKit.h>
#include "../vendor/imgui/imgui.h"

IMGUI_IMPL_API bool ImGui_ImplUIKit_Init(UIView* view);
IMGUI_IMPL_API void ImGui_ImplUIKit_Shutdown();
IMGUI_IMPL_API void ImGui_ImplUIKit_NewFrame(UIView* view);
IMGUI_IMPL_API bool ImGui_ImplUIKit_HandleTouch(NSSet<UITouch*>* touches, UIView* view, bool began);
