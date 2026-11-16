#pragma once
#include <cstdint>

namespace GakuActors {
// Scheduling only: never calls Unity or enables resource/model mutations.
class CatalogStartupRead {
    bool complete_{},settled_{};
    unsigned attempts_{};
    uint64_t settledSince_{},nextAttempt_{};
public:
    static constexpr unsigned kMaxAttempts=3;
    bool Pending()const{return !complete_&&attempts_<kMaxAttempts;}
    unsigned Attempts()const{return attempts_;}
    bool Due(bool admitted,bool busy,uint64_t now) {
        if(!admitted){settled_=false;if(!complete_){attempts_=0;nextAttempt_=0;}return false;}
        if(busy){settled_=false;return false;}
        if(!Pending())return false;
        if(!settled_){settled_=true;settledSince_=now;return false;}
        return now>=settledSince_&&now-settledSince_>=750&&now>=nextAttempt_;
    }
    void Begin(uint64_t now){++attempts_;nextAttempt_=now+(attempts_==1?2000:5000);}
    void Complete(){complete_=true;}
};
}
