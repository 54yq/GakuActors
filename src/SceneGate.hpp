#pragma once
#include <cstdint>
#include <string>

namespace GakuActors {
enum class PerformanceMode { None, Photography, Live };
// Admission uses actual actor owner + active Unity scene, never a saved scene
// name or the mere presence of a loaded model. No game calls live in this policy.
class SceneGate {
    PerformanceMode mode_{PerformanceMode::None};
    int scene_{};uint64_t observed_{};int replacementOwner_{};std::string replacement_,expectedPhoto_;
    static bool PhotoActive(const std::string& n) {
        // Same admitted photography environments as the local MMD baseline.
        return n=="Photography"||n=="env_3d_photo_hotspring-00-00-night"||
            n=="env_3d_adv_vocalroom-00-00-noon"||n=="env_3d_adv_danceroom-00-00-noon"||
            n=="env_3d_adv_visualroom-00-00-noon"||n=="env_3d_adv_danceroom-00-00-night"||
            n=="env_3d_adv_rooftop-00-00-noon"||n=="env_3d_adv_riverbed-00-00-noon"||
            n=="env_3d_adv_riverbed-00-00-evening"||n=="env_3d_photo_studio-00-00-noon"||
            n=="env_3d_adv_studio-01-00-noon";
    }
public:
    static PerformanceMode Recognize(const std::string& owner,const std::string& active) {
        if(owner=="Photography"&&PhotoActive(active))return PerformanceMode::Photography;
        if(owner=="Live"&&(active=="Live"||active.rfind("env_3d_live_",0)==0))return PerformanceMode::Live;
        return PerformanceMode::None;
    }
    bool MatchesActive(const std::string& active)const {
        return mode_==PerformanceMode::Photography?(PhotoActive(active)||(replacementOwner_==scene_&&!replacement_.empty()&&active==replacement_)):
            mode_==PerformanceMode::Live&&(active=="Live"||active.rfind("env_3d_live_",0)==0);
    }
    void Observe(const std::string& owner,int scene,const std::string& active,uint64_t now) {
        if(owner=="Photography"&&scene&&!expectedPhoto_.empty()&&active==expectedPhoto_){
            replacementOwner_=scene;replacement_=active;expectedPhoto_.clear();
        }
        auto found=Recognize(owner,active);
        if(found==PerformanceMode::None&&owner=="Photography"&&scene==replacementOwner_&&!replacement_.empty()&&active==replacement_)found=PerformanceMode::Photography;
        if(found!=PerformanceMode::None&&scene){mode_=found;scene_=scene;observed_=now;}
        else if(!MatchesActive(active))Reset();
    }
    bool Fresh(uint64_t now)const {return scene_&&mode_!=PerformanceMode::None&&now>=observed_&&now-observed_<=1500;}
    void Reset(){mode_=PerformanceMode::None;scene_=0;observed_=0;replacementOwner_=0;replacement_.clear();}
    bool AuthorizeReplacement(int scene,const std::string& target) {
        if(mode_!=PerformanceMode::Photography||!scene||scene!=scene_||target.empty())return false;
        replacementOwner_=scene;replacement_=target;return true;
    }
    void ExpectPhotoReplacement(const std::string& target){expectedPhoto_=target;}
    void ClearPhotoExpectation(){expectedPhoto_.clear();}
    int Scene()const{return scene_;}
    PerformanceMode Mode()const{return mode_;}
};
}
