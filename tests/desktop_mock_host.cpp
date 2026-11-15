// Synthetic visual integration harness: never connects to the game.
#include "DesktopProtocol.hpp"
#include "DesktopTransport.hpp"
#include <chrono>
#include <iostream>
using namespace GakuActors;
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    UiSnapshot s;s.admitted=true;s.admissionNotice="模拟拍摄场景（不连接游戏）";
    s.catalog.characters.push_back({"mock","示例人物"});
    s.catalog.bodies.push_back({"mock-cstm-0000","示例服装","mock","mdl_chr_mock-cstm-0000_body",false,"mock_thumbnail"});
    s.catalog.resources.push_back({"mock","body","mdl_chr_mock-cstm-0000_body","模拟数据",{}});
    s.catalog.resources.push_back({"mock","face","mdl_chr_mock-base-0000_face","模拟数据",{}});
    s.catalog.resources.push_back({"mock","hair","mdl_chr_mock-base-0000_hair","模拟数据",{}});
    s.catalog.defaults.push_back({"builtin:mock","示例服装","mock","mock-cstm-0000","mock-head",{},{},{},{},true});
    SceneActor actor;actor.instance=-1;actor.scene=-2;actor.name="示例场景人物";actor.owned=true;s.actors.push_back(actor);
    s.resources.push_back({SceneResourceKind::Prop,-11,-11,-2,1,"示例 / 麦克风",false});
    s.resources.push_back({SceneResourceKind::SceneNode,-12,-12,-2,1,"示例场景节点",false});
    std::string thumbnailId;DesktopTransport transport;std::string error;
    if(!transport.Start(argv[1],[&](const std::string& request){
        const auto input=Json::parse(request,nullptr,false);bool accepted=input.is_object()&&input.contains("action");
        if(accepted&&input["action"]=="load_thumbnail"){Command c{Action::Reload};accepted=desktop_protocol::Decode(request,s,c);if(accepted)thumbnailId=c.preset.id;}
        Json result{{"version",1},{"accepted",accepted},{"snapshot",desktop_protocol::Snapshot(s)}};
        if(accepted&&input["action"]=="thumbnail_status")result["thumbnail"]={{"id",thumbnailId},{"status","模拟缩略图"},{"data","iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAALUlEQVR4nGOMWvCMgZaAiaamj1owasGoBaMWjFowasGoBaMWjFowasGoBVQEAEk8AiBpzS+1AAAAAElFTkSuQmCC"}};
        return result.dump();
    },error)){std::cerr<<error;return 2;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    while(transport.Running()&&std::chrono::steady_clock::now()<deadline)Sleep(50);
    transport.Stop();return 0;
}
