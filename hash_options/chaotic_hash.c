#include <stdint.h>
#include "chaotic_hash.h"

#define SCALE_BIT CHAOTIC_FRAC_BITS
#define SCALE_VAL (INT64_C(1) << SCALE_BIT)

#define CFOLD(x) ((x) & (SCALE_VAL - INT64_C(1)))

#define MUL_FIXED(a, b) ((int64_t)((((__int128)(a)) * ((__int128)(b))) >> SCALE_BIT))

/* Q32.32 mixing from marcospgr/pqc-kyber fe63e12, chaotic_hash_32.c.
 * Unsigned operations reproduce AVALANCHE_32 bit-for-bit without signed shifts.
 * Keep the local CHF1 framing; the upstream file still uses ambiguous padding.
 */
static int64_t mix_state(int64_t value) {
#if CHAOTIC_FRAC_BITS == 32
    uint32_t mixed = (uint32_t)value;
    mixed ^= mixed << 13;
    mixed ^= mixed >> 17;
    mixed ^= mixed << 5;
    return (int64_t)mixed;
#else
    return value; /* Historical Q24 comparison, without the Q32-only mixer. */
#endif
}

void ChaoticHashInit (ChaoticHashCtx_t *Ctx, int h_size) {
    Ctx->z_real = (int64_t)(0.4569 * SCALE_VAL);
    Ctx->z_imag = (int64_t)(0.5133 * SCALE_VAL);
    Ctx->c_real = (int64_t)(0.7429 * SCALE_VAL);
    Ctx->c_imag = (int64_t)(0.2142 * SCALE_VAL);
    Ctx->h_size = h_size;

    double a = 10.45 + (double)(h_size)/10000.0;
    Ctx->alpha = (int64_t)(a * SCALE_VAL);
}

/* Read a virtual framed input by words, avoiding allocations and size additions
 * that could overflow for long inputs. The original length closes the frame so
 * trailing zeroes and the old missing-word sentinel cannot alias the encoding. */
static uint32_t frame_word(const uint8_t *data, size_t data_len,
                           size_t data_blocks, size_t block, unsigned half,
                           uint32_t domain) {
    if (block == 0) return half ? domain : UINT32_C(0x01464843);
    if (block == data_blocks + 1) {
        const uint64_t length = (uint64_t)data_len;
        return (uint32_t)(length >> (half * 32));
    }
    const size_t offset = (block - 1) * 8 + half * 4;
    uint32_t word = 0;
    for (unsigned j = 0; j < 4; ++j)
        if (offset < data_len && j < data_len - offset)
            word |= (uint32_t)data[offset + j] << (8 * j);
    return word;
}

static void chaotic_hash_process(ChaoticHashCtx_t *Ctx, uint8_t *output,
                                 const uint8_t *data, size_t data_len,
                                 uint32_t domain, int absorb) {
    int64_t z_real = Ctx->z_real;
    int64_t z_imag = Ctx->z_imag;
    int64_t c_real = Ctx->c_real;
    int64_t c_imag = Ctx->c_imag;
    int64_t alpha = Ctx->alpha;

    /* Absorb CHF1 header, zero-padded input and original UINT64 LE length.
     * Each pair still enters the same complex map with the same normalization.
     * A NULL/zero extraction call bypasses framing entirely. */
    if (absorb) {
        const size_t data_blocks = data_len / 8 + (data_len % 8 != 0);
        for (size_t block = 0; block < data_blocks + 2; ++block) {
            const uint32_t cd_real = frame_word(data, data_len, data_blocks, block, 0, domain);
            const uint32_t cd_imag = frame_word(data, data_len, data_blocks, block, 1, domain);

            /* Exact truncated normalization of (d + 1)/(2^32 + 1):
             * floor(((d + 1) * 2^F)/(2^32 + 1)) = d >> (32 - F).
             * For F=32 the complete UINT32 is preserved, without floating point. */
            int64_t norm_cd_real = (int64_t)(cd_real >> (32 - SCALE_BIT));
            int64_t norm_cd_imag = (int64_t)(cd_imag >> (32 - SCALE_BIT));

            int64_t temp_z_real = z_real;
            int64_t temp_z_imag = z_imag;

            z_real = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_real)-MUL_FIXED(temp_z_imag,temp_z_imag))) + norm_cd_real);
            z_imag = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_imag) << 1)) + norm_cd_imag);

            z_real = mix_state(z_real);
            z_imag = mix_state(z_imag);

            c_real = norm_cd_real;
            c_imag = norm_cd_imag;
        }    

    /***********************************************************************************************************
     * ITERATION BLOCK 
     * 1 -  c and alpha received from the input
     * 2 -  z received from the previous block (COMPUTATIONAL BLOCK)
     * 3 -  the last z in the previous step is repeated at (200 + H_size) iterations
     *      to make the chaotic behavior in the system 
     *      equation (7)
     * 4 -  c retains the last normalized input pair; alpha retains its initialized value
     * 5 -  the final value obtained from z will be used in the next step as the system input to extract the hash code
     **********************************************************************************************************/

        for (int l = 0; l < (Ctx->h_size + 200); l++) {
            int64_t temp_z_real = z_real;
            int64_t temp_z_imag = z_imag;

            z_real = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_real)-MUL_FIXED(temp_z_imag,temp_z_imag))) + c_real);
            z_imag = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_imag) << 1)) + c_imag);

            z_real = mix_state(z_real);
            z_imag = mix_state(z_imag);
        }
    }

    /***********************************************************************************************************
     * EXTRACTION BLOCK
     * 1 -  c and alpha received from the input
     * 2 -  z received from the previous block (ITERARION BLOCK)
     * 3 -  we repeat the chaotic iterated map to extract 32-bits of hash code in each iteration
     *      equation (7)
     * 4 -  extract 16-bits from the real part and 16-bits from the imaginary part of z in each iteration
     * 5 -  process of extraction defined in equation (13)
     * 6 -  it is sufficient to merge these 32-bit obtained hash codes with previous 32-bit hash codes in each
     *      iteration
     **********************************************************************************************************/

    if (output != 0) {
        int L = (Ctx->h_size / 32);
        for (int m = 0; m < L; m++) {
            int64_t temp_z_real = z_real;
            int64_t temp_z_imag = z_imag;

            z_real = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_real)-MUL_FIXED(temp_z_imag,temp_z_imag))) + c_real);
            z_imag = CFOLD(MUL_FIXED(MUL_FIXED(alpha,alpha),(MUL_FIXED(temp_z_real,temp_z_imag) << 1)) + c_imag);

            z_real = mix_state(z_real);
            z_imag = mix_state(z_imag);

            /* Upstream Q32.32 extracts bits 8..23, rather than the high 16. */
            uint16_t h1 = (uint16_t)((z_real >> 8) & 0xFFFF);
            uint16_t h2 = (uint16_t)((z_imag >> 8) & 0xFFFF);

            output[m*4 + 0] = h1 >> 8;
            output[m*4 + 1] = h1 & 0xFF;

            output[m*4 + 2] = h2 >> 8;
            output[m*4 + 3] = h2 & 0xFF;
        }
    }
    Ctx->z_real = z_real;
    Ctx->z_imag = z_imag;
    Ctx->c_real = c_real;
    Ctx->c_imag = c_imag;
}

void ChaoticHashingDomain(ChaoticHashCtx_t *Ctx, uint8_t *output,
                         const uint8_t *data, size_t data_len, uint32_t domain) {
    chaotic_hash_process(Ctx, output, data, data_len, domain, 1);
}

void ChaoticHashing(ChaoticHashCtx_t *Ctx, uint8_t *output,
                   const uint8_t *data, size_t data_len) {
    chaotic_hash_process(Ctx, output, data, data_len, CHAOTIC_DOMAIN_GENERIC,
                         data != NULL || data_len != 0);
}
