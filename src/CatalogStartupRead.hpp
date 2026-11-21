#pragma once
#include <cstdint>

namespace GakuActors {
// Scheduling only: never calls Unity or enables resource/model mutations.
class CatalogStartupRead {
    bool attempted_{},settled_{};
    uint64_t settledSince_{};
public:
    bool Pending()const{return !attempted_;}
    bool Due(bool connectedOrAdmitted,bool busy,uint64_t now) {
        if(!Pending())return false;
        if(!connectedOrAdmitted||busy){settled_=false;return false;}
        if(!settled_){settled_=true;settledSince_=now;return false;}
        return now>=settledSince_&&now-settledSince_>=750;
    }
    void Begin(){attempted_=true;}
};
}
