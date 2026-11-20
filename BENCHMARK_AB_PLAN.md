# A/B Test Plan: Xiaomi 15T (Mali-G720 Immortalis Gen4)

## Device Profile
- **Device**: Xiaomi 15T
- **GPU**: Mali-G720 Immortalis Gen4 (vendorID 0x13B5)
- **Driver**: r49p1 (Mali driver major=49, minor=1)
- **RAM**: 12 GB LPDDR5X
- **Android**: 14+ (API 34+)
- **Game**: BOTW 1.9.0

## Test Configuration
- **Save file**: Identical working save (post-Great Plateau, same position/inventory)
- **Route**: Fixed camera path per scene (plateau, hateno-lab, hateno-village)
- **Brightness**: Fixed at 50% (auto-brightness off)
- **Thermal**: Start from cold soak (30 min idle at 25°C ambient)
- **Repetitions**: 3 per variant + 1 long session (20 min)
- **Metrics collected**: JSON report with all extended fields

---

## Series 1: Internal Resolution Scale
| Variant | res_mode | Description |
|---------|----------|-------------|
| A | 1 (1x) | 900p base |
| B | 2 (2x) | 1800p |
| C | 3 (3x) | 2700p |

**Decision criteria**: GPU time scaling, memory pressure, PSS growth
- Keep if: 2x < 15% GPU increase, 3x < 30% GPU increase
- Experimental if: 2x OK but 3x marginal
- Remove if: any exceeds thermal cap > 50% of window

---

## Series 2: Upscaler
| Variant | upscaler | Description |
|---------|----------|-------------|
| A | 0 (FSR1) | 1 pass, fast |
| B | 1 (SGSR1) | 1 pass, sharper |
| C | 2 (FSR2) | 2 pass, temporal |

**Decision criteria**: GPU ms, visual quality (manual review), VRAM
- Keep best quality/performance ratio
- Remove if > 2ms GPU overhead vs FSR1

---

## Series 3: Anti-Aliasing
| Variant | antialias | Description |
|---------|-----------|-------------|
| A | 0 (off) | Baseline |
| B | 1 (MSAA2x) | 2x MSAA |
| C | 2 (VRS+AA) | VRS 2x2 + AA |

**Decision criteria**: GPU ms, visual aliasing reduction
- Keep if VRS+AA < 5% GPU vs off
- Experimental if MSAA2x > 10% GPU

---

## Series 4: Shader Workers
| Variant | shader_threads | Description |
|---------|----------------|-------------|
| A | 2 | Conservative |
| B | 3 | Balanced |
| C | 4 | Aggressive |

**Decision criteria**: Compile spikes, stutter frames, total warmup time
- Keep if 4 workers reduce compile stutter > 30% vs 2
- Remove if 4 workers increase peak CPU > 15%

---

## Series 5: ADPF
| Variant | adpf_mode | Description |
|---------|-----------|-------------|
| A | -1 (Auto) | On for API 33+ |
| B | 0 (Off) | Disabled |
| C | 1 (On) | Forced on |

**Decision criteria**: Thermal headroom, sustained FPS, throttling behavior
- Keep Auto if headroom > 0.9 sustained
- Experimental if forced On improves 1% lows
- Remove if no measurable difference

---

## Series 6: Memory Strategy
| Variant | mem_strategy | Description |
|---------|--------------|-------------|
| A | 0 (CPU_COMPAT) | Copies only |
| B | 1 (HOST_IMPORT) | Host import |
| C | 4 (AUTO) | Auto-validated |

**Decision criteria**: GPU ms, PSS, copies/frame, OOM events
- Keep HOST_IMPORT if GPU < CPU_COMPAT and PSS stable
- Experimental if AUTO validates and matches HOST_IMPORT
- Remove if copies/frame > 1000 or OOM

---

## Long Session (20 min)
- **Variant**: Best from Series 1-6 combined
- **Metrics**: Thermal drift, PSS growth, compile frequency, FPS stability
- **Pass criteria**: No thermal cap after 10 min, PSS delta < 100 MB, 1% low FPS > 25

---

## Measurement Protocol
Each run produces `<scene>.out/report.json` with:
- `passport.capabilities`: All Vulkan features actually enabled
- `results[].cache_state`: "cold"/"warm" SPIR-V + pipeline cache
- `results[].present_hz`, `frame_ms`, `gpu_ms`, `cpu_ms`, `render_ms` p50/p95/p99
- `results[].compiles`, `skipped_draws`, `skipped_building`
- `results[].loads`, `stores` (null + reason if unavailable)
- `results[].submissions`, `image_copies`, `buffer_copies`
- `results[].pss_mb_start/end`, `battery_temp`, `soc_temp`
- `results[].memory_pressure_level`, `memory_pressure_low`
- `results[].target_fps` vs `actual_fps_mode`, `target_res_mode` vs `actual_res_mode`

Missing measurements → `null` + `"_reason"` field explaining why.

---

## Decision Matrix
| Outcome | Action |
|---------|--------|
| Clear win (all metrics better, no regressions) | **Keep** - merge to stable |
| Trade-off (better GPU but higher PSS, or vice versa) | **Experimental** - behind flag, more testing |
| No measurable difference OR regression | **Remove** - delete code/config |

---

## Execution Order
1. Baseline (current stable defaults) - 3 reps + long
2. Series 1-6 (one variable each) - 3 reps each
3. Combined best - 3 reps + long
4. Document findings, apply decision matrix

---

## Automation
```bash
# On device (adb shell):
LARECOMP_BENCHMARK=/sdcard/bench/scenes.txt \
LARECOMP_SAVE_DIR=/sdcard/bench/save \
LARECOMP_SETTINGS=/sdcard/bench/settings.ini \
am start -n com.botwrecomp.android/... --es env "LARECOMP_BENCHMARK=..."
```

Results pulled via `adb pull /sdcard/bench/scenes.txt.out/report.json`

---

## Notes
- No frequency manipulation, no thermal protection overrides
- Swappy/Choreographer only tested in A/B with concurrent pacing disabled
- Game time logic (VFR) untouched; only present pacing modified
