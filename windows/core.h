#pragma once
#include "vendor/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline double lerp(double a, double b, double t) { return a + (b-a)*t; }
inline double hermite(const Json& keys, double t) {
    if (keys.empty()) return 1;
    if (t <= keys.front()[0].get<double>()) return keys.front()[1];
    for (size_t i=1;i<keys.size();++i) if (t <= keys[i][0].get<double>()) {
        double a=keys[i-1][0], b=keys[i][0], dt=b-a;
        if (dt<=0) throw std::runtime_error("Unsorted curve");
        double u=(t-a)/dt;
        return (2*u*u*u-3*u*u+1)*keys[i-1][1].get<double>()
            +(u*u*u-2*u*u+u)*dt*keys[i-1][3].get<double>()
            +(-2*u*u*u+3*u*u)*keys[i][1].get<double>()
            +(u*u*u-u*u)*dt*keys[i][2].get<double>();
    }
    return keys.back()[1];
}
inline double curve(const Json& c, double t=0, double r=.5) {
    double hi=c.at("max"), lo=c.at("min");
    switch(c.at("mode").get<int>()) {
        case 1:return hermite(c.at("hi"),t)*hi;
        case 2:return lerp(hermite(c.at("lo"),t)*lo,hermite(c.at("hi"),t)*hi,r);
        case 3:return lerp(lo,hi,r);
        default:return hi;
    }
}
inline double keyValue(const Json& keys,double t,size_t component) {
    if(keys.empty()) return 1;
    if(t<=keys.front()[0].get<double>()) return keys.front()[component];
    for(size_t i=1;i<keys.size();++i) if(t<=keys[i][0].get<double>()) {
        double dt=keys[i][0].get<double>()-keys[i-1][0].get<double>();
        return lerp(keys[i-1][component],keys[i][component],(t-keys[i-1][0].get<double>())/std::max(dt,1e-6));
    }
    return keys.back()[component];
}
inline std::array<float,4> gradient(const Json& g,double t) {
    return {float(keyValue(g.at("rgb"),t,1)),float(keyValue(g.at("rgb"),t,2)),
            float(keyValue(g.at("rgb"),t,3)),float(keyValue(g.at("alpha"),t,1))};
}
inline fs::path safeFile(const fs::path& root,const std::string& name) {
    auto p=fs::u8path(name);
    if(p.empty() || p.has_parent_path() || p.is_absolute() || name.find(':')!=std::string::npos)
        throw std::runtime_error("Asset filenames must stay in the pack folder");
    return root/p;
}
inline std::string readText(const fs::path& p, size_t maxBytes=2*1024*1024) {
    if(!fs::exists(p) || fs::file_size(p)>maxBytes) throw std::runtime_error("Missing or oversized file: "+p.filename().u8string());
    std::ifstream f(p,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot read file");
    return std::string(std::istreambuf_iterator<char>(f),{});
}
enum class Sound { Answer, Tap, Touch, BreakPerfect, BreakCritical, Firework };
inline const std::array<const char*,6> soundFiles={"answer.wav","tap.wav","touch.wav","break-perfect.wav","break-critical.wav","touch-firework.wav"};
// Pack 0 = mixed; 1..5 = answer, tap, touch, critical break, fireworks.
inline Sound route(unsigned key,int pack) {
    switch(pack) { case 1:return Sound::Answer; case 2:return Sound::Tap; case 3:return Sound::Touch;
        case 4:return Sound::BreakCritical; case 5:return Sound::Firework; }
    if(key==9)return Sound::Firework;
    if(key==32)return Sound::Touch;
    if(key==13)return Sound::BreakCritical;
    if(key==8 || key==27 || key==46)return Sound::Answer;
    return Sound::Tap;
}
struct Settings {
    bool enabled=true, fireworks=true, hdr=false, allDisplays=false, randomPosition=false;
    int pack=0; float volume=.35f;
    std::string assetPath;
    Json json() const {return {{"enabled",enabled},{"fireworks",fireworks},{"hdr",hdr},{"allDisplays",allDisplays},
        {"randomPosition",randomPosition},{"pack",pack},{"volume",volume},{"assetPath",assetPath}};}
    static Settings parse(const Json& j) {
        Settings s; s.enabled=j.value("enabled",true); s.fireworks=j.value("fireworks",true);
        s.hdr=j.value("hdr",false); s.allDisplays=j.value("allDisplays",false);
        s.randomPosition=j.value("randomPosition",false);
        s.pack=std::clamp(j.value("pack",0),0,5); s.volume=std::clamp(j.value("volume",.35f),0.f,1.f);
        s.assetPath=j.value("assetPath",std::string{}); return s;
    }
};
struct Particle {
    size_t emitter=0; double birth=0,life=1,diameter=0,angle=0,rotation=0,radius=0,speed=0,spin=0,random=0;
};
struct Burst {
    double start=0;
    // Stable normalized screen coordinates, sampled once per trigger.
    std::array<double,2> origin{.5,.5};
    std::vector<Particle> particles;
};
class Simulation {
    std::mt19937 rng{std::random_device{}()};
    double random() { return std::generate_canonical<double,32>(rng); }
public:
    Json emitters=Json::array(); std::vector<Burst> bursts;
    void load(const fs::path& root) {
        loadJson(Json::parse(readText(root/"maimai-fireworks.json")));
    }
    void loadJson(Json list) {
        if(!list.is_array() || list.empty() || list.size()>32) throw std::runtime_error("Invalid emitter count");
        size_t total=0;
        for(auto& e:list) {
            if(e.at("textures").size()>2) throw std::runtime_error("Too many textures");
            for(auto& n:e.at("textures")) safeFile({},n.get<std::string>());
            double rate=e.at("rate"), duration=e.at("duration"), delay=e.at("delay");
            if(!std::isfinite(rate+duration+delay) || rate<0 || rate>500 || duration<0 || duration>5 || delay<0 || delay>5)
                throw std::runtime_error("Invalid emitter timing");
            total+=size_t(std::round(rate*duration));
            for(auto& b:e.at("bursts")) {
                double at=b.at(0), count=b.at(1);
                if(at<0 || at>5 || count<0 || count>500) throw std::runtime_error("Invalid burst");
                total+=size_t(count);
            }
            for(auto key:{"life","size","speed","rotation","sizeCurve"}) {
                for(double t:{0.,.5,1.}) for(double r:{0.,1.}) {
                    double v=curve(e.at(key),t,r);
                    if(!std::isfinite(v) || std::abs(v)>10000) throw std::runtime_error("Invalid curve");
                }
            }
        }
        if(total>1000) throw std::runtime_error("Particle limit exceeded");
        emitters=std::move(list); bursts.clear();
    }
    void burst(double now,bool randomPosition=false) {
        expire(now);
        if(bursts.size()>=6) bursts.erase(bursts.begin());
        Burst b; b.start=now;
        if(randomPosition)b.origin={.2+.6*random(),.2+.6*random()};
        for(size_t i=0;i<emitters.size();++i) {
            auto& e=emitters[i]; std::vector<double> births;
            for(auto& v:e["bursts"]) for(int j=0;j<v[1].get<int>();++j)births.push_back(v[0]);
            double rate=e["rate"], duration=e["duration"];
            for(int j=0;j<int(std::round(rate*duration));++j)births.push_back(j/rate);
            for(double birth:births) {
                Particle p; p.emitter=i; p.random=random(); p.birth=birth+e["delay"].get<double>();
                p.life=std::max(.001,curve(e["life"],0,p.random)); p.diameter=curve(e["size"],0,random())*100;
                p.angle=random()*6.28318530718; p.rotation=curve(e["rotation"],0,random());
                p.radius=e["radius"].get<double>()*(1-e["radiusThickness"].get<double>()*random())*100;
                p.speed=curve(e["speed"],0,p.random)*100;
                p.spin=e["angularVelocity"].is_null()?0:curve(e["angularVelocity"],0,random());
                b.particles.push_back(p);
            }
        }
        bursts.push_back(std::move(b));
    }
    void expire(double now) { bursts.erase(std::remove_if(bursts.begin(),bursts.end(),[&](auto& b){return now-b.start>=1.75;}),bursts.end()); }
};
