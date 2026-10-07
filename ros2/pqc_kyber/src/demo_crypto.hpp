#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "kem.h"
#include "hashing.h"

namespace demo {
using Bytes = std::vector<uint8_t>;
constexpr size_t max_message = 65536;
constexpr size_t prefix_size = 5 + CRYPTO_CIPHERTEXTBYTES + 12 + 32;
constexpr size_t tag_size = 16;
using Context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
inline void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
inline std::string hex(const Bytes &bytes) {
  std::ostringstream out;
  for (auto b : bytes) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
  return out.str();
}
inline Bytes hash(const std::string &message) {
  require(message.size() <= max_message, "Mensagem excede 64 KiB");
  Bytes result(32);
  _sha3_256(result.data(), reinterpret_cast<const uint8_t *>(message.data()), message.size());
  return result;
}
inline Bytes seal(const std::string &message, const Bytes &pk) {
  require(pk.size() == CRYPTO_PUBLICKEYBYTES, "Chave publica de tamanho incorreto");
  const auto digest = hash(message);
  Bytes frame(prefix_size + message.size() + tag_size);
  frame[0] = 'K'; frame[1] = 'Y'; frame[2] = 'C'; frame[3] = 1; frame[4] = KYBER_K;
  std::array<uint8_t, CRYPTO_BYTES> secret{};
  require(crypto_kem_enc(frame.data() + 5, secret.data(), pk.data()) == 0, "Falha no KEM");
  auto *nonce = frame.data() + 5 + CRYPTO_CIPHERTEXTBYTES;
  require(RAND_bytes(nonce, 12) == 1, "Falha ao gerar nonce");
  std::copy(digest.begin(), digest.end(), nonce + 12);
  Context ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  require(bool(ctx), "Falha ao criar contexto AES");
  require(EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, secret.data(), nonce) == 1,
          "Falha ao iniciar AES-GCM");
  int length = 0, written = 0;
  require(EVP_EncryptUpdate(ctx.get(), nullptr, &length, frame.data(), prefix_size) == 1,
          "Falha na autenticacao dos metadados");
  require(EVP_EncryptUpdate(ctx.get(), frame.data() + prefix_size, &written,
          reinterpret_cast<const uint8_t *>(message.data()), message.size()) == 1, "Falha ao cifrar");
  require(EVP_EncryptFinal_ex(ctx.get(), frame.data() + prefix_size + written, &length) == 1,
          "Falha ao finalizar cifra");
  require(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag_size,
          frame.data() + frame.size() - tag_size) == 1, "Falha ao obter tag");
  return frame;
}
struct Received { std::string message; Bytes received_hash; Bytes calculated_hash; };
inline Received open(const Bytes &frame, const Bytes &sk) {
  require(sk.size() == CRYPTO_SECRETKEYBYTES, "Chave privada de tamanho incorreto");
  require(frame.size() >= prefix_size + tag_size && frame.size() <= prefix_size + max_message + tag_size,
          "Pacote de tamanho invalido");
  require(frame[0] == 'K' && frame[1] == 'Y' && frame[2] == 'C' && frame[3] == 1 && frame[4] == KYBER_K,
          "Versao ou variante Kyber incompativel");
  std::array<uint8_t, CRYPTO_BYTES> secret{};
  require(crypto_kem_dec(secret.data(), frame.data() + 5, sk.data()) == 0, "Falha no KEM");
  const auto *nonce = frame.data() + 5 + CRYPTO_CIPHERTEXTBYTES;
  Context ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  require(bool(ctx), "Falha ao criar contexto AES");
  require(EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, secret.data(), nonce) == 1,
          "Falha ao iniciar AES-GCM");
  const auto cipher_size = frame.size() - prefix_size - tag_size;
  Bytes plain(cipher_size + 16);
  int length = 0, written = 0;
  require(EVP_DecryptUpdate(ctx.get(), nullptr, &length, frame.data(), prefix_size) == 1,
          "Falha nos metadados");
  require(EVP_DecryptUpdate(ctx.get(), plain.data(), &written, frame.data() + prefix_size, cipher_size) == 1,
          "Falha ao decifrar");
  // OpenSSL's API accepts a non-const tag pointer, but does not modify this copy.
  Bytes tag(frame.end() - tag_size, frame.end());
  require(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag_size, tag.data()) == 1,
          "Falha ao configurar tag");
  require(EVP_DecryptFinal_ex(ctx.get(), plain.data() + written, &length) == 1,
          "Autenticacao AES-GCM falhou: pacote rejeitado");
  Received result;
  result.message.assign(reinterpret_cast<const char *>(plain.data()), written + length);
  result.received_hash.assign(nonce + 12, nonce + 44);
  result.calculated_hash = hash(result.message);
  require(result.received_hash == result.calculated_hash, "Hash caotico recebido difere do recalculado");
  return result;
}
inline Bytes ciphertext(const Bytes &frame) {
  return Bytes(frame.begin() + prefix_size, frame.end() - tag_size);
}
}  // namespace demo
