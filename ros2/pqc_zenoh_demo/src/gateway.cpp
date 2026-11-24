#include "common.hpp"
using namespace prototype;
class Gateway : public rclcpp::Node {
  std::string role_;
  std::unique_ptr<Link> link_;
  std::mutex queue_mutex_,ack_mutex_;
  std::condition_variable ack_cv_;
  std::deque<Event> queue_;
  uint64_t next_id_=0,active_id_=0,waiting_id_=0;
  bool busy_=false,ack_seen_=false,ack_ok_=false,pending_cancel_=false;
  std::shared_ptr<ServerGoal> local_goal_;
  ClientGoal::SharedPtr remote_goal_;
  rclcpp_action::Server<Action>::SharedPtr server_;
  rclcpp_action::Client<Action>::SharedPtr client_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr commands_;
  rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr telemetry_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr command_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr telemetry_sub_;
  rclcpp::TimerBase::SharedPtr tick_,announce_;
public:
  Gateway():Node("crypto_gateway") {
    require_zenoh();
    role_=declare_parameter<std::string>("role","a");
    demo::require(role_=="a" || role_=="b","role deve ser a ou b");
    const auto endpoint=declare_parameter<std::string>("endpoint","tcp/127.0.0.1:7447");
    const auto prefix=declare_parameter<std::string>("prefix","pqc/prototype");
    link_=std::make_unique<Link>(get_logger(),role_,endpoint,prefix,[this](Event event){
      if(role_=="a" && event.kind==GoalAck) {
        std::lock_guard<std::mutex> lock(ack_mutex_);
        if(event.id==waiting_id_) {ack_seen_=true;ack_ok_=event.body=="1";ack_cv_.notify_all();}
      } else {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if(queue_.size()<256) queue_.push_back(std::move(event));
        else RCLCPP_ERROR(get_logger(),"Fila de entrada cheia; evento descartado");
      }
    });
    if(role_=="a") setup_a();else setup_b();
    tick_=create_wall_timer(std::chrono::milliseconds(10),[this]{drain();});
    announce_=create_wall_timer(std::chrono::milliseconds(500),[this]{
      try{link_->announce();}catch(const std::exception &e){RCLCPP_ERROR(get_logger(),"%s",e.what());}
    });
    RCLCPP_INFO(get_logger(),"GATEWAY role=%s; transporte central Zenoh cliente; sem DDS",role_.c_str());
  }
  ~Gateway() override {link_.reset();}
private:
  void send(uint8_t kind,uint64_t id,const std::string &body) {link_->send(kind,id,body);}
  void setup_a() {
    telemetry_=create_publisher<std_msgs::msg::Float32>("demo/telemetry",10);
    command_sub_=create_subscription<std_msgs::msg::String>("demo/command",10,[this](std_msgs::msg::String::ConstSharedPtr msg){
      try{send(Command,0,serialize(*msg));}catch(const std::exception &e){RCLCPP_ERROR(get_logger(),"Comando nao encaminhado: %s",e.what());}
    });
    server_=rclcpp_action::create_server<Action>(this,"demo/fibonacci",
      [this](const rclcpp_action::GoalUUID &,std::shared_ptr<const Action::Goal> goal){
        if(busy_ || !link_->ready() || goal->order<2 || goal->order>30)return rclcpp_action::GoalResponse::REJECT;
        busy_=true;active_id_=++next_id_;
        std::unique_lock<std::mutex> lock(ack_mutex_);waiting_id_=active_id_;ack_seen_=ack_ok_=false;
        try {
          send(Goal,active_id_,serialize(*goal));
          bool received=ack_cv_.wait_for(lock,std::chrono::seconds(3),[this]{return ack_seen_;});
          if(received && ack_ok_)return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
          send(Cancel,active_id_,"");
        } catch(const std::exception &e){RCLCPP_ERROR(get_logger(),"Goal: %s",e.what());}
        busy_=false;return rclcpp_action::GoalResponse::REJECT;
      },
      [this](std::shared_ptr<ServerGoal>){
        try{send(Cancel,active_id_,"");return rclcpp_action::CancelResponse::ACCEPT;}
        catch(const std::exception &){return rclcpp_action::CancelResponse::REJECT;}
      },
      [this](std::shared_ptr<ServerGoal> handle){local_goal_=handle;RCLCPP_INFO(get_logger(),"ACTION goal aceito pelo robo: id=%lu",active_id_);});
  }
  void setup_b() {
    commands_=create_publisher<std_msgs::msg::String>("demo/command",10);
    telemetry_sub_=create_subscription<std_msgs::msg::Float32>("demo/telemetry",10,[this](std_msgs::msg::Float32::ConstSharedPtr msg){
      if(link_->ready()) {try{send(Telemetry,0,serialize(*msg));}catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Telemetria: %s",e.what());}}
    });
    client_=rclcpp_action::create_client<Action>(this,"demo/fibonacci");
  }
  void drain() {
    std::deque<Event> events;
    {std::lock_guard<std::mutex> lock(queue_mutex_);events.swap(queue_);}
    for(const auto &event:events) {
      try{if(role_=="a")receive_a(event);else receive_b(event);}
      catch(const std::exception &e){RCLCPP_ERROR(get_logger(),"Evento recusado: %s",e.what());}
    }
  }
  void receive_a(const Event &e) {
    if(e.kind==Telemetry)telemetry_->publish(deserialize<std_msgs::msg::Float32>(e.body));
    else if(e.kind==Feedback && local_goal_ && e.id==active_id_)
      local_goal_->publish_feedback(std::make_shared<Action::Feedback>(deserialize<Action::Feedback>(e.body)));
    else if(e.kind==CancelAck)RCLCPP_INFO(get_logger(),"ACTION cancelamento encaminhado: id=%lu aceito=%s",e.id,e.body.c_str());
    else if(e.kind==Result && local_goal_ && e.id==active_id_) {
      demo::require(!e.body.empty(),"Resultado vazio");
      auto result=std::make_shared<Action::Result>(deserialize<Action::Result>(e.body.substr(1)));
      const auto code=static_cast<rclcpp_action::ResultCode>(uint8_t(e.body[0]));
      if(code==rclcpp_action::ResultCode::SUCCEEDED)local_goal_->succeed(result);
      else if(code==rclcpp_action::ResultCode::CANCELED && local_goal_->is_canceling())local_goal_->canceled(result);
      else local_goal_->abort(result);
      RCLCPP_INFO(get_logger(),"ACTION resultado: id=%lu status=%u",e.id,uint8_t(e.body[0]));
      local_goal_.reset();busy_=false;
    } else throw std::runtime_error("Direcao/tipo de evento nao permitido em A");
  }
  void receive_b(const Event &e) {
    if(e.kind==Command){commands_->publish(deserialize<std_msgs::msg::String>(e.body));return;}
    if(e.kind==Goal) {
      if(busy_ || !client_->wait_for_action_server(std::chrono::milliseconds(500))) {send(GoalAck,e.id,"0");return;}
      const auto goal=deserialize<Action::Goal>(e.body);busy_=true;active_id_=e.id;pending_cancel_=false;
      rclcpp_action::Client<Action>::SendGoalOptions options;
      options.goal_response_callback=[this,id=e.id](ClientGoal::SharedPtr goal_handle){
        remote_goal_=goal_handle;send(GoalAck,id,goal_handle?"1":"0");
        if(!goal_handle)busy_=false;
        else if(pending_cancel_)cancel_remote(id);
      };
      options.feedback_callback=[this,id=e.id](ClientGoal::SharedPtr,std::shared_ptr<const Action::Feedback> fb){send(Feedback,id,serialize(*fb));};
      options.result_callback=[this,id=e.id](const ClientGoal::WrappedResult &result){
        std::string body(1,char(result.code));body+=serialize(*result.result);send(Result,id,body);
        remote_goal_.reset();busy_=false;
      };
      client_->async_send_goal(goal,options);return;
    }
    if(e.kind==Cancel && e.id==active_id_) {
      if(remote_goal_)cancel_remote(e.id);else pending_cancel_=true;
      return;
    }
    throw std::runtime_error("Direcao/tipo de evento nao permitido em B");
  }
  void cancel_remote(uint64_t id) {
    client_->async_cancel_goal(remote_goal_,[this,id](auto response){send(CancelAck,id,response->goals_canceling.empty()?"0":"1");});
  }
};
int main(int argc,char **argv) {
  rclcpp::init(argc,argv);
  try{rclcpp::spin(std::make_shared<Gateway>());}catch(const std::exception &e){std::cerr<<e.what()<<'\n';rclcpp::shutdown();return 1;}
  rclcpp::shutdown();return 0;
}
