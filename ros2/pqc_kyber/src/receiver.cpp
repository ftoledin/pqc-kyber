#include "demo_crypto.hpp"
#include <iostream>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

class Receiver : public rclcpp::Node {
 public:
  Receiver() : Node("computador_b"), sk_(CRYPTO_SECRETKEYBYTES) {
    std_msgs::msg::UInt8MultiArray pk;
    pk.data.resize(CRYPTO_PUBLICKEYBYTES);
    demo::require(crypto_kem_keypair(pk.data.data(), sk_.data()) == 0, "Falha ao gerar chaves");
    key_ = create_publisher<std_msgs::msg::UInt8MultiArray>("pqc_demo/public_key",
             rclcpp::QoS(1).reliable().transient_local());
    ack_ = create_publisher<std_msgs::msg::String>("pqc_demo/status", 10);
    data_ = create_subscription<std_msgs::msg::UInt8MultiArray>("pqc_demo/encrypted", 10,
      [this](std_msgs::msg::UInt8MultiArray::ConstSharedPtr packet) {
        std_msgs::msg::String status;
        try {
          RCLCPP_INFO(get_logger(), "Pacote recebido: %zu bytes. Desencapsulando segredo Kyber...", packet->data.size());
          const auto result = demo::open(packet->data, sk_);
          RCLCPP_INFO(get_logger(), "Mensagem descriptografada: %s", result.message.c_str());
          RCLCPP_INFO(get_logger(), "Hash caotico recebido: %s", demo::hex(result.received_hash).c_str());
          RCLCPP_INFO(get_logger(), "Hash caotico recalculado: %s", demo::hex(result.calculated_hash).c_str());
          RCLCPP_INFO(get_logger(), "AES-GCM autenticado; hashes iguais. Entrega concluida.");
          status.data = "OK: mensagem recuperada; AES-GCM autenticado; hashes iguais";
        } catch (const std::exception &e) {
          RCLCPP_ERROR(get_logger(), "%s", e.what());
          status.data = std::string("ERRO: ") + e.what();
        }
        ack_->publish(status);
      });
    key_->publish(pk);
    RCLCPP_INFO(get_logger(), "Computador B pronto (%s): chave publica publicada; chave privada local.", CRYPTO_ALGNAME);
  }
 private:
  demo::Bytes sk_;
  rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr key_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ack_;
  rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr data_;
};
int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try { rclcpp::spin(std::make_shared<Receiver>()); }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; rclcpp::shutdown(); return 1; }
  rclcpp::shutdown();
}
