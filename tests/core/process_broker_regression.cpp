#include "core/process_broker.hpp"
#include "nativeweb/error.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using nativeweb::detail::ProcessBroker;
namespace {
int failures=0;
#define CHECK(e) do { if(!(e)){std::cerr<<__FILE__<<":"<<__LINE__<<" FAIL: "<<#e<<"\n";++failures;}}while(0)
std::vector<std::string> argv(const std::string& python,const std::string& script,const std::string& mode){
    std::vector<std::string> v;v.push_back(python);v.push_back(script);v.push_back(mode);return v;
}
bool rejected(ProcessBroker& broker,const std::string& operation,const VariantList& arguments,const std::string& expected){
    try{(void)broker.invoke(operation,arguments);}
    catch(const nativeweb::Error& e){return e.code()==expected;}
    return false;
}
void success(const std::string& python,const std::string& script){
    ProcessBroker broker(argv(python,script,"normal"),3000);
    VariantList a;a.push_back(std::string("Ada"));
    const Any created=broker.invoke("example_py.Player.create",a);
    CHECK(created.type()==typeid(std::string));
    const std::string handle=AnyCast<std::string>(created);
    CHECK(handle.find("xy_")==0);
    VariantList args;args.push_back(handle);args.push_back(9);
    Any res=broker.invoke("example_py.Player.method.hit",args);
    CHECK(res.type()==typeid(int));CHECK(AnyCast<int>(res)==91);
    VariantList h;h.push_back(handle);
    CHECK(AnyCast<int>(broker.invoke("example_py.Player.get.health",h))==91);
    VariantList setter=h;setter.push_back(27);
    CHECK(broker.invoke("example_py.Player.set.health",setter).empty());
    CHECK(AnyCast<int>(broker.invoke("example_py.Player.get.health",h))==27);
    CHECK(rejected(broker,"example_py.Player.method.secret",h,"worker_rejected"));
    CHECK(broker.running());
    CHECK(broker.invoke("example_py.Player.dispose",h).empty());
    CHECK(rejected(broker,"example_py.Player.dispose",h,"worker_rejected"));
    CHECK(rejected(broker,"example_py.Player.get.health",h,"worker_rejected"));
    broker.stop(); CHECK(!broker.running());
    CHECK(rejected(broker,"example_py.Player.create",a,"worker_stopped"));
}
void malformed(const std::string& python,const std::string& script,const std::string& mode){
    ProcessBroker broker(argv(python,script,mode),mode=="stall"?150:3000);
    if (!rejected(broker,"example_py.Player.create",VariantList(),
                   mode=="stall"||mode=="exit"?"worker_unavailable":"worker_protocol")) {
        std::cerr << "FAIL mode=" << mode << " unexpected failure code\n"; ++failures;
    }
    CHECK(!broker.running());
}
void blockedWriter(const std::string& python,const std::string& script){
    const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    ProcessBroker broker(argv(python,script,"no_read"),250);
    VariantList large;large.push_back(std::string(48000,'x'));
    CHECK(rejected(broker,"echo",large,"worker_unavailable"));
    CHECK(!broker.running());
    const long elapsed=static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-started).count());
    CHECK(elapsed<2500);
}
void typeRoundTrip(const std::string& python,const std::string& script){
    ProcessBroker broker(argv(python,script,"echo"),3000);
    VariantList value;
    value.push_back(true);
    CHECK(AnyCast<bool>(broker.invoke("echo",value)));
    value[0]=Any(3.25);
    CHECK(AnyCast<double>(broker.invoke("echo",value))==3.25);
    value[0]=Any(std::string("string-valued"));
    CHECK(AnyCast<std::string>(broker.invoke("echo",value))=="string-valued");
    value[0]=Any();
    CHECK(broker.invoke("echo",value).empty());
}
void dotnetRoundTrip(const std::string& dotnet,const std::string& dll){
    std::vector<std::string> cmd;cmd.push_back(dotnet);cmd.push_back(dll);
    ProcessBroker broker(cmd,15000);
    VariantList name;name.push_back(std::string("Ada"));
    const std::string handle=AnyCast<std::string>(broker.invoke("example_cs.Player.create",name));
    CHECK(handle.find("xy_")==0);
    VariantList call;call.push_back(handle);call.push_back(12);
    CHECK(AnyCast<int>(broker.invoke("example_cs.Player.method.Hit",call))==88);
    VariantList args;args.push_back(handle);
    CHECK(AnyCast<int>(broker.invoke("example_cs.Player.get.Health",args))==88);
    args.push_back(55);
    CHECK(broker.invoke("example_cs.Player.set.Health",args).empty());
    args.pop_back();
    CHECK(AnyCast<int>(broker.invoke("example_cs.Player.get.Health",args))==55);
    CHECK(broker.invoke("example_cs.Player.dispose",args).empty());
    CHECK(rejected(broker,"example_cs.Player.dispose",args,"worker_rejected"));
}
void invalidArguments(const std::string& python,const std::string& script){
    ProcessBroker broker(argv(python,script,"normal"),3000);
    VariantList nested;nested.push_back(Any(VariantList()));
    CHECK(rejected(broker,"example_py.Player.create",nested,"worker_argument"));
    VariantList huge;huge.push_back(std::string(70000,'x'));
    CHECK(rejected(broker,"example_py.Player.create",huge,"worker_argument"));
    CHECK(broker.running());
}
}
int main(int argc,char** argv0){
    if(argc!=3 && argc!=5){std::cerr<<"usage: process_broker_regression python fixture.py [dotnet-path dll]\n";return 2;}
    const std::string python=argv0[1],script=argv0[2];
    try{
        success(python,script);typeRoundTrip(python,script);invalidArguments(python,script);
        blockedWriter(python,script);
        malformed(python,script,"badid");malformed(python,script,"duplicate");
        malformed(python,script,"wrongtype");malformed(python,script,"stall");
        malformed(python,script,"exit");
        if(argc==5)dotnetRoundTrip(argv0[3],argv0[4]);
    }catch(const std::exception& e){std::cerr<<"Unhandled: "<<e.what()<<"\n";return 2;}
    if(failures){std::cerr<<failures<<" process broker checks FAILED\n";return 1;}
    std::cout<<"C++11 ProcessBroker -> real Python worker: PASS\n";return 0;
}
