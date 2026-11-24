#pragma once
#include "pqc_kyber/demo/demo_crypto.hpp"
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <example_interfaces/action/fibonacci.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float32.hpp>
#include <zenoh.hxx>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <functional>
#include <cstring>
#include <thread>

namespace prototype {
inline void require_zenoh() {
  demo::require(std::string(rmw_get_implementation_identifier())=="rmw_zenoh_cpp",
                "Este prototipo exige RMW_IMPLEMENTATION=rmw_zenoh_cpp; use demo a/b/test");
}
using Action = example_interfaces::action::Fibonacci;
using ServerGoal = rclcpp_action::ServerGoalHandle<Action>;
using ClientGoal = rclcpp_action::ClientGoalHandle<Action>;
enum Kind : uint8_t {Command=1, Telemetry, Goal, GoalAck, Feedback, Result, Cancel, CancelAck};
struct Event { uint8_t kind; uint64_t id; std::string body; };
template<class T> std::string serialize(const T &message) {
  rclcpp::SerializedMessage serialized;
  rclcpp::Serialization<T> serializer;
  serializer.serialize_message(&message, &serialized);
  const auto &raw=serialized.get_rcl_serialized_message();
  return std::string(reinterpret_cast<const char *>(raw.buffer),raw.buffer_length);
}
template<class T> T deserialize(const std::string &bytes) {
  demo::require(bytes.size()<=demo::max_message,"CDR excessivo");
  rclcpp::SerializedMessage serialized(bytes.size());
  auto &raw=serialized.get_rcl_serialized_message();
  std::memcpy(raw.buffer,bytes.data(),bytes.size()); raw.buffer_length=bytes.size();
  T value; rclcpp::Serialization<T> serializer; serializer.deserialize_message(&serialized,&value);
  return value;
}
inline void put64(std::string &out,uint64_t value) {
  for(unsigned i=0;i<8;++i) out.push_back(static_cast<char>(value>>(8*i)));
}
inline uint64_t get64(const std::string &in,size_t offset) {
  uint64_t value=0; for(unsigned i=0;i<8;++i) value|=uint64_t(uint8_t(in.at(offset+i)))<<(8*i); return value;
}
inline zenoh::Session session(const std::string &endpoint) {
  demo::require(endpoint.rfind("tcp/",0)==0 && endpoint.size()<256 &&
    endpoint.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:/-_[]")==std::string::npos,"Endpoint invalido");
  auto config=zenoh::Config::from_str("{mode:'client',connect:{endpoints:['"+endpoint+
    "']},scouting:{multicast:{enabled:false},gossip:{enabled:false}}}");
  return zenoh::Session::open(std::move(config));
}

// The central session sees only PK announcements and authenticated encrypted data.
// Keys are pinned after first reception for this run. This is TOFU, not identity authentication.
class Link {
  rclcpp::Logger logger_;
  std::string role_,peer_,prefix_;
  demo::Bytes pk_,sk_,peer_pk_;
  std::string local_session_,peer_session_;
  uint64_t tx_=0,rx_=0;
  mutable std::mutex mutex_;
  std::function<void(Event)> callback_;
  zenoh::Session session_;
  std::optional<zenoh::Subscriber<void>> subscriber_;
public:
  Link(rclcpp::Logger logger,std::string role,const std::string &endpoint,std::string prefix,
       std::function<void(Event)> callback)
    :logger_(logger),role_(std::move(role)),peer_(role_=="a"?"b":"a"),prefix_(std::move(prefix)),
     pk_(CRYPTO_PUBLICKEYBYTES),sk_(CRYPTO_SECRETKEYBYTES),callback_(std::move(callback)),session_(session(endpoint)) {
    demo::require(crypto_kem_keypair(pk_.data(),sk_.data())==0,"keypair");
    local_session_.resize(16);demo::require(RAND_bytes(reinterpret_cast<unsigned char *>(local_session_.data()),16)==1,"session random");
    subscriber_.emplace(session_.declare_subscriber(zenoh::KeyExpr(prefix_+"/"+peer_+"/**"),
      [this](const zenoh::Sample &sample){receive(sample);},zenoh::closures::none));
  }
  ~Link(){subscriber_.reset();}
  bool ready() const {std::lock_guard<std::mutex> lock(mutex_);return !peer_pk_.empty();}
  void announce() {
    std::string bytes=local_session_+std::string(pk_.begin(),pk_.end());
    session_.put(zenoh::KeyExpr(prefix_+"/"+role_+"/pk"),zenoh::Bytes(bytes));
  }
  void send(uint8_t kind,uint64_t id,const std::string &body) {
    std::lock_guard<std::mutex> lock(mutex_);
    demo::require(!peer_pk_.empty(),"Link sem chave do par");
    demo::require(tx_!=UINT64_MAX,"Contador esgotado");
    std::string plain;plain.push_back(1);plain.push_back(char(kind));put64(plain,++tx_);put64(plain,id);
    plain+=local_session_;plain+=body;
    auto frame=demo::seal(plain,peer_pk_);
    session_.put(zenoh::KeyExpr(prefix_+"/"+role_+"/data"),zenoh::Bytes(std::move(frame)));
    RCLCPP_INFO(logger_,"ZENOH TX cifrado: tipo=%u id=%lu seq=%lu",kind,id,tx_);
  }
private:
  void receive(const zenoh::Sample &sample) {
    try {
      const auto key=std::string(sample.get_keyexpr().as_string_view());
      auto bytes=sample.get_payload().as_vector();
      if(key==prefix_+"/"+peer_+"/pk") {
        demo::require(bytes.size()==16+CRYPTO_PUBLICKEYBYTES,"PK tamanho invalido");
        std::string sid(bytes.begin(),bytes.begin()+16);demo::Bytes pk(bytes.begin()+16,bytes.end());
        std::lock_guard<std::mutex> lock(mutex_);
        if(peer_pk_.empty()) {
          peer_pk_=std::move(pk);peer_session_=sid;
          RCLCPP_INFO(logger_,"LINK READY role=%s (chave fixada nesta execucao; sem identidade certificada)",role_.c_str());
        } else demo::require(peer_pk_==pk && peer_session_==sid,"Troca de chave recusada; reinicie os dois gateways");
        return;
      }
      if(key!=prefix_+"/"+peer_+"/data")return;
      auto received=demo::open(bytes,sk_);const auto &p=received.message;
      demo::require(p.size()>=34 && uint8_t(p[0])==1,"Envelope invalido");
      const auto kind=uint8_t(p[1]);demo::require(kind>=Command && kind<=CancelAck,"Tipo invalido");
      const auto seq=get64(p,2),id=get64(p,10);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        demo::require(!peer_pk_.empty() && p.substr(18,16)==peer_session_,"Sessao invalida");
        demo::require(seq>rx_,"REPLAY rejeitado");rx_=seq;
      }
      RCLCPP_INFO(logger_,"ZENOH RX autenticado: tipo=%u id=%lu seq=%lu; hashes iguais",kind,id,seq);
      callback_(Event{kind,id,p.substr(34)});
    } catch(const std::exception &e) {RCLCPP_WARN(logger_,"PACOTE REJEITADO: %s",e.what());}
  }
};
}
