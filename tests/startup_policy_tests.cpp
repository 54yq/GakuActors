// Test the actual plugin frame pump with runtime-ready=true and deliberately
// absent IL2CPP pointers. Any automatic native startup scan would fail this test.
#include "Plugin.cpp"
#include <cassert>
#include <iostream>

namespace GakuActors {
struct PreLiveLoadHookTestAccess {
    static inline int calls{};
    static void* Original(void* out,void* player,void* fixed,void* helper,void* progress,uintptr_t token,const void* method) {
        assert(out==reinterpret_cast<void*>(1)&&player==reinterpret_cast<void*>(2)&&fixed==reinterpret_cast<void*>(3));
        assert(helper==reinterpret_cast<void*>(4)&&progress==reinterpret_cast<void*>(5)&&token==6&&method==reinterpret_cast<void*>(7));
        ++calls;return out;
    }
    static void Forwarding() {
        assert(PreLiveLoadHook::MethodMatches(nullptr,reinterpret_cast<void*>(7)));
        assert(PreLiveLoadHook::MethodMatches(reinterpret_cast<void*>(7),reinterpret_cast<void*>(7)));
        assert(!PreLiveLoadHook::MethodMatches(reinterpret_cast<void*>(8),reinterpret_cast<void*>(7)));
        PreLiveLoadHook hook;hook.original_=Original;PreLiveLoadHook::current_=&hook;
        assert(PreLiveLoadHook::Detour(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),reinterpret_cast<void*>(5),6,reinterpret_cast<void*>(7))==reinterpret_cast<void*>(1));
        assert(calls==1);PreLiveLoadHook::current_=nullptr;
    }
};
}

int main() {
    using namespace GakuActors;
    PreLiveLoadHookTestAccess::Forwarding();
    State s;s.ready=true;
    assert(!s.dialogOpen&&!s.catalogRequested&&!s.sceneRequested);
    for(int i=0;i<120;++i)TickImpl(&s,1.0f/60.0f);
    assert(!s.snapshot.admitted&&!s.sceneRequested&&!s.catalogRead);
    assert(s.snapshot.actors.empty()&&s.snapshot.catalog.characters.empty());
    // The pre-Live page may be opened outside a performance scene; a missing
    // runtime must fail closed without dereferencing any game API pointers.
    s.commands.push_back({Action::InspectPreLive});TickImpl(&s,1.0f/60.0f);
    assert(!s.snapshot.preLiveDetected&&s.snapshot.preLiveStatus=="游戏运行时尚未就绪");
    auto reply=Json::parse(DesktopMessage(s,R"({"version":1,"action":"snapshot"})"));
    assert(reply["accepted"]==true&&reply["snapshot"]["admitted"]==false);
    reply=Json::parse(DesktopMessage(s,R"({"version":1,"action":"scan_resources"})"));
    assert(reply["accepted"]==true&&s.commands.size()==1);
    reply=Json::parse(DesktopMessage(s,R"({"version":1,"action":"unknown"})"));
    assert(reply["accepted"]==false&&s.commands.size()==1);
    TickImpl(&s,.016f);assert(!s.snapshot.admitted&&s.snapshot.resources.empty());
    s.commands.push_back({Action::Scan});
    s.commands.push_back({Action::ScanResources});
    s.commands.push_back({Action::EnableResourceControls,{},1});
    s.commands.push_back({Action::HideResource});
    s.commands.push_back({Action::CameraColor});
    s.commands.push_back({Action::SelectStage,{},1});
    s.commands.push_back({Action::EnableCreation,{},1}); // unsupported pages cannot enable loading
    s.modelQueue.push_back(Preset{"id","queued","hski","body","head"});
    TickImpl(&s,.016f);
    assert(!s.catalogRequested&&!s.sceneRequested&&!s.snapshot.admitted);
    assert(!s.snapshot.resourceControls&&s.snapshot.resources.empty());
    assert(s.modelQueue.empty()); // no deferred model resolution leaks into startup
    s.commands.push_back({Action::Spawn,Preset{"id","test","hski","body","head"},1});
    TickImpl(&s,.016f);
    assert(!s.snapshot.mutationsEnabled);
    for(int i=0;i<128;++i) {
        s.commands.push_back({Action::Spawn,Preset{"id","test","hume","body","head"},1});
        s.commands.push_back({Action::Remove,{},1});
        s.commands.push_back({Action::Move,{},1,{1,2,3}});
    }
    TickImpl(&s,.016f);
    // The frame pump rejects spawn earlier at the scene gate; no native
    // mutation entry point is reached on an unsupported page.
    assert(!s.snapshot.admitted&&!s.native.MutationsEnabled());
    s.native.SuspendScene();s.native.Shutdown();
    assert(s.native.PerformanceScene()==0&&!s.snapshot.mutationsEnabled);
    std::cout<<"Actual plugin startup pump: 120 frames, repeated spawn/remove/move and lifecycle; zero game API pointers required\n";
}
