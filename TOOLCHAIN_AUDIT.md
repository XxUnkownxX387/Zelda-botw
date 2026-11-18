# TOOLCHAIN_AUDIT.md — BOTWRecompAndroid

Date: 2026-11-17
Environnement hôte : Alpine Linux 3.21 (aarch64), kernel musl, LLVM 16.0.6 (Alpine clang), LLVM 19.1.4 (binaire unversionné), GCC 14.2.0, Python 3.12.16, Git 2.47.3, BusyBox 1.37.0.
Cible Android : NDK r30.0.16248370 (r30), arm64-v8a ; driver Mali-G720 (Xiaomi 15T, r49p1) en priorité.

Contexte du projet audité : BOTWRecompAndroid convertit BOTW (BC1/BC3/BC5) → ASTC 4x4, backend Vulkan NVN,
runtime C/C++ dans `runtime/`, UI ImGui dans `runtime/ui/*.mm`, shaders SPIR-V, pipeline warm-up, Mali host
import, VRAM budget, half textures, shader compile threads.

---

## 1. Inventaire des outils et état de fonctionnement

### 1.1 Compilateurs et analyse statique

| Outil | Présent | Version | Arch | Hôte / Android | État | Analyse les fichiers BOTW ? | Utilité réelle | Limites Mali/Android | Commande concrète |
|---|---|---|---|---|---|---|---|---|
| clang | oui | 16.0.6 | aarch64 | hôte (Alpine) | OK | oui (runtime/*.c, runtime/nvn/**, runtime/hle/**, runtime/ui/** via `-x c` / `-x objective-c++`) | Syntaxe/type check pré-push (via `check.sh`) ; c'est l'outil de gate du projet | n/a Mali ; libc++ NDK requiert clang ≥ 19 → clang 16 hôte ne peut pas parser les headers NDK libc++ | `./check.sh <fichier.c>` ; `clang --target=aarch64-none-linux-android30 --sysroot=... -fsyntax-only f.c` |
| clang-tidy | oui | 16.0.6 | aarch64 | hôte | OK | oui (C/C++) | Static analysis (bug/correctness/style) sur le runtime | n/a | `clang-tidy --checks='-*,bugprone-*,performance-*' file.c` |
| gcc | oui | 14.2.0 | aarch64 | hôte | OK | partiel (C pur ; C++ headers incompatibles avec la toolchain Android) | Cross-check syntaxique C | n/a | `gcc -fsyntax-only file.c` |

### 1.2 Suite LLVM (profilage, symbolication, analyse de code)

| Outil | Version | Arch | Hôte/Android | État | Analyse BOTW ? | Utilité réelle | Limites | Commande concrète |
|---|---|---|---|---|---|---|---|---|
| llvm-profdata | 19.1.4 | aarch64 | hôte | OK | oui (fichiers .profraw générés par instrumentation clang) | Merge + stats des profils CPU/GPU-path | n/a | `llvm-profdata merge -o out.profdata a.profraw b.profraw && llvm-profdata show out.profdata` |
| llvm-profgen | 19.1.4 | aarch64 | hôte | OK | oui | Génération de programme d'exécution (profgen) à partir des stats | n/a | `llvm-profgen -o default_%m_%m.profdefault a.profdata` |
| llvm-cov | 19.1.4 | aarch64 | hôte | OK | oui | Coverage / lignes/branches exécutées (avec profraw) | n/a | `llvm-cov show build/ prog.profdata --instr-profile=prog.profdata` |
| llvm-symbolizer | 19.1.4 | aarch64 | hôte | OK | oui (libmain.so + DWARF) | Adresses → source pour crash (remplace addr2line partiellement) | n/a | `llvm-symbolizer -e ... ` / `llvm-symbolizer --obj ... addr` |
| llvm-objdump | 19.1.4 | aarch64 | hôte | OK | oui (arm64 disasm) | Analyse du code ARM64 généré (hot path) | n/a | `llvm-objdump -d --source libmain.so` |
| llvm-nm | 19.1.4 | aarch64 | hôte | OK | oui | Symboleres ELF | n/a | `llvm-nm -C libmain.so` |
| llvm-addr2line | 19.1.4 | aarch64 | hôte | OK | oui | Adresses → fichier+ligne (crash) | n/a | `llvm-addr2line -f -C -e libmain.so <addr>` |
| llvm-dwarfdump | 19.1.4 | aarch64 | hôte | OK | oui | Debug info (DWARF) | n/a | `llvm-dwarfdump -dwarf-depth=2 libmain.so` |
| llvm-readelf | 19.1.4 | aarch64 | hôte | OK | oui | Sections ELF | n/a | `llvm-readelf -S libmain.so` |
| llvm-mc | 19.1.4 | aarch64 | hôte | OK | oui | Machine code → asm IR | n/a | `llvm-mc -disassemble ...` |
| llvm-ar / llvm-ranlib / llvm-strip | 19.1.4 | aarch64 | hôte | OK | oui | Archive/debug info | n/a | `llvm-ar rcs libmain.a ...` |
| llvm-dis | 19.1.4 | aarch64 | hôte | OK | partiel (LLVM bitcode, pas SPIR-V) | Désassemblage IR → asm | n/a | `llvm-dis file.bc` |
| llvm-exegesis | 19.1.4 | aarch64 | hôte | OK | oui (CPU) | Estimation des fréquences d'instructions/statistiques | n/a | `llvm-exegesis -t cpu ...` |
| llvm-stress | 19.1.4 | aarch64 | hôte | OK | oui (compile-time bug hunt) | Stress testing du compilateur | n/a | `llvm-stress input.c --max-repeats=...` |
| llvm-split / llvm-lipo / llvm-ifs | 19.1.4 | aarch64 | hôte | OK | n/a (utilitaires IR, peu d'intérêt BOTW) | — | n/a | — |
| llvm-opt / llvm-opt-16 | **ABSENT** | — | — | — | — | — | — | — |

### 1.3 Profilage, debugging, mémoire, syscalls

| Outil | Version | Arch | Hôte/Android | État | Analyse BOTW ? | Utilité réelle | Limites | Commande concrète |
|---|---|---|---|---|---|---|---|---|
| perf | 6.11.3 | aarch64 | hôte | OK | oui (runtime/nvn/vulkan/exec.c, shaders.c, textures.c, device.c, hle/*) | CPU hotspots, GC-less, threads | Pas de visibilité GPU Mali ; Android nécessite root/perf daemon | `perf record -g -p <pid> && perf report` |
| simpleperf | 1.3.0-ish | aarch64 | hôte + Android (root) | OK | oui | Profilage CPU bas niveau pour Android (via perf) | GPU Mali non couvert | `simpleperf record -e cpu/cpu_cycles/ -p <pid>` |
| valgrind | 3.24.0 | aarch64 | hôte | OK | oui (runtime C pur) | Memcheck : fuites, accès invalides, UB | Android (binaire dex/ART) non supporté ; host Linux only | `valgrind --tool=memcheck --leak-check=full ./prog` |
| gdb | 15.2 | aarch64 | hôte | OK | oui (runtime C/C++) | Crash : backtrace DWARF | Android : NDK gdb est x86_64-hosted → inutilisable sur ce host aarch64 | `gdb -batch -ex run -ex bt ./prog` |
| strace | 4.25 | aarch64 | hôte | OK | oui | Syscalls (IO overlay, cache, romfs) | Android : pas de ptrace-like direct | `strace -f -e trace=openat,read,write ./prog` |
| hexdump | busybox | aarch64 | hôte | OK | oui (cache `cache/pipe_keys.bin`, fichiers settings) | Inspection binaire brute | n/a | `hexdump -C cache/pipe_keys.bin` |

### 1.4 Android NDK, SPIR-V, bibliothèques Vulkan

| Outil | Version | Arch | Hôte/Android | État | Analyse BOTW ? | Utilité réelle | Limites | Commande concrète |
|---|---|---|---|---|---|---|---|---|
| Android NDK r30 | 30.0.16248370 (r30) | x86_64-hosted | **ni l'un ni l'autre** (x86_64 host tools) | toolchain présent mais **non exécutable** sur host aarch64 | n/a (cross-compile) | Cross-compile arm64-v8a ; clang `aarch64-linux-android21…37` disponibles dans `toolchains/llvm/prebuilt/linux-x86_64/bin/` | ndk-build → "Unknown host CPU architecture: aarch64" ; ndk-gdb/ndk-stack/ndk-lldb = scripts 91 B ; shader-tools = x86_64 | doit tourner sur CI / x86_64 host |
| shader-tools (glslc, spirv-as/dis/opt/val/reduce/link) | NDK r30 | **x86_64** | hôte | **non exécutable** (x86_64 host bin) | oui (SPIR-V shaders) | SPIR-V validation/optimisation des shaders Vulkan | inutilisable sur ce host aarch64 | `spirv-val shader.spv` (CI) |
| shaderc (libshaderc) | source /opt/shaderc-src | aarch64 (headers) | hôte | OK (headers) | oui (shaders GLSL→SPIR-V) | Compiler SPIR-V | n/a | headers `/opt/shaderc-src/libshaderc/include` |
| SDL3 | 3.x | aarch64 | hôte (headers /opt/sdl3-src) | OK | oui (runtime SDL hooks) | — | — | — |
| Volk | /opt/volk-src | aarch64 | hôte (headers) | OK | oui (runtime vulkan) | — | — | — |
| VMA | 3.3.0 | aarch64 | hôte (headers) | OK | oui (runtime/vma.cpp) | Allocation GPU memory | — | — |

### 1.5 Environnement / scripts

| Outil | Version | Arch | Hôte/Android | État | Analyse BOTW ? | Utile pour | Limites | Commande |
|---|---|---|---|---|---|---|---|---|
| python3 | 3.12.16 | aarch64 | hôte | OK | oui (tools/gen, scripts) | — | — | — |
| git | 2.47.3 | aarch64 | hôte | OK | oui (historique) | — | — | — |
| busybox | 1.37.0 | aarch64 | hôte | OK | n/a | hexdump/ash | — | — |
| check.sh | script maison | — | hôte | OK | oui (gate pré-push) | — | — | `./check.sh` |

---

## 2. État de fonctionnement global

- **Fonctionnels sur l'environnement hôte** : clang 16, clang-tidy, gcc, LLVM 19 (profdata/profgen/cov/objdump/dwarfdump/readelf/mc/ar/ranlib/strip/dis/exegesis/stress/split/lipo/ifs), llvm-16-* (16.0.6), perf, simpleperf, valgrind, gdb, strace, hexdump, python3, git, busybox, check.sh.
- **Absents / inutilisables sur host aarch64** : llvm-opt (ni `llvm-opt` ni `llvm16-opt`), NDK r30 toolchain binaries (x86_64-hosted), ndk-build, ndk-gdb, ndk-stack, ndk-lldb, shader-tools binaries (glslc, spirv-*) (x86_64-hosted), NDK libc++ headers (nécessitent clang ≥ 19).
- **Partiels** : hexdump (busybox, option `-` rejetée → usage busybox), llvm-symbolizer/nm/strip/lipo/ifs (sortie version ambiguë), simpleperf (version string bâtie), shaderc (headers OK, pas de binaire local).
- **Conclusion inventaire** : l'environnement hôte permet l'analyse statique, de profilage CPU host-side, de debug mémoire (valgrind), de symbolication/analyse ARM64 (LLVM/gdb) — mais **pas** l'exécution Android, le cross-compile NDK, ni l'optimisation SPIR-V localement.

---

## 3. Utile pour BOTWRecompAndroid — synthèse

| Domaine | Outil(s) pertinent(s) | Cible(s) BOTW |
|---|---|---|
| Compilation / gate | clang 16 + check.sh | runtime/*.c, runtime/nvn/**, runtime/hle/**, runtime/ui/*.mm/.c |
| Crash / stabilité | gdb, llvm-addr2line, llvm-dwarfdump, llvm-readelf, llvm-nm, llvm-objdump, llvm-mc | libmain.so (artifact CI), runtime/nvn/vulkan/*.c, runtime/hle/*.c |
| CPU perf / shaders compile | perf, simpleperf, valgrind, llvm-profdata/profgen/cov | runtime/nvn/vulkan/exec.c, shaders.c, textures.c, device.c, hle/*, fnprof.c |
| Static analysis | clang-tidy, clang | runtime/**/*.c, *.cpp, *.mm |
| SPIR-V / shaders | shader-tools (NDK), shaderc | bc3_astc.c, bc1_astc.c, shaders SPIR-V du jeu |

---

## 4. Commandes recommandées (issues de l'audit)

1. `./check.sh <f>` — gate syntaxique (clang 16 + sysroot NDK + deps /opt).
2. `llvm-addr2line -f -C -e libmain.so <addr>` — crash symbolication (artifact CI).
3. `perf record -g -p <pid> && perf report --stdio` — hotspots CPU (game thread, workers shader).
4. `valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all ./prog` — erreurs mémoire.
5. `llvm-profdata merge -o m.profdata *.profraw && llvm-profdata show m.profdata` — stats profils.
6. `llvm-cov show ... --instr-profile=...` — couverture/branches.
7. `gdb -batch -ex 'run' -ex 'bt full' ./prog` — debug natif.
8. NDK shaders : `glslc`, `spirv-val`, `spirv-opt`, `spirv-dis` (sur CI / x86_64 host) — temps de compilation shaders.

---

## 5. Optimisations réalisables avec ces outils (priorité Mali-G720)

1. **Hotspots CPU** (perf/LLVM-profdata) → réduire temps game thread ; déjà ciblé par les features (sync_lag, framepace).
2. **Temps de compilation shaders** → instrumentation perf sur worker threads + cache pipeline (déjà en place) ; SPIR-V opt (spirv-opt) via NDK shader-tools.
3. **Mémoire** → VMA (runtime/vma.cpp) + valgrind (host builds) ; VRAM budget / half textures (fait).
4. **Stabilité** → valgrind memcheck + gdb + LLVM symbolication (fait partiellement).
5. **Static** → clang-tidy.

## 6. Limitations techniques (Mali / Android / ARM64 / Vulkan)

- Host aarch64 : NDK toolchain x86_64-hosted inutilisable → cross-compile/debug Android impossible localement.
- NDK libc++ exige clang ≥ 19 → clang 16 hôte incomplet pour les headers C++ Android.
- Valgrind/gdb/perf : host Linux only ; Android = device/root.
- Mali : pas d'outil hôte ne profile le GPU Mali directement ; driver-side (driverInfo, gating Mali) ; Mali-G720 (r49p1).
- SPIR-V : outils NDK en x86_64 → non exécutables ici ; optimisation shader via spirv-opt à faire sur CI/x86_64 host.

## 7. Plan d'exploitation priorisé (Xiaomi 15T Mali-G720)

1. **P0 — Validation runtime sur device** (Mali-G720, r49p1) : warm-up shaders, cache pipeline, Mali host import gating, VRAM budget, half textures (features livrées, à valider).
2. **P1 — Profilage CPU sur device** (simpleperf/perf avec root ou CI) : game thread + workers compile shaders; mesures avant/après.
3. **P2 — Temps de compilation shaders** : spirv-opt / variante shaders / cache (CI shader-tools).
4. **P3 — Mémoire/CPU static** : valgrind (builds host), clang-tidy, LLVM profiling.

## 8. Contraintes de mesure

- Aucun benchmark ne sera fabriqué ; mesures comparatives requièrent exécution réelle (device Mali-G720 ou builds host).
- Un outil n'est considéré comme « utilisé » que s'il s'est exécuté et produit un résultat confirmé.

## 9. Mise à jour : contraintes de toolchain imposées (2026-11-17)

Contraintes communiquées puis intégrées à la stratégie de travail :

1. LLVM opt / llvm16-opt : absents. Ne pas les utiliser. → Impact : pas de passe d'optimisation LLVM locale ; les optimisations SPIR-V passent par les outils NDK shader-tools (glslc / spirv-*) qui sont x86_64-hosted et donc non exécutables sur cet hôte ARM64.

2. NDK r30 (x86_64) : incompatible avec l'hôte aarch64. Ne pas tenter de lancer ndk-build, ndk-gdb, ndk-stack, ndk-lldb ou shader-tools provenant de cette distribution. Vérifications faites : `readelf -h .../clang-21` → Machine: Advanced Micro Devices X86-64 ; ndk-build → « Unknown host CPU architecture: aarch64 » ; ndk-gdb/ndk-stack/ndk-lldb = wrappers shell vers binaires x86_64 ; shader-tools = binaires x86_64.

3. NDK libc++ : les headers C++ Android nécessitent clang >= 19. Clang 16 (hôte Alpine) ne doit pas prétendre réaliser une compilation Android complète avec ces headers. → Checks syntaxiques C++ Android via clang 16 + sysroot NDK sont donc partiels (libc++ incompatible) ; les .mm C++ du projet ne peuvent pas être vérifiés proprement avec cet hôte.

4. Valgrind / GDB / perf : ne pas considérer comme des outils de débogage Android directement opérationnels depuis cet hôte. Distinguer outils hôte (valgrind, gdb, perf, strace sur binaires Linux aarch64) et outils device/CI (simpleperf, NDK gdb, perf daemon Android).

5. Ne jamais annoncer une compilation Android réussie si la toolchain réellement utilisée n'est pas compatible.

### 9.1 Recherche d'une toolchain Android compatible ARM64

Recherche faite : seuls des binaires X86-64 existent dans `/opt/android-ndk` (prebuilt/linux-x86_64 : clang-21 = X86-64, shader-tools = x86_64, ndk-build = x86_64) ; aucun binaire Android aarch64 exécutable et aucun qemu-aarch64 n'a été trouvé. **Conclusion : aucune toolchain Android exécutable sur l'hôte ARM64 n'est disponible.** Pour une compilation Android réelle : CI GitHub Actions (ndkVersion 30.0.16248370) ou machine x86_64.

### 9.2 Outils préférés maintenus (compatibles ARM64 hôte, exécutés et vérifiés)

clang 16, GCC, clang-tidy, simpleperf (hôte/Android via perf), llvm-profdata, llvm-cov, llvm-objdump, llvm-symbolizer, llvm-addr2line, llvm-readelf, llvm-nm, Python 3, Git, check.sh, perf, valgrind, gdb, strace, LLVM 19 (outils unversionnés 19.1.4), aliases LLVM 16 (16.0.6).

### 9.3 Commandes vérifiées lors de cette mise à jour (logs conservés)

- `clang --version` → OK (exit 0, clang 16.0.6, aarch64-alpine-linux-musl) — log: /tmp/tcaudit/verify1.log.
- `clang-tidy --version` → OK (exit 0, LLVM 16.0.6).
- `llvm-profdata --version` → OK (exit 0, LLVM 19.1.4).
- `llvm-cov --version` → OK (exit 0, LLVM 19.1.4).
- `llvm-objdump --version` → OK (exit 0, LLVM 19.1.4).
- `llvm-symbolizer --version` → OK (exit 0, LLVM 19.1.4).
- `llvm-addr2line -f -C -e /usr/bin/env 0x0` → OK (exit 0, `?? ??:0`).
- `llvm-readelf -h /usr/bin/env` → OK (exit 0, ELF64 AArch64, DYN).
- `llvm-nm /usr/bin/env` → OK (exit 0, « no symbols »).
- Logs conservés : `/tmp/tcaudit/verify1.log`.

### 9.4 Impact sur la stratégie et les plans d'optimisation

- Les checks syntaxiques C/C++ via clang 16 + sysroot NDK restent valides pour le C (runtime C pur) — c'est le mode de vérification utilisé par check.sh (exit 0 confirmé).
- Les .mm C++ et les headers libc++ Android ne peuvent plus être vérifiés proprement avec clang 16 → toute affirmation de compile C++ Android complète est à réserver à la CI (clang 21 + NDK r30) ou à un hôte x86_64.
- Profilage CPU / memcheck / disasm ARM64 / symbolication : utilisables sur hôte avec les outils compatibles (exécutés, exit 0).
- Étapes device-side (profiling simpleperf, gdb Android, spirv/glslc) : réservées à CI/device ; non exécutables depuis cet hôte ARM64.

---

*Fin du rapport TOOLCHAIN_AUDIT.md.*
