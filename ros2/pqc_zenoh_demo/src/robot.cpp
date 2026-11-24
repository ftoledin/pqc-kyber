#include "common.hpp"
using namespace prototype;
class Robot:public rclcpp::Node {
  rclcpp_action::Server<Action>::SharedPtr server_;
  std::shared_ptr<ServerGoal> goal_;
  std::vector<int32_t> values_;
  rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr telemetry_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr command_;
  rclcpp::TimerBase::SharedPtr timer_;
  unsigned ticks_=0;
public:
  Robot():Node("robot_simulator") {
    require_zenoh();
    command_=create_subscription<std_msgs::msg::String>("demo/command",10,[this](std_msgs::msg::String::ConstSharedPtr m){RCLCPP_INFO(get_logger(),"COMMAND RECEIVED: %s",m->data.c_str());});
    telemetry_=create_publisher<std_msgs::msg::Float32>("demo/telemetry",10);
    server_=rclcpp_action::create_server<Action>(this,"demo/fibonacci",
      [this](const auto &,std::shared_ptr<const Action::Goal> g){return !goal_ && g->order>=2 && g->order<=30?rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE:rclcpp_action::GoalResponse::REJECT;},
      [](auto){return rclcpp_action::CancelResponse::ACCEPT;},
      [this](auto g){goal_=g;values_={0,1};RCLCPP_INFO(get_logger(),"ROBOT GOAL order=%d",g->get_goal()->order);});
    timer_=create_wall_timer(std::chrono::milliseconds(200),[this]{
      if(++ticks_%5==0){std_msgs::msg::Float32 m;m.data=87.5f;telemetry_->publish(m);}
      if(!goal_)return;
      auto result=std::make_shared<Action::Result>();
      if(goal_->is_canceling()) {
        result->sequence=values_;goal_->canceled(result);goal_.reset();RCLCPP_INFO(get_logger(),"ROBOT CANCELED");return;
      }
      if(values_.size()<size_t(goal_->get_goal()->order))values_.push_back(values_[values_.size()-1]+values_[values_.size()-2]);
      auto fb=std::make_shared<Action::Feedback>();fb->sequence=values_;goal_->publish_feedback(fb);
      if(values_.size()>=size_t(goal_->get_goal()->order)) {
        result->sequence=values_;goal_->succeed(result);goal_.reset();RCLCPP_INFO(get_logger(),"ROBOT SUCCEEDED");
      }
    });
    RCLCPP_INFO(get_logger(),"ROBOT READY");
  }
};
int main(int argc,char **argv){rclcpp::init(argc,argv);rclcpp::spin(std::make_shared<Robot>());rclcpp::shutdown();}
