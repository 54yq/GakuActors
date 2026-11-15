#pragma once
#include <array>
#include <string>
#include <vector>

namespace GakuActors {
inline bool ActorModelUpdatesAvailable() { return false; }
inline bool ModelRecoveryPermitted(bool completed,bool restoring,bool cancelled,bool targetMatches,bool baselinePreserved) {
    return completed&&!restoring&&!cancelled&&targetMatches&&baselinePreserved;
}
// Operates only on copied values. A source must occur exactly once and each
// source must be distinct, otherwise ownership of an asset slot is ambiguous.
inline bool ReplaceActorModelAssets(const std::vector<std::string>& original,
    const std::array<std::string,3>& current,const std::array<std::string,3>& replacement,
    std::vector<std::string>& result) {
    if(original.size()>128)return false;
    std::array<size_t,3> slots{};
    for(size_t part=0;part<3;++part) {
        if(current[part].empty()||replacement[part].empty())return false;
        for(size_t other=0;other<part;++other)if(current[part]==current[other])return false;
        size_t matches=0;
        for(size_t i=0;i<original.size();++i)if(original[i]==current[part]){slots[part]=i;++matches;}
        if(matches!=1)return false;
    }
    auto copy=original;
    for(size_t part=0;part<3;++part)copy[slots[part]]=replacement[part];
    for(size_t part=0;part<3;++part) {
        size_t matches=0;
        for(const auto& value:copy)if(value==replacement[part])++matches;
        if(matches!=1)return false;
    }
    result=std::move(copy);return true;
}
}
