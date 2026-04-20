#!/usr/bin/env bash
# Build RGAutoKill.dylib cho iOS ARM64
# Chạy trên macOS có Xcode (hoặc GitHub Actions macos-latest)
set -e

OUT="RGAutoKill.dylib"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CXX=$(xcrun --sdk iphoneos --find clang++)
AR=$(xcrun --sdk iphoneos --find ar)

IMGUI="./vendor/imgui"
LUA_INC="./vendor/lua/include"
LUA_LIB="./vendor/lua/lib/liblua5.4.a"

BASE_FLAGS="-arch arm64 -isysroot $SDK -miphoneos-version-min=14.0 -O2"
CXX_FLAGS="$BASE_FLAGS -std=c++17 -fobjc-arc \
  -I$IMGUI \
  -I$IMGUI/backends \
  -I$LUA_INC \
  -I./vendor \
  -I./bridge \
  -I./backends"

mkdir -p .build

echo "── Compiling ImGui core ──"
IMGUI_SRCS=(
  "$IMGUI/imgui.cpp"
  "$IMGUI/imgui_draw.cpp"
  "$IMGUI/imgui_tables.cpp"
  "$IMGUI/imgui_widgets.cpp"
)
OBJS=()
for src in "${IMGUI_SRCS[@]}"; do
  obj=".build/$(basename ${src%.*}).o"
  $CXX $CXX_FLAGS -c "$src" -o "$obj"
  OBJS+=("$obj")
  echo "  OK $obj"
done

echo "── Compiling ImGui Metal backend ──"
$CXX $CXX_FLAGS \
  -framework Metal -framework MetalKit \
  -c "$IMGUI/backends/imgui_impl_metal.mm" -o .build/imgui_impl_metal.o
OBJS+=(".build/imgui_impl_metal.o")

echo "── Compiling UIKit backend ──"
$CXX $CXX_FLAGS \
  -framework UIKit \
  -c "./backends/imgui_impl_uikit.mm" -o .build/imgui_impl_uikit.o
OBJS+=(".build/imgui_impl_uikit.o")

echo "── Compiling Tweak.mm ──"
$CXX $CXX_FLAGS \
  -framework Metal -framework MetalKit \
  -framework UIKit -framework Foundation \
  -c Tweak.mm -o .build/Tweak.o
OBJS+=(".build/Tweak.o")

echo "── Linking $OUT ──"
$CXX \
  -arch arm64 \
  -isysroot $SDK \
  -miphoneos-version-min=14.0 \
  -dynamiclib \
  -install_name "@rpath/$OUT" \
  -std=c++17 \
  -fobjc-arc \
  -lc++ \
  -framework Metal \
  -framework MetalKit \
  -framework UIKit \
  -framework Foundation \
  -framework QuartzCore \
  "$LUA_LIB" \
  "${OBJS[@]}" \
  -o "$OUT"

echo "── Ad-hoc sign ──"
codesign -f -s - "$OUT"

SIZE=$(du -sh "$OUT" | cut -f1)
echo ""
echo "✅ Build thành công!"
echo "   File : $OUT"
echo "   Size : $SIZE"
echo ""
echo "Inject: copy $OUT vào TrollStore hoặc inject tool"
