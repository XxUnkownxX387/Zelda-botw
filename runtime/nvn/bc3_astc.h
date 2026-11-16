// BC3/BC4/BC5 -> ASTC 4x4: decode BC3/BC4/BC5 blocks directly to ASTC 4x4 blocks for GPUs without BC support.
// BC3 (BC1 color + smooth alpha) -> ASTC 4x4 dual-plane (RGBA endpoints CEM 12, or 8+alpha)
// BC4 (single channel) -> ASTC 4x4 luminance (CEM 0)
// BC5 (two channels) -> ASTC 4x4 luminance+alpha (CEM 4)
#pragma once
#include <stdint.h>

// n BC3 blocks (16 bytes each) -> n ASTC 4x4 blocks (16 bytes each)
// Decodes BC3 (BC1 color + smooth alpha) to ASTC 4x4 dual-plane (RGBA endpoints CEM 12 / 8+alpha)
void bc3_to_astc(const uint8_t *bc3, uint8_t *astc, uint64_t n);

// n BC4 blocks (8 bytes each) -> n ASTC 4x4 blocks (16 bytes each)
void bc4_to_astc(const uint8_t *bc4, uint8_t *astc, uint64_t n, bool is_snorm);

// n BC5 blocks (16 bytes each) -> n ASTC 4x4 blocks (16 bytes each)
void bc5_to_astc(const uint8_t *bc5, uint8_t *astc, uint64_t n, bool is_snorm);

// Unit test: round-trip random BC3 blocks through decoder, report max channel error
// Returns max channel error (0-255), or -1 on failure
int bc3_astc_test(void);

// Parallel decode for large images: use up to max_threads threads (0=auto: shader worker count)
// Returns 0 on success, -1 on failure
int bc3_astc_decode_parallel(const uint8_t *bc3, uint8_t *astc, uint64_t n, int max_threads);
int bc4_astc_decode_parallel(const uint8_t *bc4, uint8_t *astc, uint64_t n, bool is_snorm, int max_threads);
int bc5_astc_decode_parallel(const uint8_t *bc5, uint8_t *astc, uint64_t n, bool is_snorm, int max_threads);