#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace GakuActors {
// Value-only identities and restoration policy. Unity handles stay with the
// caller and are never kept in a GUI snapshot or persisted in configuration.
struct ResourceIdentity {
    uint64_t generation{};
    int scene{}, owner{}, object{};
    uint32_t property{};
};
inline bool SameResource(const ResourceIdentity& a,const ResourceIdentity& b) {
    return a.generation && a.scene && a.owner && a.object &&
        a.generation==b.generation && a.scene==b.scene &&
        a.owner==b.owner && a.object==b.object && a.property==b.property;
}
enum class RestoreDecision { RejectIdentity, AlreadyRestored, RestoreOriginal };
// A bool is sufficient for an individual renderer/property or activeSelf bit;
// a model with several renderers must maintain a separate record for each bit.
struct ResourceChange {
    ResourceIdentity identity;
    bool original{}, written{};
    RestoreDecision Decide(const ResourceIdentity& observed,bool current) const {
        if(!SameResource(identity,observed))return RestoreDecision::RejectIdentity;
        if(current==original)return RestoreDecision::AlreadyRestored;
        // A boolean has only two states; an independent writer that makes the
        // same write is indistinguishable here. Cross-plugin ownership needs a
        // separate coordination mechanism before this policy can be used.
        if(current!=written)return RestoreDecision::AlreadyRestored;
        return RestoreDecision::RestoreOriginal;
    }
};
// Commands created before a scan or scene transition cannot target the new
// generation, even when Unity recycles a numerical instance ID.
struct ResourceSession {
    uint64_t generation{};
    int scene{};
    void Reset(int nextScene) {
        ++generation;if(!generation)++generation;
        scene=nextScene;
    }
    bool Accept(const ResourceIdentity& id) const {
        return scene && id.generation==generation && id.scene==scene && id.owner && id.object;
    }
};

// Track a record before writing, so a setter that fails after modifying a game
// object still leaves a recovery path. Backend operations must revalidate the
// live object's identity, verify write readback and return false on failure.
class ResourceChanges {
    std::vector<ResourceChange> entries_;
public:
    std::size_t Size() const { return entries_.size(); }
    template<class Backend>
    bool Apply(Backend& backend,const ResourceIdentity& identity,bool value) {
        for(const auto& entry:entries_)if(SameResource(entry.identity,identity))return false;
        bool original{};
        if(!backend.Read(identity,original))return false;
        if(original==value)return true;
        entries_.push_back({identity,original,value});
        return backend.Write(identity,value);
    }
    template<class Backend>
    bool Restore(Backend& backend) {
        bool clean=true;
        for(auto it=entries_.begin();it!=entries_.end();) {
            bool current{};
            if(!backend.Read(it->identity,current)) { clean=false;++it;continue; }
            const auto decision=it->Decide(it->identity,current);
            if(decision==RestoreDecision::AlreadyRestored ||
               (decision==RestoreDecision::RestoreOriginal && backend.Write(it->identity,it->original)))
                it=entries_.erase(it);
            else {clean=false;++it;}
        }
        return clean;
    }
};
}
