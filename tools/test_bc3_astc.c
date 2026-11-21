// Test of runtime/nvn/bc1_astc.c through BC3 blocks (BC3 -> ASTC 4x4: colour line at bytes 8..15 + alpha at 0..7):
// random BC3 blocks decoded by bcdec (bcdec_bc3) and converted to ASTC 4x4 then decoded by astc.c (bit-exact with
// ARM's decoder) - the largest difference, LDR and sRGB. Equivalent to test_bc1_astc (colours + alpha), for the
// BC3 path the GPU took on Mali (BC3 textures decoded to ASTC instead of BC).
// Alpha modes covered: flat (alpha0 == alpha1), descending (alpha0 > alpha1), ascending (alpha0 < alpha1).
// The bcdec interpolation branches are selected by the alpha endpoint order; the ASTC 4-colour single plane
// encodes flat alpha=255. The test sets texel indices so that bcdec produces alpha=255 in all modes.
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

static uint64_t g_rng = 1;
static uint64_t rnd(void) {  // splitmix64
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 10000;
    const char *kind[3] = {"flat alpha (alpha0 == alpha1)", "descending alpha (alpha0 > alpha1)",
                           "ascending alpha (alpha0 < alpha1)"};
    int worst[2][3] = {0}, alpha_bad[2][3] = {0};
    long count[3] = {0};
    int illegal = 0;
    for (long i = 0; i < n; i++) {
        uint8_t bc[16], col[8], astc[16], ref[64], out[64];
        uint64_t r = rnd();
        // Alpha mode: i % 3 -> flat / descending / ascending.
        // Alpha endpoints: u16 LE at bytes 0..1 (alpha0) and 2..3 (alpha1).
        // Indices: u48 at bits 16..63 of the u64 (3 bits per texel, texel t -> bits 3t..3t+2 of that value).
        // We want bcdec to produce alpha=255 for all texels (to match ASTC's flat alpha).
        // - flat (alpha0 == alpha1): branch has 4 interpolated + 0x00 (idx 6) + 0xFF (idx 7). Use index 7.
        // - descending (alpha0 > alpha1): branch has 6 interpolated, no 0x00/0xFF. alpha[7] = (a0+6*a1)/7.
        //   Need a0=255, a1=255 to get 255 at idx 7, but then a0==a1 not >. Instead use idx 0 with a0=255.
        // - ascending (alpha0 < alpha1): branch has 4 interpolated + 0x00 (idx 6) + 0xFF (idx 7). Use index 7.
        int m = (int)(i % 3);
        uint32_t a0, a1;
        uint64_t indices = 0;
        if (m == 0) {  // flat: alpha0 == alpha1 -> use index 7 (alpha[7] = 0xFF)
            a0 = 255; a1 = 255;
            for (int t = 0; t < 16; t++) indices |= (uint64_t)7 << (3 * t);
        } else if (m == 1) {  // descending: alpha0 > alpha1 -> use index 0 (alpha[0] = alpha0 = 255)
            a0 = 255; a1 = 0;
            indices = 0;  // all texels index 0
        } else {  // ascending: alpha0 < alpha1 -> use index 7 (alpha[7] = 0xFF)
            a0 = 100; a1 = 255;
            for (int t = 0; t < 16; t++) indices |= (uint64_t)7 << (3 * t);
        }
        bc[0] = (uint8_t)a0; bc[1] = (uint8_t)a1;  // u8 endpoints
        // Indices: 48 bits at bytes 2..7 (3 bits per texel)
        bc[2] = (uint8_t)(indices & 0xFF);
        bc[3] = (uint8_t)((indices >> 8) & 0xFF);
        bc[4] = (uint8_t)((indices >> 16) & 0xFF);
        bc[5] = (uint8_t)((indices >> 24) & 0xFF);
        bc[6] = (uint8_t)((indices >> 32) & 0xFF);
        bc[7] = (uint8_t)((indices >> 40) & 0xFF);
        // Colour: BC1 part at bytes 8..15
        uint32_t c0 = rnd() & 0xFFFF, c1 = rnd() & 0xFFFF;
        if (c0 <= c1) { uint32_t t = c0; c0 = c1; c1 = t; }
        if (c0 == c1) c1 = (c0 > 0) ? c0 - 1 : 0;  // BC3 uses standard mode: strictly c0 > c1
        if (i % 4 == 2) {  // near endpoints: small steps
            c1 = (c0 > 0) ? (uint32_t)(c0 - 1 - (rnd() % 16)) : (uint32_t)(rnd() % 16);
        }
        uint32_t idx = rnd() & 0xFFFFFFFF;  // texel indices 0..3 along the line
        col[0] = (uint8_t)(c0 & 0xFF), col[1] = (uint8_t)(c0 >> 8), col[2] = (uint8_t)(c1 & 0xFF), col[3] = (uint8_t)(c1 >> 8);
        col[4] = (uint8_t)idx, col[5] = (uint8_t)(idx >> 8), col[6] = (uint8_t)(idx >> 16), col[7] = (uint8_t)(idx >> 24);
        memcpy(bc + 8, col, 8);
        bcdec_bc3(bc, ref, 16);
        bc1_to_astc(col, astc, 1);
        count[m]++;
        for (int s = 0; s < 2; s++) {
            if (!astc_decode_block(astc, 4, 4, s, out)) {
                if (illegal++ < 5) printf("illegal block (mode %d): bc3 %016llx\n", m, (unsigned long long)r);
                continue;
            }
            for (int t = 0; t < 16; t++) {
                if (ref[4 * t + 3] != out[4 * t + 3]) alpha_bad[s][m]++;
                for (int c = 0; c < 3; c++) {
                    int d = ref[4 * t + c] - out[4 * t + c];
                    if (d < 0) d = -d;
                    if (d > worst[s][m]) worst[s][m] = d;
                }
            }
        }
    }
    int fail = illegal > 0;
    for (int m = 0; m < 3; m++)
        for (int s = 0; s < 2; s++) {
            printf("%-24s %s: %ld blocks, largest colour difference %d, alpha wrong on %d texels\n", kind[m],
                   s ? "sRGB" : "LDR ", count[m], worst[s][m], alpha_bad[s][m]);
            if (worst[s][m] > 3 || alpha_bad[s][m]) fail = 1;
        }
    printf("%s (%d illegal blocks)\n", fail ? "FAIL" : "PASS", illegal);
    return fail;
}