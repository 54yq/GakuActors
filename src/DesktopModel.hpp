#pragma once
#include "Catalog.hpp"
#include <array>
#include <string>
#include <utility>
#include <vector>
namespace GakuActors {
inline bool ShouldObservePreLive(bool admitted,uint64_t uiSeen,uint64_t now) {
    return !admitted&&uiSeen&&now>=uiSeen&&now-uiSeen<=3000;
}
enum class Action { Scan, Reload, Save, DeletePreset, Spawn, Remove, Move, ResetPosition, Turn, SetYaw, ResetYaw, SetLivePause, SetVisualHandoff, EnableCreation, ScanResources, HideResource, EnableResourceControls, CameraColor, SelectStage, LoadThumbnail, ApplyActorModel, RestoreActorModel, FreeCamera, InspectPreLive, ReadPreLiveCharacters, ReadDesktopCatalog, ArmPreLive, CancelPreLive };
struct Command { Action action; Preset preset; int instance{}; float position[3]{}; float degrees{}; SceneResource resource{}; bool hidden{}; float color[4]{}; int actorScene{};std::string actorUnique; };
struct LiveModelObservation {int instance{};std::string name,body,face,hair,status;bool operator==(const LiveModelObservation&)const=default;};
struct UiSnapshot {
    Catalog catalog;std::vector<Preset> presets;std::vector<SceneActor> actors;
    bool catalogAutoPending{true};
    std::vector<SceneResource> resources;std::string resourceStatus;
    int stageChoice{};std::string stageStatus;
    std::string notice,storeNotice,activeSceneName;bool writable{},busy{},playing{},runtime{};
    uint64_t poseResetRevision{};int poseResetActor{};float poseResetPosition[3]{};
    uint64_t poseResetYawRevision{};int poseResetYawActor{};float poseResetYaw{};
    bool livePauseAvailable{},livePaused{},livePauseOwned{};std::string livePauseStatus;
    bool freeCameraActive{};std::string freeCameraStatus;
    uint64_t preLiveCatalogRevision{};bool preLiveDetected{};int preLiveScene{};std::string preLiveCharacter,preLiveBody,preLiveHead,preLiveStatus,preLiveCatalogStatus,preLiveApplyStatus,preLiveModelStatus;
    std::vector<PreLiveCostume> preLiveCostumes;
    std::vector<LiveModelObservation> liveModels;bool inLive{};
    bool admitted{},mutationsEnabled{},faulted{},resourceControls{},cameraColorEnabled{},motionConflict{},motionHandoffAvailable{},motionHandoffRequested{},motionHandoffReleased{},resourceEnablePending{};float cameraColor[4]{0,1,0,1};std::string admissionNotice{"仅在拍摄或 Live 场景启用"};
};
inline bool ActorModelChoiceAllowed(const Catalog& catalog,const SceneActor& actor,const Preset& preset) {
    const std::array<std::pair<const char*,std::string>,3> parts{{{"body",preset.bodyAsset},{"face",preset.faceAsset},{"hair",preset.hairAsset}}};
    for(const auto& part:parts) {
        if(!TextValid(part.second))return false;
        bool found=false;
        for(const auto& resource:catalog.resources)if(resource.kind==part.first&&resource.resource==part.second){found=true;break;}
        if(!found&&!(part.first==std::string("body")&&part.second==actor.bodyAsset)&&!(part.first==std::string("face")&&part.second==actor.faceAsset)&&!(part.first==std::string("hair")&&part.second==actor.hairAsset))return false;
    }
    return true;
}
}
