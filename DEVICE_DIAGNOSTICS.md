# DEVICE_DIAGNOSTICS.md — Diagnostic P0 : Xiaomi 15T (GPU Mali-G720) — BOTWRecompAndroid

Date : 2026-11-17
Contexte : HEAD = `97e6d7d` (VRAM budget, Mali host import, shader warm-up — `export_driver_info_log()` n'existe PAS à cette révision ; il a été ajouté après le reset vers `97e6d7d`).

---

## 1. Sources de données Vulkan présentes dans le code (HEAD `97e6d7d`)

| Fichier | Ligne | Contenu |
|---|---|---|
| `runtime/nvn/vulkan/device.c` | ~342 | `[vk] gpu %u: %s (api %u.%u.%u)` — infos GPU par device |
| `runtime/nvn/vulkan/device.c` | ~346 | `[vk-cap] skipping %s: Vulkan %u.%u without what the renderer needs of 1.3` |
| `runtime/nvn/vulkan/device.c` | ~359 | `[vk-cap] gpu %u queue %u surface query failed: %d` |
| `runtime/nvn/vulkan/device.c` | ~364 | `[vk-cap] skipping %s: no graphics+compute queue can present to this surface` |
| `runtime/nvn/vulkan/device.c` | ~388 | `[vk] using %s (Vulkan %u.%u%s)` — device retenu |
| `runtime/nvn/vulkan/device.c` | ~402 | `[vk] gpu %u: %s (api %u.%u.%u)` (2ᵉ device) |
| `runtime/nvn/vulkan/device.c` | ~425 | `[vk] using %s` |
| `runtime/nvn/vulkan/device.c` | ~437-451 | `[vk] device fault: ...`, `[vk] device fault: %s`, `[vk] vendor: %s (code %#llx data %#llx)` |
| `runtime/nvn/vulkan/device.c` | ~471 | `[vk] device extension %s %u` — **extensions Vulkan** |
| `runtime/nvn/vulkan/device.c` | ~510-523 | compat: `VK_KHR_driver_properties_extension_name` (driver info), instance divisor, custom border colors |
| `runtime/nvn/vulkan/device.c` | ~553-613 | `[vk] Mali host import auto: gpu_gen=%d, driver_ver=r%d, ram=%d GB -> %s` ; `[vk] guest heap import left off (%s)` |
| `runtime/nvn/vulkan/device.c` | ~652 | `[vk-cap] memory export: fd %s, dma-buf %s, placed maps %s, host import %s, feedback loop layout %s` |
| `runtime/nvn/vulkan/device.c` | ~730-771 | `[vk] compat features: eds ... dynamic-rendering ... sync2 ...`, `[vk] no synchronization2...`, `[vk] no dynamic rendering...`, `[vk-cap] gpu %s vendor %#x device %#x api %u.%u.%u driver-id %u name %s info %s`, `[vk-cap] storage16 ...` |
| `runtime/nvn/vulkan/device.c` | ~924 | `[vk] Vulkan %u.%u device, %u extensions` |
| `runtime/nvn/vulkan/device.c` | ~47 | `[vk] %s` — **messages du validation layer** (severity warning+) |
| `runtime/nvn/vulkan/device.c` | ~71 | `[vk] validation layer unavailable (%d); continuing without` |
| `runtime/nvn/vulkan/exec.c`, `shaders.c`, `textures.c` | — | `[vk] swapchain: ...`, `[vk] cache ...`, VRAM budget logs (`[vk] ...`), warmup logs |

- **Package Android** : `com.botwcomp.android.test` (`android/botwrecompandroid/app/build.gradle.kts:21`).
- **Fonction `export_driver_info_log()`** : **n'existe pas** à cette révision (HEAD `97e6d7d`) — elle a été ajoutée dans le commit post-reset `386637b` puis corrigée (`d6cc6c2`, `db0010b`) avant le reset. Le diagnostic P0 se base donc sur les logs `[vk*]` + `dumpsys gfxinfo` + device props.

## 2. Script ADB complet (à exécuter depuis un PC externe compatible ARM64 + adb, PAS depuis cet hôte)

```bash
#!/bin/bash
# DEVICE_DIAGNOSTICS.sh — P0 diagnostics BOTWRecompAndroid / Xiaomi 15T (Mali-G720)
# À exécuter depuis un PC externe avec adb fonctionnel. NON depuis cet hôte ARM64.
PKG="com.botwcomp.android.test"

echo "=== [1] devices ==="
adb devices

echo "=== [2] device model / android version ==="
adb shell getprop ro.product.model
adb shell getprop ro.build.version.release
adb shell getprop ro.product.version
adb shell getprop ro.product.vendor
adb shell getprop ro.hardware

echo "=== [3] Vulkan info (logs [vk*] / vulkan / gpu / mali) ==="
adb logcat -d | grep -i -e "\[vk" -e "vulkan" -e "gpu" -e "mali"

echo "=== [4] validation layer / vulkan errors ==="
adb logcat -d -b all | grep -i -e "vulkan" -e "validation" -e "\[vk"

echo "=== [5] gfxinfo gpu ==="
adb shell dumpsys gfxinfo "$PKG" gpu

echo "=== [6] gfxinfo framestats ==="
adb shell dumpsys gfxinfo "$PKG" framestats

echo "=== [7] memory usage ==="
adb shell dumpsys meminfo "$PKG" | grep -e "com.botwcomp" -e "TOTAL"
PID=$(adb shell ps -A | grep -i "botwcomp" | awk 'NR==1 {print $2}')
adb shell cat /proc/$PID/status | grep -e VmRSS -e VmSize -e VmHWM -e VmData

echo "=== [8] thermal ==="
adb shell sh -c 'for z in /sys/class/thermal/thermal_zone*/temp; do printf "%s: %s\n" "$z" "$(cat "$z")"; done'

echo "=== [9] dmesg (mali/drm/vulkan) ==="
adb shell dmesg | grep -i -e mali -e drm -e vulkan

echo "=== [10] kernel logcat (alternative) ==="
adb logcat -b kernel | tail -50
```

## 3. Étapes exactes pour récupérer les données

1. PC externe avec platform-tools adb + accès USB/debugging au Xiaomi 15T.
2. `adb connect <ip_du_telephone>:5555` si le device n'est pas déjà listé.
3. Lancer BOTWRecompAndroid sur le device (ou le benchmark via `LARECOMP_BENCHMARK=<scene>`).
4. Exécuter le script ci-dessus.
5. Transmettre les sorties (sections 3, 5, 6, 7, 9, 10 principalement).

## 4. Commandes corrigées / limitations (points exigés)

- **`getprop ro.vendor.mali.*`** : **ne pas considéré comme universel** → remplacé par `ro.product.vendor`, `ro.hardware`, et les infos Vulkan réelles (`[vk] gpu %u: %s`, `[vk-cap] gpu %s vendor %#x device %#x ...`).
- **`gfxinfo` comme mesure directe d'utilisation GPU** : **non** → gfxinfo donne du frame/GPU timing et des stats d'usage ; pour l'usage GPU réel : `[vk-cap] gpu %s vendor %#x device %#x api %u.%u.%u driver-id %u name %s info %s` + `vk_gpuprof` (`LARECOMP_GPUPROF_AT`). gfxinfo ≠ mesure d'utilisation.
- **`dmesg`** : permissions insuffisantes possibles (root) → alternative : `adb logcat -b kernel` ; filtre Mali/DRM/Vulkan sur les deux.
- **Thermique** : `/sys/class/thermal/thermal_zone*/temp` lisible, mais les zones Mali/GPU peuvent ne pas être exposées → distinguer.
- **Données réellement accessibles vs indisponibles** : logs `[vk*]` (accessibles via logcat), gfxinfo (frametimes/GPU timing), meminfo (mémoire app), thermal (zones sysfs), dmesg (kernel, root).

## 5. Méthode alternative sans PC externe

- Si aucun PC externe n'est disponible : passer par l'application elle-même :
  - logs Vulkan : `[vk*]` à l'écran/log (stderr → logcat, overlay, fichier de log via settings/log) ;
  - FPS : overlay in-app (`show_fps`) / benchmark (`LARECOMP_BENCHMARK=<scene>`) ;
  - infos Mali : logs `[vk] Mali host import auto: ...` ;
  - GPU timing : `vk_gpuprof` (`LARECOMP_GPUPROF_AT`).
- Bas niveau (dumpsys/gfxinfo/dmesg/thermal) nécessite adb → impossible sans PC externe.

## 6. Limitations techniques (P0)

- Host ARM64 : pas de toolchain Android exécutable, pas d'adb → diagnostics device via PC externe.
- Vulkan info au runtime = logs du driver + gfxinfo ; pas de query exhaustive depuis host.
- Mali-G720 : driver r49p1 (auto host import : gpu_gen≥710, driver<r54, RAM≥11 GB).

## 7. Informations indispensables à transmettre pour la suite P0

1. `ro.product.model`, `ro.build.version.release`, `ro.product.version`, `ro.product.vendor`, `ro.hardware`.
2. GPU Mali : deviceName, vendorID, deviceID, driverVersion, apiVersion.
3. Extensions Vulkan + validation layer status.
4. Décision Mali host import (`[vk] Mali host import auto: ...`).
5. Frametimes : p50/p95/p99 (gfxinfo framestats / benchmark / overlay).
6. Mémoire : meminfo / RSS / HWM.
7. Thermique : zones + températures.
8. Logs : `[vk*]`, vulkan, validation, errors.

---

## 8. Recherches documentaires — Xiaomi 15T / Mali-G720 (2026-11-17)

Sources : Xiaomi Global (mi.com), MediaTek (mediatek.com), Arm (developer.arm.com / arm.com), Khronos Vulkan docs, GSMArena, NanoReview, DevCheck, PhoneArena, benchmark sites, Arm Mali docs, Arm GPU errata (SDEN-3735689), GitHub (MaliBridge, panvk-kbase-android, bionic-vulkan-wrapper, llama.cpp gist), Android Authority, RottenWiFi, AOSP thermal docs.

### 8.1 Caractéristiques confirmées officiellement
- **Xiaomi 15T** : SoC **MediaTek Dimensity 8400-Ultra** (MT6899, 4 nm TSMC), CPU 8× Cortex-A725 (1@3.25 GHz / 3@3.0 GHz / 4@2.1 GHz), NPU 880, GPU **Mali-G720 MC7**, GPU clock 1300 MHz, FLOPS ~2329.6 GFLOPS, **Vulkan 1.3**, OpenCL 3.0 FP, LPDDR5X (8533 Mbps), UFS 4.1, display 6.83" 1280×2772 AMOLED 120 Hz HDR10+/Dolby Vision 3200 nits, battery 5500 mAh, 67 W, 12 GB LPDDR5X, Android 15 (HyperOS 2.0), cooling 3D IceLoop + graphite sheet.

### 8.2 Caractéristiques documentées par sources techniques fiables
- **Mali-G720** (Arm, 5th Gen) : DVS, VRS, ASTC, AFBC 1.3.2, AFRC, Vulkan 1.3, OpenGL ES 1.1–3.2, OpenCL, L2 512 KB–2 MB, scalability 6–9 cores, CSF, deferred vertex shading, work scheduling; Immortalis-G720 MP7 vs Mali-G720 MC7 (ray tracing optional only for Immortalis); 5th Gen perf +15% vs G715, −40% memory bandwidth, 2× HDR.
- **Compiler Mali 5th Gen** : r53p0 (Mali Offline Compiler).
- **Errata Arm GPU** (SDEN-3735689 v3.0, 2025-03-06) : `VK_ARM_scheduling_controls` feature-query bug → drivers **r47p0–r49p2** impacted, **fixed r49p3 / r53p0**.
- **Khronos** : `VK_EXT_external_memory_host` (rev 1, ratified, #179, requires VK_KHR_external_memory or Vulkan 1.1, minImportedHostPointerAlignment) — **clé pour le host import Mali du BOTW** ; `VK_KHR_driver_properties` (promoted to core Vulkan 1.2).
- **Benchmarks** : Xiaomi 15T AnTuTu ≈1.86–1.93 M (GPU ≈570–582k), Geekbench GPU compute 11228, 3DMark Wild Life Extreme avg 21.37 FPS (stability 50.2%), Wild Life 77 FPS, Sling Shot maxed, Genshin max quality +60 FPS stable (low FPS ≥30 → most games playable).

### 8.3 À vérifier directement sur le téléphone
- Version exacte du pilote Mali (r49p1 ?), listing complet des extensions Vulkan réelles, FPS réels BOTW, thermal/GPU real behaviour.

### 8.4 Impossible à confirmer actuellement
- Résultats FPS réels BOTW sur ce matériel sans diagnostic.
- Firmware build numbers / détails pilote sans accès device.

## 9. Nouveau plan P0 adapté au Xiaomi 15T (après fusion des recherches)

1. **Étape 0** : préparer le device — Xiaomi 15T (Dimensity 8400-Ultra, Mali-G720 MC7, Android 15/16, HyperOS 2.0, 12 GB LPDDR5X, 5500 mAh, Mali-G720 + MFRC, +24% peak perf / −42% power vs G715).
2. **Étape 1** : diagnostic runtime via logs `[vk*]` (DEVICE_DIAGNOSTICS §2/3) → confirmer Vulkan 1.3, extensions, Mali host import auto, VRAM, compat features.
3. **Étape 2** : mesures Mali-G720 (perf GPU, thermique, fréquence) vs benchmarks documentés.
4. **Étape 3** : optimisations graphiques possibles pour Mali-G720 (DVS, VRS, ASTC/AFBC/AFRC, MSAA, bandwidth, MFRC, errata scheduling controls).

---

*Fin du document DEVICE_DIAGNOSTICS.md.*
