#include "common.hpp"
#include <geometry_msgs/msg/point_stamped.hpp>
#include <std_msgs/msg/empty.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <map>
#include <poll.h>
#include <sstream>
#include <unistd.h>

using namespace prototype;
using Position = geometry_msgs::msg::PointStamped;
using Clock = std::chrono::steady_clock;

static std::string canonical_id(std::string id) {
  std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
  return id;
}

// Local, simulated map: metres, z up. These bounds are demo limits, not flight constraints.
static bool valid_id(const std::string &id) {
  return !id.empty() && id.size()<=24 && id!="A" && std::isalpha(static_cast<unsigned char>(id[0])) &&
    std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c) || c=='_';});
}
static bool valid_position(const Position &p) {
  return p.header.frame_id=="map" && std::isfinite(p.point.x) && std::isfinite(p.point.y) &&
    std::isfinite(p.point.z) && std::abs(p.point.x)<=100 && std::abs(p.point.y)<=100 && p.point.z>=0 && p.point.z<=50;
}
static double distance(const Position &a,const Position &b) {
  return std::hypot(std::hypot(a.point.x-b.point.x,a.point.y-b.point.y),a.point.z-b.point.z);
}

class FleetNode : public rclcpp::Node {
  struct Incoming { std::string drone; Event event; };
  struct Peer {
    std::unique_ptr<Link> link;
    rclcpp::Publisher<Position>::SharedPtr command,position,tamper;
    rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr cancel,position_request;
    rclcpp::Subscription<Position>::SharedPtr command_sub,tamper_sub;
    rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr cancel_sub,position_request_sub;
    Position last,target;
    Clock::time_point seen{},requested{},reported{},position_requested{};
    uint64_t next=0,active=0,next_position=0,position_pending=0;
    bool known=false,busy=false,accepted=false,cancel_requested=false;
  };
  std::string role_,id_,endpoint_,prefix_;
  bool verbose_=false,show_crypto_=false,instant_goto_=true,moving_=false;
  double speed_=3;
  std::map<std::string,Peer> peers_;
  std::unique_ptr<Link> drone_link_;
  std::mutex mutex_;
  std::deque<Incoming> incoming_;
  Position position_,target_;
  uint64_t goal_=0,pending_=0;
  Clock::time_point central_seen_=Clock::now(),last_tick_=Clock::now();
  rclcpp::Publisher<Position>::SharedPtr target_pub_,position_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr cancel_pub_;
  rclcpp::Subscription<Position>::SharedPtr target_sub_,position_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr cancel_sub_;
  rclcpp::TimerBase::SharedPtr tick_,announce_,telemetry_;

  void enqueue(const std::string &id,Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(incoming_.size()<256) incoming_.push_back({id,std::move(event)});
    else RCLCPP_ERROR(get_logger(),"Fila da frota cheia; pacote descartado");
  }
  std::unique_ptr<Link> make_link(const std::string &id,const std::string &role) {
    auto logger=get_logger().get_child("transport_"+id);
    logger.set_level(verbose_?rclcpp::Logger::Level::Info:rclcpp::Logger::Level::Warn);
    return std::make_unique<Link>(logger,role,endpoint_,prefix_+"/"+id,
      [this,id](Event e){enqueue(id,std::move(e));},show_crypto_);
  }
  bool connected(const Peer &p) const {return p.known && Clock::now()-p.seen<std::chrono::seconds(3);}
  void add(const std::string &input_id) {
    const auto id=canonical_id(input_id);
    demo::require(valid_id(id),"ID invalido: use letras, numeros e _, ate 24 caracteres; A e a central");
    demo::require(peers_.find(id)==peers_.end(),"Drone ja cadastrado");
    demo::require(peers_.size()<32,"Limite desta demo: 32 drones cadastrados");
    Peer p;p.link=make_link(id,"a");
    const auto base="fleet/"+id;
    p.command=create_publisher<Position>(base+"/goto",10);
    p.tamper=create_publisher<Position>(base+"/tamper",10);
    p.position=create_publisher<Position>(base+"/position",10);
    p.cancel=create_publisher<std_msgs::msg::Empty>(base+"/cancel",10);
    p.position_request=create_publisher<std_msgs::msg::Empty>(base+"/position_request",10);
    p.position_request_sub=create_subscription<std_msgs::msg::Empty>(base+"/position_request",10,[this,id](std_msgs::msg::Empty::ConstSharedPtr){
      try {
        auto &peer=peers_.at(id);
        demo::require(peer.link->ready(),"Link ainda sem chave do drone");
        demo::require(!peer.position_pending,"Consulta de posicao ja em andamento");
        demo::require(peer.next_position!=UINT64_MAX,"IDs de consulta esgotados; reinicie a sessao");
        const auto request=++peer.next_position;
        RCLCPP_INFO(get_logger(),"POSITION REQUEST drone=%s request=%lu",id.c_str(),request);
        peer.link->send(PositionRequest,request,"");
        peer.position_pending=request;peer.position_requested=Clock::now();
      } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"POSITION REQUEST REJECTED drone=%s: %s",id.c_str(),e.what());}
    });
    p.command_sub=create_subscription<Position>(base+"/goto",10,[this,id](Position::ConstSharedPtr msg){
      try {
        auto &peer=peers_.at(id);
        demo::require(valid_position(*msg),"Destino invalido: frame map; x/y entre -100 e 100; z entre 0 e 50 metros");
        demo::require(connected(peer),"Drone sem telemetria recente");
        demo::require(!peer.busy,"Drone ocupado; cancele ou aguarde o goto atual");
        demo::require(peer.next!=UINT64_MAX,"IDs esgotados; reinicie a sessao");
        peer.active=++peer.next;peer.target=*msg;peer.requested=Clock::now();
        if(show_crypto_)RCLCPP_INFO(get_logger(),"CRYPTO INPUT drone=%s id=%lu goto=(%.2f,%.2f,%.2f) antes da cifra",id.c_str(),peer.active,msg->point.x,msg->point.y,msg->point.z);
        peer.link->send(Goal,peer.active,serialize(*msg));
        peer.busy=true;peer.accepted=false;peer.cancel_requested=false;
        RCLCPP_INFO(get_logger(),"GOTO SENT drone=%s id=%lu target=(%.2f,%.2f,%.2f)",id.c_str(),peer.active,msg->point.x,msg->point.y,msg->point.z);
      } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"GOTO REJECTED drone=%s: %s",id.c_str(),e.what());}
    });
    p.tamper_sub=create_subscription<Position>(base+"/tamper",10,[this,id](Position::ConstSharedPtr msg){
      try {
        auto &peer=peers_.at(id);
        demo::require(valid_position(*msg),"Destino invalido: frame map; x/y entre -100 e 100; z entre 0 e 50 metros");
        demo::require(connected(peer),"Drone sem telemetria recente");
        demo::require(!peer.busy,"Drone ocupado; cancele ou aguarde o goto atual");
        demo::require(peer.next!=UINT64_MAX,"IDs esgotados; reinicie a sessao");
        const auto request=++peer.next;
        RCLCPP_INFO(get_logger(),"TAMPER REQUEST drone=%s id=%lu target=(%.2f,%.2f,%.2f)",id.c_str(),request,msg->point.x,msg->point.y,msg->point.z);
        peer.link->send(Goal,request,serialize(*msg),true);
        // This intentionally invalid packet is not tracked as an active goto.
        // No receiver acknowledgement is fabricated: verify in its log and with pos.
        RCLCPP_WARN(get_logger(),"TAMPER SENT drone=%s id=%lu; tag alterada em 1 bit; confira a rejeicao no drone e consulte pos",id.c_str(),request);
      } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"TAMPER REJECTED drone=%s: %s",id.c_str(),e.what());}
    });
    p.cancel_sub=create_subscription<std_msgs::msg::Empty>(base+"/cancel",10,[this,id](std_msgs::msg::Empty::ConstSharedPtr){
      auto &peer=peers_.at(id);
      if(peer.busy) {
        try {
          peer.link->send(Cancel,peer.active,"");peer.cancel_requested=true;
          RCLCPP_INFO(get_logger(),"CANCEL SENT drone=%s id=%lu",id.c_str(),peer.active);
        } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Cancelamento sem confirmacao de envio: %s",e.what());}
      } else RCLCPP_INFO(get_logger(),"Drone %s sem goto ativo",id.c_str());
    });
    peers_.emplace(id,std::move(p));
    RCLCPP_INFO(get_logger(),"DRONE REGISTERED id=%s; chave/sessao/contador proprios",id.c_str());
  }
  void finish(const std::string &status) {
    moving_=false;
    try {
      position_.header.stamp=now();
      drone_link_->send(Telemetry,goal_,serialize(position_));
      std_msgs::msg::String value;value.data=status;
      drone_link_->send(Result,goal_,serialize(value));
    } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Resultado sem confirmacao de entrega: %s",e.what());}
    RCLCPP_INFO(get_logger(),"GOTO RESULT drone=%s id=%lu status=%s position=(%.2f,%.2f,%.2f)",id_.c_str(),goal_,status.c_str(),position_.point.x,position_.point.y,position_.point.z);
  }
  void setup_drone() {
    demo::require(valid_id(id_),"ID de drone invalido");
    drone_link_=make_link(id_,"b");
    position_.header.frame_id=target_.header.frame_id="map";
    const auto base="drone/"+id_;
    target_pub_=create_publisher<Position>(base+"/goto",10);
    position_pub_=create_publisher<Position>(base+"/position",10);
    cancel_pub_=create_publisher<std_msgs::msg::Empty>(base+"/cancel",10);
    target_sub_=create_subscription<Position>(base+"/goto",10,[this](Position::ConstSharedPtr msg){
      if(!pending_)return;
      const auto request=pending_;pending_=0;
      if(moving_ || !valid_position(*msg)) {drone_link_->send(GoalAck,request,"0");return;}
      goal_=request;target_=*msg;moving_=true;last_tick_=Clock::now();
      try{drone_link_->send(GoalAck,goal_,"1");}
      catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Aceitacao sem confirmacao de envio: %s",e.what());}
      RCLCPP_INFO(get_logger(),"GOTO ACCEPTED drone=%s id=%lu target=(%.2f,%.2f,%.2f)",id_.c_str(),goal_,target_.point.x,target_.point.y,target_.point.z);
      if(instant_goto_) {position_=target_;finish("REACHED");}
    });
    cancel_sub_=create_subscription<std_msgs::msg::Empty>(base+"/cancel",10,[this](std_msgs::msg::Empty::ConstSharedPtr){
      if(moving_)finish("CANCELED");
    });
    position_sub_=create_subscription<Position>(base+"/position",10,[this](Position::ConstSharedPtr msg){
      try{if(drone_link_->ready())drone_link_->send(Telemetry,0,serialize(*msg));}
      catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Telemetria indisponivel: %s",e.what());}
    });
    telemetry_=create_wall_timer(std::chrono::milliseconds(500),[this]{position_.header.stamp=now();position_pub_->publish(position_);});
    if(instant_goto_)RCLCPP_INFO(get_logger(),"DRONE READY id=%s; goto imediato apos validar o comando; metros",id_.c_str());
    else RCLCPP_INFO(get_logger(),"DRONE READY id=%s; movimento opcional simulado; velocidade=%.1f m/s",id_.c_str(),speed_);
  }
  void central_receive(const std::string &id,const Event &e) {
    auto &p=peers_.at(id);
    if(e.kind==PositionReply) {
      demo::require(p.position_pending!=0 && e.id==p.position_pending,"Resposta de posicao sem consulta correspondente");
      auto value=deserialize<Position>(e.body);demo::require(valid_position(value),"Posicao consultada invalida");
      if(!connected(p))RCLCPP_INFO(get_logger(),"DRONE CONNECTED id=%s",id.c_str());
      p.last=value;p.seen=Clock::now();p.known=true;p.position->publish(value);p.position_pending=0;
      RCLCPP_INFO(get_logger(),"POSITION RESPONSE drone=%s request=%lu xyz=(%.2f,%.2f,%.2f); pacote autenticado",id.c_str(),e.id,value.point.x,value.point.y,value.point.z);
    } else if(e.kind==Telemetry) {
      auto value=deserialize<Position>(e.body);demo::require(valid_position(value),"Posicao recebida invalida");
      if(!connected(p))RCLCPP_INFO(get_logger(),"DRONE CONNECTED id=%s",id.c_str());
      p.last=value;p.seen=Clock::now();p.known=true;p.position->publish(value);
      if(e.id!=0)RCLCPP_INFO(get_logger(),"POSITION RECOVERED drone=%s id=%lu xyz=(%.2f,%.2f,%.2f); pacote autenticado",id.c_str(),e.id,value.point.x,value.point.y,value.point.z);
      if(p.busy && p.accepted && distance(value,p.target)>0.001 && p.seen-p.reported>std::chrono::seconds(1)) {
        p.reported=p.seen;
        RCLCPP_INFO(get_logger(),"GOTO PROGRESS drone=%s position=(%.2f,%.2f,%.2f) remaining_m=%.2f",id.c_str(),value.point.x,value.point.y,value.point.z,distance(value,p.target));
      }
    } else if(e.kind==GoalAck && p.busy && e.id==p.active) {
      p.accepted=e.body=="1";
      if(!p.accepted)p.busy=false;
      RCLCPP_INFO(get_logger(),"GOTO ACK drone=%s id=%lu accepted=%s",id.c_str(),e.id,e.body.c_str());
    } else if(e.kind==Result && p.busy && e.id==p.active) {
      const auto result=deserialize<std_msgs::msg::String>(e.body);
      demo::require(result.data=="REACHED" || result.data=="CANCELED" || result.data=="LINK_LOST","Resultado invalido");
      p.busy=false;
      RCLCPP_INFO(get_logger(),"GOTO RESULT drone=%s id=%lu status=%s position=(%.2f,%.2f,%.2f)",id.c_str(),e.id,result.data.c_str(),p.last.point.x,p.last.point.y,p.last.point.z);
    } else if(e.kind==CancelAck)RCLCPP_INFO(get_logger(),"CANCEL ACK drone=%s id=%lu accepted=%s",id.c_str(),e.id,e.body.c_str());
    else throw std::runtime_error("Evento inesperado na central");
  }
  void drone_receive(const Event &e) {
    if(e.kind==Command && e.body.empty()) {central_seen_=Clock::now();return;}
    if(e.kind==PositionRequest) {
      demo::require(e.id!=0 && e.body.empty(),"Consulta de posicao invalida");
      central_seen_=Clock::now();
      RCLCPP_INFO(get_logger(),"POSITION REQUEST RECEIVED drone=%s request=%lu; pacote autenticado",id_.c_str(),e.id);
      Position response=position_;response.header.stamp=now();
      drone_link_->send(PositionReply,e.id,serialize(response));
    } else if(e.kind==Goal) {
      const auto target=deserialize<Position>(e.body);
      if(!valid_position(target) || moving_ || pending_) {drone_link_->send(GoalAck,e.id,"0");return;}
      central_seen_=Clock::now();pending_=e.id;target_pub_->publish(target);
    } else if(e.kind==Cancel) {
      const bool match=(moving_ && e.id==goal_) || (pending_ && e.id==pending_);
      if(pending_ && e.id==pending_) {goal_=pending_;pending_=0;finish("CANCELED");}
      else if(match)cancel_pub_->publish(std_msgs::msg::Empty{});
      drone_link_->send(CancelAck,e.id,match?"1":"0");
    } else throw std::runtime_error("Evento inesperado no drone");
  }
  void tick() {
    std::deque<Incoming> events;
    {std::lock_guard<std::mutex> lock(mutex_);events.swap(incoming_);}
    for(const auto &e:events) {
      try{if(role_=="central")central_receive(e.drone,e.event);else drone_receive(e.event);}
      catch(const std::exception &error){RCLCPP_WARN(get_logger(),"Evento recusado: %s",error.what());}
    }
    const auto current=Clock::now();
    const double dt=std::min(0.2,std::chrono::duration<double>(current-last_tick_).count());last_tick_=current;
    if(role_=="drone" && moving_) {
      if(current-central_seen_>std::chrono::seconds(3)) {finish("LINK_LOST");return;}
      const double d=distance(position_,target_),step=speed_*dt;
      if(d<=step) {position_=target_;finish("REACHED");}
      else {
        const double fraction=step/d;
        position_.point.x+=(target_.point.x-position_.point.x)*fraction;
        position_.point.y+=(target_.point.y-position_.point.y)*fraction;
        position_.point.z+=(target_.point.z-position_.point.z)*fraction;
      }
    }
    if(role_=="central")for(auto &[id,p]:peers_) {
      if(p.position_pending && current-p.position_requested>std::chrono::seconds(3)) {
        RCLCPP_WARN(get_logger(),"POSITION TIMEOUT drone=%s request=%lu; sem resposta, cache nao substitui a consulta",id.c_str(),p.position_pending);
        p.position_pending=0;
      }
      if(p.busy && !p.accepted && !p.cancel_requested && current-p.requested>std::chrono::seconds(3)) {
        p.link->send(Cancel,p.active,"");p.cancel_requested=true;
        RCLCPP_WARN(get_logger(),"GOTO TIMEOUT drone=%s; cancelamento solicitado, estado remoto incerto",id.c_str());
      }
    }
  }
public:
  explicit FleetNode():Node("fleet_simulator") {
    require_zenoh();role_=declare_parameter<std::string>("role","central");
    demo::require(role_=="central" || role_=="drone","Papel deve ser central ou drone");
    id_=canonical_id(declare_parameter<std::string>("drone_id","B"));
    endpoint_=declare_parameter<std::string>("endpoint","tcp/127.0.0.1:7447");
    prefix_=declare_parameter<std::string>("prefix","pqc/fleet");
    verbose_=declare_parameter<bool>("verbose",false);
    show_crypto_=declare_parameter<bool>("show_crypto",false);
    instant_goto_=declare_parameter<bool>("instant_goto",true);
    speed_=declare_parameter<double>("speed",3.0);
    demo::require(std::isfinite(speed_) && speed_>0 && speed_<=20,"Velocidade deve estar entre 0 e 20 m/s");
    if(role_=="central") {
      for(const auto &id:declare_parameter<std::vector<std::string>>("drone_ids",{"B","C"}))add(id);
      std::cout<<"CENTRAL READY A | list | add ID | goto ID x y z | tamper ID x y z | pos ID | cancel ID | help | quit\n"<<std::flush;
    } else setup_drone();
    tick_=create_wall_timer(std::chrono::milliseconds(50),[this]{
      try{tick();}catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Atualizacao da frota: %s",e.what());}
    });
    announce_=create_wall_timer(std::chrono::milliseconds(500),[this]{
      try {
        if(role_=="drone")drone_link_->announce();
        else for(auto &[id,p]:peers_) {
          p.link->announce();if(p.link->ready())p.link->send(Command,0,"");
        }
      } catch(const std::exception &e){RCLCPP_WARN(get_logger(),"Enlace: %s",e.what());}
    });
    RCLCPP_WARN(get_logger(),"PROTOTIPO: chaves fixadas no primeiro uso; sem identidade certificada. Posicoes simuladas.");
  }
  ~FleetNode() override {drone_link_.reset();peers_.clear();}
  bool central() const {return role_=="central";}
  bool command(const std::string &line) {
    try {
      std::istringstream input(line);std::string op,id,extra;input>>op;
      std::transform(op.begin(),op.end(),op.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
      if(op.empty())return true;
      if(op=="quit" || op=="exit")return false;
      if(op=="help") {
        std::cout<<"list; add ID; goto ID x y z (metros, frame map); tamper ID x y z (goto com tag adulterada); pos ID (consulta remota); status ID (cache); cancel ID; quit\n"<<std::flush;return true;
      }
      if(op=="list") {
        for(const auto &[name,p]:peers_)std::cout<<"DRONE "<<name<<" "<<(connected(p)?"ONLINE":"OFFLINE")<<" position=("<<p.last.point.x<<","<<p.last.point.y<<","<<p.last.point.z<<") "<<(p.known?"known":"unknown")<<" "<<(p.busy?"BUSY":"IDLE")<<'\n';
        std::cout<<std::flush;return true;
      }
      demo::require(bool(input>>id),"Informe o ID do drone");
      id=canonical_id(id);
      if(op=="add") {demo::require(!(input>>extra),"Uso: add ID");add(id);return true;}
      demo::require(peers_.find(id)!=peers_.end(),"Drone nao cadastrado; use add ID");auto &p=peers_.at(id);
      if(op=="goto" || op=="tamper") {
        Position target;target.header.frame_id="map";target.header.stamp=now();
        demo::require(bool(input>>target.point.x>>target.point.y>>target.point.z) && !(input>>extra),("Uso: "+op+" ID x y z; numeros finitos em metros").c_str());
        demo::require(valid_position(target),"Destino fora dos limites: x/y [-100,100], z [0,50] metros");
        (op=="tamper"?p.tamper:p.command)->publish(target);
      } else if(op=="pos" || op=="position") {
        demo::require(!(input>>extra),"Uso: pos ID");
        p.position_request->publish(std_msgs::msg::Empty{});
      } else if(op=="status") {
        demo::require(!(input>>extra),"Uso: status ID");
        std::cout<<"STATUS CACHE drone="<<id<<" state="<<(connected(p)?"ONLINE":"OFFLINE")<<" known="<<p.known<<" xyz=("<<p.last.point.x<<","<<p.last.point.y<<","<<p.last.point.z<<")";
        if(p.known)std::cout<<" age_s="<<std::chrono::duration<double>(Clock::now()-p.seen).count();
        if(p.busy && p.known)std::cout<<" remaining_m="<<distance(p.last,p.target);
        std::cout<<'\n'<<std::flush;
      } else if(op=="cancel") {demo::require(!(input>>extra),"Uso: cancel ID");p.cancel->publish(std_msgs::msg::Empty{});}
      else throw std::runtime_error("Comando desconhecido; use help");
    } catch(const std::exception &error){std::cout<<"COMMAND ERROR: "<<error.what()<<'\n'<<std::flush;}
    return true;
  }
};

int main(int argc,char **argv) {
  try {
    rclcpp::init(argc,argv);auto node=std::make_shared<FleetNode>();
    if(!node->central())rclcpp::spin(node);
    else {
      rclcpp::executors::SingleThreadedExecutor executor;executor.add_node(node);
      bool stdin_open=true,keep_running=true,discard_line=false;std::string input;
      while(rclcpp::ok() && keep_running) {
        executor.spin_some();pollfd fd{STDIN_FILENO,POLLIN,0};
        if(stdin_open && ::poll(&fd,1,0)>0) {
          // Read the FD directly: std::cin may buffer several lines while poll
          // only sees an empty kernel pipe, leaving the next command stuck.
          char bytes[4096];const auto count=::read(STDIN_FILENO,bytes,sizeof(bytes));
          if(count>0) {
            input.append(bytes,static_cast<size_t>(count));size_t newline;
            while(keep_running && (newline=input.find('\n'))!=std::string::npos) {
              const auto line=input.substr(0,newline);input.erase(0,newline+1);
              if(discard_line)discard_line=false;
              else if(line.size()>1024)std::cout<<"COMMAND ERROR: linha muito longa\n"<<std::flush;
              else keep_running=node->command(line);
            }
            if(input.size()>1024) {input.clear();discard_line=true;std::cout<<"COMMAND ERROR: linha muito longa\n"<<std::flush;}
          } else if(count==0) {
            stdin_open=false;if(!input.empty() && !discard_line)keep_running=node->command(input);input.clear();
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    rclcpp::shutdown();return 0;
  } catch(const std::exception &error){std::cerr<<error.what()<<'\n';if(rclcpp::ok())rclcpp::shutdown();return 1;}
}
