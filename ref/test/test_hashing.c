#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../hashing.h"

/* CHF1 vectors generated from the independent Python integer reference. */
#if CHAOTIC_FRAC_BITS == 24
static const uint8_t expected256[32] = {
  0x63,0xcc,0xe6,0x83,0xb3,0xbb,0x0a,0xdf,0xe4,0xf1,0x8b,0x3b,0x4e,0x5c,0xc0,0x59,
  0x54,0x27,0x7a,0x34,0xda,0x2c,0x70,0xf3,0x57,0xbb,0x87,0x89,0x20,0xf4,0xd2,0x98,
};
static const uint8_t expected512[64] = {
  0x8f,0x72,0xcf,0x17,0x75,0xbe,0xf9,0x5d,0xb1,0x72,0xcf,0xa9,0x6b,0x5a,0x02,0x67,
  0x61,0xe7,0xde,0x7b,0xd6,0xf9,0x4d,0xba,0x98,0xcc,0x3b,0x9a,0x4e,0xf7,0xa6,0x73,
  0xdf,0x82,0x3b,0xbb,0x0e,0x9a,0xee,0x39,0xdd,0xe1,0xb5,0x66,0x78,0x45,0x73,0xc6,
  0xc9,0x00,0xdc,0x2f,0x66,0xe6,0xf1,0x19,0x01,0xba,0x7d,0xa3,0x71,0xc3,0xba,0xf6,
};
#else
/* Q32.32 upstream mixer, CHF1 input; independently computed fixtures. */
static const uint8_t expected256[32] = {
  0x25,0x71,0x50,0xea,0x71,0x0c,0x51,0xb6,0xf2,0x81,0x1f,0xfb,0xd3,0xd0,0xde,0x0d,
  0x26,0xb1,0xa6,0x87,0xe6,0x93,0xa6,0x22,0xe6,0x10,0x45,0xc7,0xb8,0x84,0x48,0x9f,
};
static const uint8_t expected512[64] = {
  0xe3,0xa2,0xad,0xaf,0xcd,0x79,0x09,0xc5,0xa5,0x42,0x44,0x8a,0x65,0x2b,0x4b,0x72,
  0x73,0xe7,0x85,0x63,0x5b,0x61,0x7a,0x5f,0x08,0x7d,0x6a,0x8b,0xb1,0xcf,0xeb,0xa6,
  0x98,0xa8,0x87,0xf6,0xd5,0x1a,0xf5,0x0b,0x7f,0xd6,0xef,0x6f,0x3b,0x05,0x78,0x60,
  0xe9,0x4f,0xf5,0x50,0x37,0xbd,0x80,0x79,0x7e,0xe3,0xef,0x00,0xec,0xb8,0x1a,0x05,
};
#endif

static int test_reference_vectors(void)
{
  uint8_t input[64], digest256[32], digest512[64];
  for(size_t i = 0; i < sizeof input; i++) input[i] = (uint8_t)i;
  _sha3_256(digest256, input, sizeof input);
  _sha3_512(digest512, input, sizeof input);
  if(memcmp(digest256, expected256, sizeof digest256) ||
     memcmp(digest512, expected512, sizeof digest512)) {
    puts("ERROR hashing differs from the selected precision reference vectors");
    return 1;
  }
  return 0;
}

static int test_partial_squeeze(void)
{
  uint8_t seed[KYBER_SYMBYTES] = {0};
  uint8_t whole[176], split[176];
  const size_t chunks[] = {1, 3, 5, 167};
  hash_state state;
  kyber_shake128_absorb(&state, seed, 0, 0);
  _shake128_squeeze(whole, sizeof whole, &state);
  kyber_shake128_absorb(&state, seed, 0, 0);
  memset(split, 0xa5, sizeof split);
  size_t offset = 0;
  for(size_t i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
    _shake128_squeeze(split + offset, chunks[i], &state);
    offset += chunks[i];
  }
  if(memcmp(whole, split, sizeof whole)) {
    puts("ERROR XOF partial output differs from continuous output");
    return 1;
  }
  return 0;
}

#if CHAOTIC_FRAC_BITS == 32
static int test_low_input_bits(void)
{
  uint8_t input[64], changed[64], original[64], actual[64];
  for(size_t i = 0; i < sizeof input; ++i) input[i] = (uint8_t)i;
  _sha3_512(original, input, sizeof input);
  for(size_t word = 0; word < sizeof input; ++word) {
    for(unsigned bit = 0; bit < 8; ++bit) {
      memcpy(changed, input, sizeof input);
      changed[word] ^= (uint8_t)(1u << bit);
      _sha3_512(actual, changed, sizeof changed);
      if(!memcmp(original, actual, sizeof actual)) {
        printf("ERROR Q32 low input bit produced an unchanged hash: byte %zu bit %u\n", word, bit);
        return 1;
      }
    }
  }
  return 0;
}
#endif

static int test_length_and_domains(void)
{
  uint8_t input[80], changed[80], original[64], actual[64];
  for (size_t i = 0; i < sizeof input; ++i) input[i] = (uint8_t)(i + 65);
  for (int bits = 256; bits <= 512; bits += 256) {
    for (size_t n = 0; n <= 65; ++n) {
      if (bits == 256) _sha3_256(original, input, n);
      else _sha3_512(original, input, n);
      memcpy(changed, input, n);
      changed[n] = 0;
      if (bits == 256) _sha3_256(actual, changed, n + 1);
      else _sha3_512(actual, changed, n + 1);
      if (!memcmp(original, actual, (size_t)bits / 8)) {
        printf("ERROR trailing-zero collision: hash %d, length %zu\n", bits, n);
        return 1;
      }
      if (n % 8 == 4) {
        changed[n] = 1;
        if (bits == 256) _sha3_256(actual, changed, n + 1);
        else _sha3_512(actual, changed, n + 1);
        if (!memcmp(original, actual, (size_t)bits / 8)) {
          puts("ERROR old missing-word sentinel collision");
          return 1;
        }
      }
    }
  }
  /* Empty input has the same representation for NULL and non-NULL pointers. */
  _sha3_256(original, NULL, 0);
  _sha3_256(actual, input, 0);
  if (memcmp(original, actual, 32)) return 1;
  /* Absorption purpose must not be confused with hash size or XOF squeezing. */
  uint8_t domains[7][32];
  for (uint32_t domain = 0; domain < 7; ++domain) {
    ChaoticHashCtx_t state;
    ChaoticHashInit(&state, 256);
    ChaoticHashingDomain(&state, domains[domain], input, 64, domain);
    for (uint32_t prev = 0; prev < domain; ++prev)
      if (!memcmp(domains[prev], domains[domain], 32)) {
        puts("ERROR absorption domains produced equal hashes");
        return 1;
      }
  }
  puts("CHF1 lengths, trailing bytes, empty input and domains: OK");
  return 0;
}

int main(void)
{
  int result = test_reference_vectors();
  result |= test_partial_squeeze();
  result |= test_length_and_domains();
#if CHAOTIC_FRAC_BITS == 32
  result |= test_low_input_bits();
#endif
  if(!result) puts("Chaotic hashing regression tests: OK");
  return result;
}
