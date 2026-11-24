#include "common.hpp"
#include <thread>
#include <atomic>
int main(int argc,char **argv) {
  if(argc!=3){std::cerr<<"Uso: probe tcp/127.0.0.1:7447 prefix\n";return 2;}
  try {
    auto session=prototype::session(argv[1]);std::string prefix=argv[2];
    std::mutex mutex;std::optional<demo::Bytes> captured;std::atomic<bool> leak{false};
    auto sub=session.declare_subscriber(zenoh::KeyExpr(prefix+"/**"),[&](const zenoh::Sample &s){
      auto data=s.get_payload().as_vector();std::string plain(data.begin(),data.end());
      if(plain.find("Subir para 10 metros")!=std::string::npos)leak=true;
      if(s.get_keyexpr().as_string_view()==prefix+"/a/data") {
        std::lock_guard<std::mutex> lock(mutex);if(!captured)captured=std::move(data);
      }
      std::cout<<"WIRE "<<s.get_keyexpr().as_string_view()<<" bytes="<<data.size()<<std::endl;
    },zenoh::closures::none);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));std::lock_guard<std::mutex> lock(mutex);
      if(captured)break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    demo::Bytes frame;
    {std::lock_guard<std::mutex> lock(mutex);if(!captured)return 1;frame=*captured;}
    auto altered=frame;altered.back()^=1;
    session.put(zenoh::KeyExpr(prefix+"/a/data"),zenoh::Bytes(std::move(altered)));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    session.put(zenoh::KeyExpr(prefix+"/a/data"),zenoh::Bytes(std::move(frame)));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout<<"PROBE DONE plaintext_literal="<<leak<<std::endl;
    return leak?1:0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
