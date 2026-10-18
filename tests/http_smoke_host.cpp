// SPDX-License-Identifier: GPL-3.0-only
#include "http_server.h"
#include "iidx_protocol.h"
#include <iostream>
#include <string>
using namespace cabinet;
int main(int argc,char **argv) {
    Config config;
    if(argc!=5 || std::string_view(argv[1])!="--bind" || std::string_view(argv[3])!="--port") return 2;
    std::string error;
    if(!parse_config(std::string("[Server]\nBindAddress=")+argv[2]+"\nPort="+argv[4],config,error)) return 2;
    StatusState state; HttpServer server;
    if(!server.start(config,state,error)) { std::cerr<<error<<'\n'; return 1; }
    std::cout<<"READY"<<std::endl;
    auto emit=[&](Observation::Kind kind,const std::array<char,32> &id=std::array<char,32>{}) {
        state.apply({kind,state.generation(),id,true});
    };
    std::string command;
    while(std::getline(std::cin,command)) {
        using K=Observation::Kind;
        if(command=="quit") { state.stop(); server.stop(); std::cout<<"OK"<<std::endl; return 0; }
        if(command=="idle") { emit(K::IdleArchiveOpened); emit(K::IdleArchiveMounted); }
        else if(command=="fault") emit(K::CaptureFault);
        else if(command.starts_with("login ") || command.starts_with("ending ")) {
            bool login=command.starts_with("login "); std::array<char,32> id{};
            if(!copy_profile_id(std::string_view(command).substr(login?6:7),id)) { server.stop(); return 2; }
            if(login) { emit(K::GetRequested); emit(K::ProfileLoaded,id); } else emit(K::SaveRequested,id);
        } else { server.stop(); return 2; }
        std::cout<<"OK"<<std::endl;
    }
    state.stop(); server.stop(); return 0;
}
