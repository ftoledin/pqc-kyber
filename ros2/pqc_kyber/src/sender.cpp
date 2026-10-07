#include "demo_crypto.hpp"
#include <chrono>
#include <iostream>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

class Sender : public rclcpp::Node {
 public:
  bool finished = false;
  int result = 1;
  Sender() : Node("computador_a") {
    message_ = declare_parameter<std::string>("message", "Ola, computador B!");
    tamper_ = declare_parameter<bool>("tamper", false);
    demo::require(message_.size() <= demo::max_message, "Mensagem excede 64 KiB");
    data_ = create_publisher<std_msgs::msg::UInt8MultiArray>("pqc_demo/encrypted", 10);
    key_ = create_subscription<std_msgs::msg::UInt8MultiArray>("pqc_demo/public_key",
      rclcpp::QoS(1).reliable().transient_local(),
      [this](std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) { if (!sent_) pk_ = msg->data; });
    ack_ = create_subscription<std_msgs::msg::String>("pqc_demo/status", 10,
      [this](std_msgs::msg::String::ConstSharedPtr msg) {
        if (!sent_) return;
        RCLCPP_INFO(get_logger(), "Resposta do computador B: %s", msg->data.c_str());
        result = msg->data.rfind("OK:", 0) == 0 ? 0 : 1;
        finished = true;
      });
    start_ = std::chrono::steady_clock::now();
    timer_ = create_wall_timer(std::chrono::milliseconds(200), [this]() {
      if (finished) return;
      if (std::chrono::steady_clock::now() - start_ > std::chrono::seconds(30)) {
        RCLCPP_ERROR(get_logger(), "Tempo esgotado aguardando receptor/confirmacao.");
        finished = true; return;
      }
      if (sent_ || pk_.empty() || data_->get_subscription_count() == 0) return;
      try {
        std_msgs::msg::UInt8MultiArray packet;
        packet.data = demo::seal(message_, pk_);
        RCLCPP_INFO(get_logger(), "Mensagem enviada: %s", message_.c_str());
        RCLCPP_INFO(get_logger(), "Hash caotico da mensagem: %s", demo::hex(demo::hash(message_)).c_str());
        RCLCPP_INFO(get_logger(), "Mensagem cifrada (AES-256-GCM, hex): %s", demo::hex(demo::ciphertext(packet.data)).c_str());
        if (tamper_) { packet.data.back() ^= 1; RCLCPP_WARN(get_logger(), "Simulacao: tag de autenticacao adulterada."); }
        sent_ = true;
        data_->publish(packet);
        RCLCPP_INFO(get_logger(), "Pacote publicado: %zu bytes (KEM, nonce, hash, mensagem cifrada e tag).", packet.data.size());
      } catch (const std::exception &e) {
        RCLCPP_ERROR(get_logger(), "%s", e.what()); finished = true;
      }
    });
    RCLCPP_INFO(get_logger(), "Computador A: aguardando chave publica do computador B...");
  }
 private:
  std::string message_;
  bool tamper_ = false, sent_ = false;
  demo::Bytes pk_;
  std::chrono::steady_clock::time_point start_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr data_;
  rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr key_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr ack_;
};
int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<Sender>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (rclcpp::ok() && !node->finished) executor.spin_once(std::chrono::milliseconds(100));
    const auto result = node->result;
    rclcpp::shutdown(); return result;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; rclcpp::shutdown(); return 1; }
}
