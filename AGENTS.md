# AGENTS.md — Règles pour tous les travaux futurs sur ce projet

## 1. Compilation avant tout push
- Compiler les fichiers modifiés avec :
  ```bash
  export ANDROID_NDK_HOME=/opt/android-ndk
  SYSROOT="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
  R=/public/Zelda-botw
  DEPS="$R/android/deps/out"
  IMGUI="$R/third_party/imgui"
  BOTW_GEN_DIR="$R/android/botwrecompandroid/app/build/generated/botwrecompandroid/inc"

  clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" \
    -I"$R/runtime" -I"$R/runtime/nvn" -I"$IMGUI" -I"$IMGUI/backends" \
    -I"$DEPS/include" -I"$DEPS/volk" -I"$BOTW_GEN_DIR" \
    -I"$R/third_party/smaa/Textures" -I"$R/android/common" \
    -DVK_NO_PROTOTYPES -DVK_USE_PLATFORM_ANDROID_KHR -DIMGUI_IMPL_VULKAN_USE_VOLK \
    -DVMA_STATIC_VULKAN_FUNCTIONS=0 -DVMA_DYNAMIC_VULKAN_FUNCTIONS=1 \
    -fsyntax-only <fichier.c>
  ```
  **Zéro erreur obligatoire**. Corriger avant de push.
  - Note : les headers SDL3/VMA/FFmpeg sont dans `$DEPS/include` (buildés par `android/deps/build_deps.sh`).
  - Pour les fichiers n'utilisant pas SDL3 (ex: `bc3_astc.c`), la commande passe sans les deps.

## 2. Nouveaux fichiers .c
- Ajouter le fichier au `CMakeLists.txt` utilisé par la build Android (`android/botwrecompandroid/app/src/main/cpp/CMakeLists.txt`).
- Vérifier qu'il n'y a **aucun symbole non défini** au link.

## 3. Variables globales
- **Ne jamais déclarer deux fois la même variable globale**.
- Réutiliser les types (`struct`) existants au lieu de créer des structs anonymes redondants.

## 4. Git discipline
- Avant de modifier : afficher `git diff --stat`.
- Ne supprimer **aucun fichier** sans validation explicite.

## 5. Débogage des crashes
- Pour tout crash : résoudre les adresses avec `llvm-addr2line -f -C -e libmain.so <addr>` sur le `.so` non strippé (artifact CI) **avant** de proposer un correctif.

## 6. Environnement
- NDK version : `30.0.16248370` (r30) — correspond à `build.gradle.kts` et CI.
- `ANDROID_NDK_HOME=/opt/android-ndk` (ajouté à `~/.bashrc`).
- clang/clang-tidy/cppcheck/llvm-addr2line disponibles localement (LLVM 16.0.6).

## 7. Vérification automatisée avant push
- **Lance `./check.sh` avant chaque push.**
- Il compile en `-fsyntax-only` (avec `--sysroot`, les mêmes `-I` et `-D` que le CMake, y compris SDL3 via `$DEPS/include`).
- Il vérifie tous les `.c`/`.cpp` listés par `git diff --name-only HEAD` et `--cached`.
- Si un fichier ne peut pas être vérifié (ex: headers SDL3 manquants car deps non buildés localement), il l'indique explicitement (`SKIPPED: SDL3 headers not built...`), **ne dis jamais que c'est normal**.
- Exit code non nul = échec, corriger avant de push.