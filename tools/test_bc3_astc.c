// Test of runtime/nvn/bc1_astc.c through BC3 blocks (BC3 -> ASTC 4x4: colour line at bytes 8..15 + alpha at 0..8):
// random BC3 blocks decoded by bcdec (bcdec_bc3) and converted to ASTC 4x4 then decoded by astc.c (bit-exact with
// ARM's decoder) - the largest difference, LDR and sRGB. Equivalent to test_bc1_astc (colours + alpha), for the
// BC3 path the GPU took on Mali (BC3 textures decoded to ASTC instead of BC).
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
    const char *kind = "4 colours (opaque)";  // BC3 -> ASTC: the standard 4-colour line, opaque alpha
    int worst[2] = {0}, alpha_bad[2] = {0}, illegal = 0;
    long count = 0;
    for (long i = 0; i < n; i++) {
        uint8_t bc[16], col[8], astc[16], ref[64], out[64];
        uint64_t r = rnd();
        uint32_t c0 = rnd() & 0xFFFF, c1 = rnd() & 0xFFFF;
        if (c0 <= c1) { uint32_t t = c0; c0 = c1; c1 = t; }  // (swap; strict when unequal)
        if (c0 == c1) c1 = (c0 > 0) ? c0 - 1 : 0;  // BC3 uses the standard mode: strictly c0 > c1
        if (i % 4 == 2) {  // (near endpoints: small steps)
            c1 = (c0 > 0) ? (uint32_t)(c0 - 1 - (rnd() % 16)) : (uint32_t)(rnd() % 16);
        }
        uint32_t idx = rnd() & 0xFF;  // texel indices 0..3 along the line
        col[0] = (uint8_t)(c0 & 0xFF), col[1] = (uint8_t)(c0 >> 8), col[2] = (uint8_t)(c1 & 0xFF), col[3] = (uint8_t)(c1 >> 8);
        col[4] = (uint8_t)idx, col[5] = (uint8_t)(idx >> 8), col[6] = (uint8_t)(idx >> 16), col[7] = (uint8_t)(idx >> 24);
        memcpy(bc + 8, col, 8);
        bc[0] = 255, bc[1] = 255, bc[2] = 255, bc[3] = 255, bc[4] = 0, bc[5] = 0, bc[6] = 0, bc[7] = 0;  // opaque alpha -> 255 (alpha index 0)
        bcdec_bc3(bc, ref, 16);
        bc1_to_astc(col, astc, 1);
        count++;
        for (int s = 0; s < 2; s++) {
            if (!astc_decode_block(astc, 4, 4, s, out)) {
                if (illegal++ < 5) printf("illegal block: bc3 %016llx\n", (unsigned long long)r);
                continue;
            }
            for (int t = 0; t < 16; t++) {
                if (ref[4 * t + 3] != out[4 * t + 3]) alpha_bad[s]++;
                for (int c = 0; c < 3; c++) {
                    int d = ref[4 * t + c] - out[4 * t + c];
                    if (d < 0) d = -d;
                    if (d > worst[s]) worst[s] = d;
                }
            }
        }
    }
    int fail = illegal > 0;
    for (int s = 0; s < 2; s++) {
        printf("%-24s %s: %ld blocks, largest colour difference %d, alpha wrong on %d texels\n", kind,
               s ? "sRGB" : "LDR ", count, worst[s], alpha_bad[s]);
        if (worst[s] > 3 || alpha_bad[s]) fail = 1;
    }
    printf("%s (%d illegal blocks)\n", fail ? "FAIL" : "PASS", illegal);
    return fail;
}
