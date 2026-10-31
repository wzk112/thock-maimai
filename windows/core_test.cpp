#include "core.h"
#include <iostream>
void check(bool value) { if(!value)throw std::runtime_error("Assertion failed"); }
int main() {
    check(!Settings{}.hdr); check(!Settings::parse(Json::object()).hdr);
    check(!Settings{}.randomPosition);check(!Settings::parse(Json::object()).randomPosition);
    Settings randomSettings;randomSettings.randomPosition=true;
    check(Settings::parse(randomSettings.json()).randomPosition);
    check(Settings::parse({{"pack",999},{"volume",-2}}).pack==5);
    check(Settings::parse({{"volume",-2}}).volume==0);
    check(route(9,0)==Sound::Firework); check(route(32,0)==Sound::Touch);
    check(route(13,0)==Sound::BreakCritical); check(route(8,0)==Sound::Answer);
    for(unsigned k=0;k<256;++k) {check(route(k,2)==Sound::Tap);check(route(k,5)==Sound::Firework);}
    check(std::abs(hermite(Json::parse("[[0,1,-2,-2],[1,0,0,0]]"),.5)-.25)<1e-9);
    bool rejected=false;try{safeFile("assets","../secret");}catch(...){rejected=true;}check(rejected);
    check(safeFile("assets","star.png")==fs::path("assets")/"star.png");
    Simulation s; for(int i=0;i<30;++i)s.burst(i*.01);check(s.bursts.size()==6);
    check(s.bursts.back().origin==std::array<double,2>{.5,.5});
    auto previous=s.bursts.back().origin;
    for(int i=0;i<24;++i){s.burst(.31+i*.01,true);auto origin=s.bursts.back().origin;
        check(s.bursts[s.bursts.size()-2].origin==previous);
        for(auto v:origin)check(v>=.2&&v<=.8);
        previous=origin;check(s.bursts.size()<=6);}
    s.burst(.6);check(s.bursts.back().origin==std::array<double,2>{.5,.5});
    s.expire(3);check(s.bursts.empty());
    std::cout<<"PASS: default HDR off, route table, limits, curve, safe paths, burst bound and expiry\n";
}
