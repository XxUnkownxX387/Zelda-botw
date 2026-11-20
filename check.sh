#!/bin/bash
# check.sh - syntax check for modified C files using clang with Android NDK sysroot
# Usage: ./check.sh [files...] (if no args, checks git diff --name-only HEAD and --cached)

set -euo pipefail

export ANDROID_NDK_HOME=/opt/android-ndk
SYSROOT="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
R="/public/Zelda-botw"
DEPS="$R/android/deps/out"
SDL3_HEADERS="/opt/sdl3-src/include"
VOLK_HEADERS="/opt/volk-src"
VMA_HEADERS="/opt/vma-src/include"
SHADERC_HEADERS="/opt/shaderc-src/libshaderc/include"
IMGUI="$R/third_party/imgui"
BOTW_GEN_DIR="$R/android/botwrecompandroid/app/build/generated/botwrecompandroid/inc"

# Base include paths and defines from CMakeLists.txt
INCLUDES=(
  -I"$R/runtime"
  -I"$R/runtime/nvn"
  -I"$IMGUI"
  -I"$IMGUI/backends"
  -I"$DEPS/include"
  -I"$SDL3_HEADERS"
  -I"$VOLK_HEADERS"
  -I"$VMA_HEADERS"
  -I"$SHADERC_HEADERS"
  -I"$BOTW_GEN_DIR"
  -I"$R/third_party/smaa/Textures"
  -I"$R/android/common"
)
DEFINES=(
  -DVK_NO_PROTOTYPES
  -DVK_USE_PLATFORM_ANDROID_KHR
  -DIMGUI_IMPL_VULKAN_USE_VOLK
  -DVMA_STATIC_VULKAN_FUNCTIONS=0
  -DVMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

# Get files to check
FILES=()
if [ $# -gt 0 ]; then
  FILES=("$@")
else
  mapfile -t FILES < <(git diff --name-only HEAD; git diff --name-only --cached)
  # Deduplicate
  FILES=($(printf "%s\n" "${FILES[@]}" | sort -u))
fi

# Filter to .c and .cpp files
CHECK_FILES=()
for f in "${FILES[@]}"; do
  case "$f" in
    *.c|*.cpp) 
      # Skip deleted files
      [ -f "$f" ] && CHECK_FILES+=("$f") ;;
  esac
done

if [ ${#CHECK_FILES[@]} -eq 0 ]; then
  echo "No C/C++ files to check"
  exit 0
fi

echo "Checking ${#CHECK_FILES[@]} file(s)..."

FAILED=0
MISSING_HEADERS=()
for f in "${CHECK_FILES[@]}"; do
  echo -n "  $f: "
  # Use clang exit code (0=OK, non-zero=error)
  if clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" \
      "${INCLUDES[@]}" "${DEFINES[@]}" \
      -fsyntax-only "$f" 2>&1 >/dev/null; then
    echo "OK"
  else
    echo "FAILED"
    # Capture stderr to find missing headers
    ERRORS=$(clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" \
      "${INCLUDES[@]}" "${DEFINES[@]}" \
      -fsyntax-only "$f" 2>&1)
    echo "$ERRORS" | head -20
    # Extract missing header names
    echo "$ERRORS" | grep -oE "fatal error: '[^']+'" | sed "s/fatal error: '//; s/'//" | while read hdr; do
      if [[ ! " ${MISSING_HEADERS[@]} " =~ " ${hdr} " ]]; then
        MISSING_HEADERS+=("$hdr")
      fi
    done
    FAILED=1
  fi
done

if [ ${#MISSING_HEADERS[@]} -gt 0 ]; then
  echo "MISSING HEADERS (not in sysroot, DEPS, or SDL3): ${MISSING_HEADERS[*]}"
fi

if [ $FAILED -eq 1 ]; then
  echo "ERROR: syntax check failed"
  exit 1
else
  echo "All OK"
  exit 0
fi
