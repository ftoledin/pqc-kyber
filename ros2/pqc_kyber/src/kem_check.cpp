#include <array>
#include <cstdint>
#include <iostream>

// These headers supply their own C linkage when included by a C++ consumer.
#include "api.h"
#include "kem.h"
#include "hashing.h"
#include "hashing.h"  // Also check the include guard.
#include "chaotic_hash.h"
#include "verify.h"

int main()
{
  std::array<std::uint8_t, CRYPTO_PUBLICKEYBYTES> pk{};
  std::array<std::uint8_t, CRYPTO_SECRETKEYBYTES> sk{};
  std::array<std::uint8_t, CRYPTO_CIPHERTEXTBYTES> ct{};
  std::array<std::uint8_t, CRYPTO_BYTES> sender_secret{}, receiver_secret{};

  if (crypto_kem_keypair(pk.data(), sk.data()) != 0 ||
      crypto_kem_enc(ct.data(), sender_secret.data(), pk.data()) != 0 ||
      crypto_kem_dec(receiver_secret.data(), ct.data(), sk.data()) != 0 ||
      verify(sender_secret.data(), receiver_secret.data(), CRYPTO_BYTES) != 0) {
    std::cerr << "Falha: os segredos compartilhados diferem.\n";
    return 1;
  }

  constexpr std::array<std::uint8_t, 8> input{{1, 2, 3, 4, 5, 6, 7, 8}};
  std::array<std::uint8_t, 32> hash_from_wrapper{}, hash_direct{};
  ChaoticHashCtx_t state{};
  _sha3_256(hash_from_wrapper.data(), input.data(), input.size());
  ChaoticHashInit(&state, 256);
  ChaoticHashing(&state, hash_direct.data(), input.data(), input.size());
  if (hash_from_wrapper != hash_direct) {
    std::cerr << "Falha: o adaptador nao corresponde ao hashing caotico.\n";
    return 1;
  }

  auto invalid_ct = ct;
  invalid_ct[0] ^= 1;
  if (crypto_kem_dec(receiver_secret.data(), invalid_ct.data(), sk.data()) != 0 ||
      verify(sender_secret.data(), receiver_secret.data(), CRYPTO_BYTES) == 0) {
    std::cerr << "Falha: ciphertext alterado manteve o mesmo segredo.\n";
    return 1;
  }

  std::cout << CRYPTO_ALGNAME << ": C++ ligado ao codigo C com hashing caotico.\n"
            << "Segredo compartilhado recuperado; ciphertext alterado rejeitado.\n";
  return 0;
}
