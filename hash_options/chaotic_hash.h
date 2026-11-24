#ifndef CHAOTIC_HASH_H
#define CHAOTIC_HASH_H

#include <stdint.h>
#include <stddef.h> 

/* Q32.32 + upstream xorshift is the default; Q24 is historical comparison. */
#ifndef CHAOTIC_FRAC_BITS
#define CHAOTIC_FRAC_BITS 32
#endif
#if CHAOTIC_FRAC_BITS != 24 && CHAOTIC_FRAC_BITS != 32
#error "CHAOTIC_FRAC_BITS must be 24 or 32"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t z_real;
    int64_t z_imag;
    int64_t c_real;
    int64_t c_imag;
    int64_t alpha;
    int h_size;
} ChaoticHashCtx_t;

/* CHF version 1: independent absorption purposes, encoded as UINT32 LE. */
enum {
    CHAOTIC_DOMAIN_GENERIC = 0,
    CHAOTIC_DOMAIN_HASH256 = 1,
    CHAOTIC_DOMAIN_HASH512 = 2,
    CHAOTIC_DOMAIN_XOF128 = 3,
    CHAOTIC_DOMAIN_PRF256 = 4,
    CHAOTIC_DOMAIN_REJECT_KEY = 5,
    CHAOTIC_DOMAIN_REJECT_CT = 6
};

/* Always absorb one framed input, including an empty input (NULL, 0).
 * Frame: "CHF" || version(1) || domain32le || input || zero padding
 * to an 8-byte boundary || input_length64le. No allocation is needed.
 * Q32 uses upstream fe63e12 mixing and extraction, with local CHF1 framing.
 */
void ChaoticHashingDomain(ChaoticHashCtx_t *Ctx, uint8_t *output,
                         const uint8_t *data, size_t data_len, uint32_t domain);

void ChaoticHashInit (ChaoticHashCtx_t *Ctx, int h_size);
/***************************************************************************
 * Ctx - estrutura de contexto para a funcao de hash caotica
 * h_size - tamanho do hash pertencente ao conjunto [32,64,128,160,256,512,1024,2048]
***************************************************************************/

void ChaoticHashing (ChaoticHashCtx_t *Ctx, uint8_t *output, const uint8_t *data, size_t data_len);
/****************************************************************************
 * Ctx - valores iniciais para a funcao de hash caotica
 * output - ponteiro para o buffer de saida do hash (1-byte para cada elemento)
 * data - ponteiro para o buffer de entrada de dados (1-byte para cada elemento)
 * data_len - tamanho do buffer de entrada de dados (em bytes)
 * data != NULL: absorve uma entrada com dominio GENERIC, mesmo vazia.
 * data == NULL e data_len == 0: somente extracao, sem absorver nova entrada.
****************************************************************************/

#ifdef __cplusplus
}
#endif

#endif //CHAOTIC_HASH_H
