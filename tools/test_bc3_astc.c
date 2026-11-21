// Test of runtime/nvn/bc1_astc.c through BC3 blocks (BC3 -> ASTC 4x4: colour line at bytes 8..15 + alpha at 0..7):
// random BC3 blocks decoded by bcdec (bcdec_bc3) and converted to ASTC 4x4 then decoded by astc.c (bit-exact with
// ARM's decoder) - the largest difference, LDR and sRGB. Equivalent to test_bc1_astc (colours + alpha), for the
// BC3 path the GPU took on Mali (BC3 textures decoded to ASTC instead of BC).
// Alpha modes covered: flat (alpha0 == alpha1), descending (alpha0 > alpha1), ascending (alpha0 < alpha1).
// The bcdec interpolation branches are selected by the alpha endpoint order; the ASTC 4-colour single plane
// encodes flat alpha=255. The test uses random alpha indices for all 16 texels per block and compares
// RGB against BC3 reference; alpha is reported (ASTC flat vs BC3 per-texel).
// Build: cc -O2 -I runtime/nvn tools/test_bc3_astc.c runtime/nvn/bc1_astc.c runtime/nvn/astc.c -o build/test_bc3_astc
//   ./build/test_bc3_astc [blocks]
#include "../runtime/nvn/astc.h"
#include "../runtime/nvn/bc1_astc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../runtime/nvn/bcdec.h"

// Opacity check: decode all 16 alpha values per block via bcdec, verify all == 255.
// Mirrors runtime/nvn/vulkan/textures.c:bc3_is_opaque()
static bool bc3_is_opaque_block(const uint8_t bc[16]) {
    uint8_t ref[64];
    bcdec_bc3(bc, ref, 16);
    for (int t = 0; t < 16; t++) {
        if (ref[4 * t + 3] != 255) return false;
    }
    return true;
}

static uint64_t g_rng = 1;
static uint64_t rnd(void) {  // splitmix64
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Build an opaque BC3 block: alpha0=alpha1=255, all indices=7 (alpha[7]=255 in flat mode)
static void build_opaque_block(uint8_t bc[16], uint32_t c0, uint32_t c1, uint32_t idx) {
    bc[0] = 255; bc[1] = 255;  // alpha0=alpha1=255
    // All 16 texels use index 7 -> alpha[7] = 255
    uint64_t indices = 0;
    for (int t = 0; t < 16; t++) indices |= (uint64_t)7 << (3 * t);
    bc[2] = (uint8_t)(indices & 0xFF);
    bc[3] = (uint8_t)((indices >> 8) & 0xFF);
    bc[4] = (uint8_t)((indices >> 16) & 0xFF);
    bc[5] = (uint8_t)((indices >> 24) & 0xFF);
    bc[6] = (uint8_t)((indices >> 32) & 0xFF);
    bc[7] = (uint8_t)((indices >> 40) & 0xFF);
    bc[8] = (uint8_t)(c0 & 0xFF); bc[9] = (uint8_t)(c0 >> 8);
    bc[10] = (uint8_t)(c1 & 0xFF); bc[11] = (uint8_t)(c1 >> 8);
    bc[12] = (uint8_t)idx; bc[13] = (uint8_t)(idx >> 8);
    bc[14] = (uint8_t)(idx >> 16); bc[15] = (uint8_t)(idx >> 24);
}

// Build a non-opaque BC3 block: random alpha endpoints/indices
static void build_nonopaque_block(uint8_t bc[16], uint32_t c0, uint32_t c1, uint32_t idx) {
    uint32_t a0 = rnd() % 256;
    uint32_t a1 = rnd() % 256;
    if (a0 == a1) a1 = (a0 + 1) % 256;  // ensure different
    uint64_t indices = 0;
    for (int t = 0; t < 16; t++) indices |= (uint64_t)(rnd() & 7) << (3 * t);
    bc[0] = (uint8_t)a0; bc[1] = (uint8_t)a1;
    bc[2] = (uint8_t)(indices & 0xFF);
    bc[3] = (uint8_t)((indices >> 8) & 0xFF);
    bc[4] = (uint8_t)((indices >> 16) & 0xFF);
    bc[5] = (uint8_t)((indices >> 24) & 0xFF);
    bc[6] = (uint8_t)((indices >> 32) & 0xFF);
    bc[7] = (uint8_t)((indices >> 40) & 0xFF);
    bc[8] = (uint8_t)(c0 & 0xFF); bc[9] = (uint8_t)(c0 >> 8);
    bc[10] = (uint8_t)(c1 & 0xFF); bc[11] = (uint8_t)(c1 >> 8);
    bc[12] = (uint8_t)idx; bc[13] = (uint8_t)(idx >> 8);
    bc[14] = (uint8_t)(idx >> 16); bc[15] = (uint8_t)(idx >> 24);
}

int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 10000;
    const char *kind[5] = {"opaque (all alpha=255)",
                           "flat alpha (alpha0 == alpha1)",
                           "descending alpha (alpha0 > alpha1)",
                           "ascending alpha (alpha0 < alpha1)",
                           "non-opaque (random)"};
    int worst[2][5] = {0}, alpha_bad[2][5] = {0};
    long count[5] = {0};
    int illegal = 0;
    int opaque_detected = 0, nonopaque_detected = 0;
    for (long i = 0; i < n; i++) {
        uint8_t bc[16], col[8], astc[16], ref[64], out[64];
        uint64_t r = rnd();
        int m;
        if (i % 5 == 0) {
            m = 0;  // opaque
        } else if (i % 5 == 1) {
            m = 1;  // flat
        } else if (i % 5 == 2) {
            m = 2;  // descending
        } else if (i % 5 == 3) {
            m = 3;  // ascending
        } else {
            m = 4;  // non-opaque random
        }
        uint32_t c0 = rnd() & 0xFFFF, c1 = rnd() & 0xFFFF;
        if (c0 <= c1) { uint32_t t = c0; c0 = c1; c1 = t; }
        if (c0 == c1) c1 = (c0 > 0) ? c0 - 1 : 0;
        if (i % 4 == 2) {
            c1 = (c0 > 0) ? (uint32_t)(c0 - 1 - (rnd() % 16)) : (uint32_t)(rnd() % 16);
        }
        uint32_t idx = rnd() & 0xFFFFFFFF;
        if (m == 0) {
            build_opaque_block(bc, c0, c1, idx);
        } else if (m == 4) {
            build_nonopaque_block(bc, c0, c1, idx);
        } else {
            // Original random alpha modes
            uint32_t a0, a1;
            uint64_t indices = 0;
            if (m == 1) { a0 = 255; a1 = 255; }
            else if (m == 2) { a0 = 255; a1 = rnd() % 200; }
            else { a0 = rnd() % 200; a1 = 255; }
            for (int t = 0; t < 16; t++) indices |= (uint64_t)(rnd() & 7) << (3 * t);
            bc[0] = (uint8_t)a0; bc[1] = (uint8_t)a1;
            bc[2] = (uint8_t)(indices & 0xFF);
            bc[3] = (uint8_t)((indices >> 8) & 0xFF);
            bc[4] = (uint8_t)((indices >> 16) & 0xFF);
            bc[5] = (uint8_t)((indices >> 24) & 0xFF);
            bc[6] = (uint8_t)((indices >> 32) & 0xFF);
            bc[7] = (uint8_t)((indices >> 40) & 0xFF);
            bc[8] = (uint8_t)(c0 & 0xFF); bc[9] = (uint8_t)(c0 >> 8);
            bc[10] = (uint8_t)(c1 & 0xFF); bc[11] = (uint8_t)(c1 >> 8);
            bc[12] = (uint8_t)idx; bc[13] = (uint8_t)(idx >> 8);
            bc[14] = (uint8_t)(idx >> 16); bc[15] = (uint8_t)(idx >> 24);
        }
        // Opacity detection
        bool is_opaque = bc3_is_opaque_block(bc);
        if (m == 0) { if (is_opaque) opaque_detected++; else printf("FAIL: opaque block not detected as opaque\n"); }
        if (m == 4) { if (!is_opaque) nonopaque_detected++; else printf("FAIL: non-opaque block detected as opaque\n"); }
        bcdec_bc3(bc, ref, 16);
        col[0] = bc[8]; col[1] = bc[9]; col[2] = bc[10]; col[3] = bc[11];
        col[4] = bc[12]; col[5] = bc[13]; col[6] = bc[14]; col[7] = bc[15];
        bc1_to_astc(col, astc, 1);
        count[m]++;
        for (int s = 0; s < 2; s++) {
            if (!astc_decode_block(astc, 4, 4, s, out)) {
                if (illegal++ < 5) printf("illegal block (mode %d): bc3 %016llx\n", m, (unsigned long long)r);
                continue;
            }
            for (int t = 0; t < 16; t++) {
                for (int c = 0; c < 3; c++) {
                    int d = ref[4 * t + c] - out[4 * t + c];
                    if (d < 0) d = -d;
                    if (d > worst[s][m]) worst[s][m] = d;
                }
                if (ref[4 * t + 3] != out[4 * t + 3]) alpha_bad[s][m]++;
            }
        }
    }
    int fail = illegal > 0;
    for (int m = 0; m < 5; m++)
        for (int s = 0; s < 2; s++) {
            printf("%-30s %s: %ld blocks, largest colour difference %d, alpha mismatch on %d texels\n", kind[m],
                   s ? "sRGB" : "LDR ", count[m], worst[s][m], alpha_bad[s][m]);
            if (worst[s][m] > 3) fail = 1;
            // For opaque blocks (m=0), alpha mismatch must be 0
            if (m == 0 && alpha_bad[s][m] > 0) fail = 1;
        }
    printf("Opacity detection: opaque blocks correctly detected %d/%ld, non-opaque correctly detected %d/%ld\n",
           opaque_detected, count[0], nonopaque_detected, count[4]);
    if (opaque_detected != count[0] || nonopaque_detected != count[4]) fail = 1;
    printf("%s (%d illegal blocks)\n", fail ? "FAIL" : "PASS", illegal);
    return fail;
}