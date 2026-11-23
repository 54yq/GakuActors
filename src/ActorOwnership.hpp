#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace GakuActors {
inline constexpr int MaxOwnedActors=64;

template<class IsOccupied>
int SelectActorIndex(int preferred,IsOccupied isOccupied) {
    if(preferred>=0&&!isOccupied(preferred))return preferred;
    for(int index=0;index<=2*MaxOwnedActors;++index)
        if(index!=preferred&&!isOccupied(index))return index;
    return -1;
}

struct ActorIdentity {
    int instance{},index{},scene{};
    std::string unique;
    bool active{},build{},rendered{};
};
struct ActorLease {
    int manager{};ActorIdentity identity;
    std::string preset;
    std::vector<int> preexisting;
    bool fresh{},ready{};
    float spawnPosition[3]{};
};
inline bool FreshIdentity(const ActorIdentity& actor,int index,int scene,const std::vector<int>& before) {
    // Unity instance IDs and scene handles can be negative; zero is invalid.
    return actor.instance&&actor.scene&&actor.index==index&&actor.scene==scene&&
        std::find(before.begin(),before.end(),actor.instance)==before.end();
}
inline bool LeaseMatches(const ActorLease& lease,const ActorIdentity& current) {
    const auto& expected=lease.identity;
    return lease.fresh&&lease.manager&&FreshIdentity(expected,expected.index,expected.scene,lease.preexisting)&&
        !expected.unique.empty()&&current.instance==expected.instance&&current.index==expected.index&&
        current.scene==expected.scene&&current.unique==expected.unique;
}
inline bool BaselinePreserved(const std::vector<ActorIdentity>& before,const std::vector<ActorIdentity>& after) {
    for(const auto& a:before) {
        const auto it=std::find_if(after.begin(),after.end(),[&](const auto& b){return a.instance==b.instance;});
        if(it==after.end()||a.index!=it->index||a.scene!=it->scene||a.unique!=it->unique||
            a.active!=it->active||a.build!=it->build||a.rendered!=it->rendered)return false;
    }
    return true;
}
}
