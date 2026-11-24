#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "params.h"
#include "hashing.h"

void hash_state_init(hash_state *state, int h_size)
{
    ChaoticHashInit(&state->ctx, h_size);
    memset(state->pending, 0, sizeof state->pending);
    state->pending_position = sizeof state->pending;
}

void _sha3_256(uint8_t h[32], const uint8_t *in, size_t inlen)
{
    ChaoticHashCtx_t state;
    ChaoticHashInit(&state, 256);
    ChaoticHashingDomain(&state, h, in, inlen, CHAOTIC_DOMAIN_HASH256);
}

void _sha3_512(uint8_t h[64], const uint8_t *in, size_t inlen)
{
    ChaoticHashCtx_t state;
    ChaoticHashInit(&state, 512);
    ChaoticHashingDomain(&state, h, in, inlen, CHAOTIC_DOMAIN_HASH512);
}

/* The core extracts 32 bits per iteration. Buffer the unused bytes in this
 * adapter, as described for xof_state in SBCCI2026, Section III.A. */
void _shake128_squeeze(uint8_t *out, size_t outlen, hash_state *state)
{
    const int original_h_size = state->ctx.h_size;
    while(outlen > 0 && state->pending_position < sizeof state->pending) {
        *out++ = state->pending[state->pending_position++];
        outlen--;
    }
    while(outlen >= 4) {
        size_t count = outlen & ~(size_t)3;
        const size_t max_count = ((size_t)INT_MAX / 8) & ~(size_t)3;
        if(count > max_count) count = max_count;
        state->ctx.h_size = (int)(count * 8);
        ChaoticHashing(&state->ctx, out, NULL, 0);
        out += count;
        outlen -= count;
    }
    if(outlen > 0) {
        state->ctx.h_size = 32;
        ChaoticHashing(&state->ctx, state->pending, NULL, 0);
        memcpy(out, state->pending, outlen);
        state->pending_position = outlen;
    }
    state->ctx.h_size = original_h_size;
}

void _shake128_squeezeblocks(uint8_t *out, size_t nblocks, hash_state *state)
{
    /* Avoid overflowing the byte count when converting blocks to bytes. */
    const size_t max_blocks = SIZE_MAX / 168;
    while(nblocks > max_blocks) {
        _shake128_squeeze(out, max_blocks * 168, state);
        out += max_blocks * 168;
        nblocks -= max_blocks;
    }
    _shake128_squeeze(out, nblocks * 168, state);
}

void kyber_shake128_absorb(hash_state *state, const uint8_t seed[KYBER_SYMBYTES],
                          uint8_t x, uint8_t y)
{
    uint8_t in[KYBER_SYMBYTES + 2];
    memcpy(in, seed, KYBER_SYMBYTES);
    in[KYBER_SYMBYTES] = x;
    in[KYBER_SYMBYTES + 1] = y;
    hash_state_init(state, 128);
    ChaoticHashingDomain(&state->ctx, NULL, in, sizeof in, CHAOTIC_DOMAIN_XOF128);
}

void kyber_shake256_prf(uint8_t *out, size_t outlen,
                       const uint8_t key[KYBER_SYMBYTES], uint8_t nonce)
{
    ChaoticHashCtx_t state;
    uint8_t in[KYBER_SYMBYTES + 1];
    ChaoticHashInit(&state, 256);
    memcpy(in, key, KYBER_SYMBYTES);
    in[KYBER_SYMBYTES] = nonce;
    state.h_size = (int)(outlen * 8);
    ChaoticHashingDomain(&state, out, in, sizeof in, CHAOTIC_DOMAIN_PRF256);
}

void kyber_shake256_rkprf(uint8_t out[KYBER_SSBYTES],
                         const uint8_t key[KYBER_SYMBYTES],
                         const uint8_t input[KYBER_CIPHERTEXTBYTES])
{
    ChaoticHashCtx_t state;
    ChaoticHashInit(&state, 256);
    ChaoticHashingDomain(&state, NULL, key, KYBER_SYMBYTES, CHAOTIC_DOMAIN_REJECT_KEY);
    ChaoticHashingDomain(&state, NULL, input, KYBER_CIPHERTEXTBYTES, CHAOTIC_DOMAIN_REJECT_CT);
    state.h_size = KYBER_SSBYTES * 8;
    ChaoticHashing(&state, out, NULL, 0);
}
