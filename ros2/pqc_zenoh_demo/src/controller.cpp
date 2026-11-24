#include "common.hpp"
using namespace prototype;
class Controller:public rclcpp::Node {
  rclcpp_action::Client<Action>::SharedPtr client_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr command_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr telemetry_;
  rclcpp::TimerBase::SharedPtr timer_;
  ClientGoal::SharedPtr goal_;
  bool sent_=false,cancel_sent_=false,got_result_=false,got_telemetry_=false;
  int order_,cancel_after_,feedbacks_=0;
  std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
public:
  bool finished=false;int result=1;
  Controller():Node("operator_simulator") {
    require_zenoh();
    order_=declare_parameter<int>("order",8);cancel_after_=declare_parameter<int>("cancel_after",0);
    auto message=declare_parameter<std::string>("message","Subir para 10 metros (simulado)");
    command_=create_publisher<std_msgs::msg::String>("demo/command",10);
    telemetry_=create_subscription<std_msgs::msg::Float32>("demo/telemetry",10,[this](std_msgs::msg::Float32::ConstSharedPtr m){
      got_telemetry_=true;RCLCPP_INFO(get_logger(),"TELEMETRY RECEIVED: %.1f",m->data);
      if(got_result_)finished=true;
    });
    client_=rclcpp_action::create_client<Action>(this,"demo/fibonacci");
    timer_=create_wall_timer(std::chrono::milliseconds(200),[this,message]{
      if(finished)return;
      if(std::chrono::steady_clock::now()-start_>std::chrono::seconds(20)) {
        RCLCPP_ERROR(get_logger(),"CONTROLLER TIMEOUT");finished=true;return;
      }
      if(sent_ || !client_->action_server_is_ready() || command_->get_subscription_count()==0)return;
      sent_=true;std_msgs::msg::String msg;msg.data=message;command_->publish(msg);
      RCLCPP_INFO(get_logger(),"COMMAND SENT: %s",message.c_str());
      Action::Goal request;request.order=order_;
      rclcpp_action::Client<Action>::SendGoalOptions options;
      options.goal_response_callback=[this](auto g){goal_=g;if(!g){RCLCPP_ERROR(get_logger(),"GOAL REJECTED");finished=true;}};
      options.feedback_callback=[this](auto g,std::shared_ptr<const Action::Feedback> fb){
        ++feedbacks_;RCLCPP_INFO(get_logger(),"ACTION FEEDBACK length=%zu",fb->sequence.size());
        if(cancel_after_>0 && feedbacks_>=cancel_after_ && !cancel_sent_) {
          cancel_sent_=true;client_->async_cancel_goal(g);RCLCPP_INFO(get_logger(),"ACTION CANCEL REQUESTED");
        }
      };
      options.result_callback=[this](const ClientGoal::WrappedResult &r){
        const bool expected=cancel_after_>0?r.code==rclcpp_action::ResultCode::CANCELED:r.code==rclcpp_action::ResultCode::SUCCEEDED;
        result=expected && feedbacks_>0?0:1;got_result_=true;
        RCLCPP_INFO(get_logger(),"ACTION RESULT %s length=%zu",r.code==rclcpp_action::ResultCode::CANCELED?"CANCELED":r.code==rclcpp_action::ResultCode::SUCCEEDED?"SUCCEEDED":"FAILED",r.result->sequence.size());
        if(got_telemetry_)finished=true;
      };
      client_->async_send_goal(request,options);
    });
  }
};
int main(int argc,char **argv){
  rclcpp::init(argc,argv);auto node=std::make_shared<Controller>();
  while(rclcpp::ok()&&!node->finished){rclcpp::spin_some(node);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  int result=node->result;rclcpp::shutdown();return result;
}
