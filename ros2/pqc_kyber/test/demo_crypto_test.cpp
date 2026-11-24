#include "demo_crypto.hpp"
#include <iostream>

int main() {
  try {
    demo::Bytes pk(CRYPTO_PUBLICKEYBYTES), sk(CRYPTO_SECRETKEYBYTES);
    demo::require(crypto_kem_keypair(pk.data(), sk.data()) == 0, "keypair");
    const std::vector<std::string> messages = {"", "Ola, computador B!", "Mensagem com acentos: comunicação",
      std::string("a\0b", 3), std::string(demo::max_message, 'x')};
    for (const auto &message : messages) {
      const auto frame = demo::seal(message, pk);
      const auto decoded = demo::open(frame, sk);
      demo::require(decoded.message == message, "Mensagem recuperada incorretamente");
      // Header, KEM, nonce, hash, tag, and payload are authenticated.
      std::vector<size_t> positions = {0, 4, 5, 5 + CRYPTO_CIPHERTEXTBYTES,
        demo::prefix_size - 1, frame.size() - 1};
      if (!message.empty()) positions.push_back(demo::prefix_size);
      for (auto position : positions) {
        auto corrupted = frame; corrupted[position] ^= 1;
        bool rejected = false;
        try { (void)demo::open(corrupted, sk); } catch (const std::runtime_error &) { rejected = true; }
        demo::require(rejected, "Pacote adulterado aceito");
      }
    }
    for (const auto &frame : {demo::Bytes{}, demo::Bytes(demo::prefix_size + demo::max_message + 17)}) {
      bool rejected = false;
      try { (void)demo::open(frame, sk); } catch (const std::runtime_error &) { rejected = true; }
      demo::require(rejected, "Tamanho invalido aceito");
    }
    demo::Bytes other_pk(CRYPTO_PUBLICKEYBYTES), other_sk(CRYPTO_SECRETKEYBYTES);
    crypto_kem_keypair(other_pk.data(), other_sk.data());
    bool rejected = false;
    try { (void)demo::open(demo::seal("teste", pk), other_sk); }
    catch (const std::runtime_error &) { rejected = true; }
    demo::require(rejected, "Chave privada incorreta aceita");
    std::cout << "PASS: mensagens, hash, adulteracoes, limites e chave incorreta\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
