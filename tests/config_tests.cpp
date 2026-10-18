// SPDX-License-Identifier: GPL-3.0-only
#include "config.h"
#include <cassert>
#include <cstdio>
using namespace cabinet;
int main() {
    Config c; std::string error;
    assert(parse_config("",c,error)); assert(c.enabled && c.port==18080 && c.bind_address=="127.0.0.1");
    assert(parse_config("\xef\xbb\xbf[SeRvEr]\r\n Enabled=0\r\nPort=23456\r\nBindAddress=0.0.0.0\n",c,error));
    assert(!c.enabled && c.port==23456 && c.bind_address=="0.0.0.0");
    for(auto bad:{"Enabled=true","Enabled=2","Enabled=","Port=0","Port=65536","Port=-1","Port=1s","Port=9999999999999999999999999",
        "Port=2\nport=3","BindAddress=localhost","BindAddress=127.0.0.1:8","BindAddress=127.0.0.1,8","BindAddress=1.2.3.256",
        "BindAddress=1.2.3","BindAddress=1.2.3.4.5","BindAddress=1..3.4","Enabled=0\nPort=bad"}) {
        assert(!parse_config(std::string("[Server]\n")+bad,c,error)); assert(!error.empty());
    }
    assert(parse_config("[Other]\nPort=bad\n[Server]\nPort=65535\nExtra=x",c,error)); assert(c.port==65535 && !error.empty());
    assert(!parse_config(std::string(65537,' '),c,error));
    std::puts("INI defaults, bounds, duplicates and disabled validation passed");
}
