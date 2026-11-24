// Standalone audit of the application frame, without ROS2/DDS or drone actuation.
// Compile against a freshly built Kyber library; do not use this test as a security proof.
#include "demo_crypto.hpp"
#include <iostream>
#include <map>
#include <random>
#include <set>

int main(int argc, char **argv) {
  std::map<std::string, size_t> checks, failures, observations;
  auto check = [&](const std::string &name, bool ok) {
    ++checks[name]; if (!ok) ++failures[name];
  };
  demo::Bytes pk(CRYPTO_PUBLICKEYBYTES), sk(CRYPTO_SECRETKEYBYTES);
  demo::require(crypto_kem_keypair(pk.data(), sk.data()) == 0, "keypair");
  // Export ephemeral test material only when explicitly requested for the
  // independent Python AES-GCM cross-check. Never use production keys here.
  if (argc == 2 && std::string(argv[1]) == "--interop") {
    std::cout << '[';
    bool first_case = true;
    for (size_t n : {size_t(0), size_t(1), size_t(15), size_t(16), size_t(17), size_t(65536)}) {
      std::string message(n, '\0');
      for (size_t i = 0; i < n; ++i) message[i] = static_cast<char>(i & 255);
      const auto frame = demo::seal(message, pk);
      std::array<uint8_t, CRYPTO_BYTES> secret{};
      crypto_kem_dec(secret.data(), frame.data() + 5, sk.data());
      if (!first_case) std::cout << ',';
      first_case = false;
      const demo::Bytes plain(message.begin(), message.end());
      const demo::Bytes key(secret.begin(), secret.end());
      std::cout << "{\"message\":\"" << demo::hex(plain) << "\",\"frame\":\"" << demo::hex(frame)
                << "\",\"test_secret\":\"" << demo::hex(key) << "\",\"prefix_size\":"
                << demo::prefix_size << ",\"kem_size\":" << CRYPTO_CIPHERTEXTBYTES << '}';
    }
    std::cout << "]\n";
    return 0;
  }
  auto rejects = [&](const demo::Bytes &frame, const demo::Bytes &key) {
    try { (void)demo::open(frame, key); return false; }
    catch (const std::runtime_error &) { return true; }
  };
  std::mt19937_64 rng(20261103); // Only message/test generation, never production keys.
  std::vector<size_t> sizes;
  for (size_t n = 0; n <= 65; ++n) sizes.push_back(n);
  for (size_t n : {127,128,129,255,256,257,1023,1024,4095,4096,65535,65536}) sizes.push_back(n);
  for (unsigned i = 0; i < 32; ++i) sizes.push_back(rng() % (demo::max_message + 1));
  for (auto n : sizes) {
    std::string message(n, '\0');
    for (auto &c : message) c = static_cast<char>(rng() & 255);
    auto frame = demo::seal(message, pk);
    auto result = demo::open(frame, sk);
    check("roundtrip_binary_and_boundaries", result.message == message &&
      result.received_hash == result.calculated_hash &&
      frame.size() == demo::prefix_size + n + demo::tag_size);
  }
  const std::string message = "Teste ROS2: comunicacao integra";
  const auto original = demo::seal(message, pk);
  for (uint8_t version : {uint8_t(1), uint8_t(2)}) {
    auto legacy_frame = original;
    legacy_frame[3] = version;
    check("reject_legacy_frame_version", rejects(legacy_frame, sk));
  }
  for (size_t i = 0; i < original.size(); ++i) {
    const std::string region = i < 5 ? "header" :
      i < 5 + CRYPTO_CIPHERTEXTBYTES ? "kem" :
      i < 5 + CRYPTO_CIPHERTEXTBYTES + 12 ? "nonce" :
      i < demo::prefix_size ? "hash" :
      i < original.size() - demo::tag_size ? "payload" : "tag";
    for (unsigned bit = 0; bit < 8; ++bit) {
      auto altered = original; altered[i] ^= static_cast<uint8_t>(1u << bit);
      check("reject_one_bit_" + region, rejects(altered, sk));
    }
  }
  for (size_t n = 0; n < original.size(); ++n) {
    demo::Bytes truncated(original.begin(), original.begin() + n);
    check("reject_every_truncation", rejects(truncated, sk));
  }
  for (size_t n = 1; n <= 128; ++n) {
    auto appended = original; appended.resize(original.size() + n, 0);
    check("reject_appended_bytes", rejects(appended, sk));
  }
  for (unsigned i = 0; i < 1000; ++i) {
    demo::Bytes random_frame(demo::prefix_size + demo::tag_size + rng() % 256);
    for (auto &c : random_frame) c = static_cast<uint8_t>(rng());
    // Exercise the parser beyond the first header check on half the trials.
    if (i % 2 == 0) std::copy(original.begin(), original.begin() + 5, random_frame.begin());
    check("reject_random_frames", rejects(random_frame, sk));
  }
  for (unsigned i = 0; i < 64; ++i) {
    demo::Bytes other_pk(CRYPTO_PUBLICKEYBYTES), other_sk(CRYPTO_SECRETKEYBYTES);
    crypto_kem_keypair(other_pk.data(), other_sk.data());
    check("reject_unrelated_private_key", rejects(original, other_sk));
  }
  for (size_t n : {size_t(0), size_t(CRYPTO_SECRETKEYBYTES - 1), size_t(CRYPTO_SECRETKEYBYTES + 1)})
    check("reject_private_key_size", rejects(original, demo::Bytes(n)));
  for (size_t n : {size_t(0), size_t(CRYPTO_PUBLICKEYBYTES - 1), size_t(CRYPTO_PUBLICKEYBYTES + 1)}) {
    bool rejected = false;
    try { (void)demo::seal(message, demo::Bytes(n)); }
    catch (const std::runtime_error &) { rejected = true; }
    check("reject_public_key_size", rejected);
  }
  bool rejected = false;
  try { (void)demo::seal(std::string(demo::max_message + 1, 'x'), pk); }
  catch (const std::runtime_error &) { rejected = true; }
  check("reject_oversize_plaintext", rejected);
  check("reject_oversize_frame", rejects(demo::Bytes(demo::prefix_size + demo::max_message + demo::tag_size + 1), sk));
  std::set<demo::Bytes> packets;
  for (unsigned i = 0; i < 256; ++i) {
    auto frame = demo::seal(message, pk);
    check("fresh_encryption_packet_uniqueness", packets.insert(frame).second);
    check("fresh_encryption_roundtrip", demo::open(frame, sk).message == message);
  }
  // These are protocol limitations, not AES forgeries or failures of decryption.
  for (unsigned i = 0; i < 10; ++i)
    observations["replay_accepted_by_open"] += demo::open(original, sk).message == message;
  for (unsigned i = 0; i < 10; ++i) {
    auto outsider_frame = demo::seal("comando de participante sem identidade", pk);
    observations["public_key_only_sender_accepted"] +=
      demo::open(outsider_frame, sk).message == "comando de participante sem identidade";
  }
  std::cout << "{\"variant\":\"" << CRYPTO_ALGNAME << "\",\"checks\":{";
  bool first = true;
  for (const auto &entry : checks) {
    if (!first) std::cout << ',';
    first = false;
    std::cout << '"' << entry.first << "\":{\"cases\":" << entry.second
              << ",\"failures\":" << failures[entry.first] << '}';
  }
  std::cout << "},\"protocol_observations\":{"; first = true;
  for (const auto &entry : observations) {
    if (!first) std::cout << ',';
    first = false;
    std::cout << '"' << entry.first << "\":" << entry.second;
  }
  std::cout << "}}\n";
  for (const auto &entry : failures) if (entry.second) return 1;
  return 0;
}
