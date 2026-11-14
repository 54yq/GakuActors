// Test the actual plugin frame pump with runtime-ready=true and deliberately
// absent IL2CPP pointers. Any automatic native startup scan would fail this test.
#include "Plugin.cpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace GakuActors;
    State s;s.ready=true;
    assert(!s.dialogOpen&&!s.catalogRequested&&!s.sceneRequested);
    for(int i=0;i<120;++i)TickImpl(&s,1.0f/60.0f);
    assert(!s.snapshot.admitted&&!s.sceneRequested&&!s.catalogRead);
    assert(s.snapshot.actors.empty()&&s.snapshot.catalog.characters.empty());
    s.commands.push_back({Action::Scan});
    s.commands.push_back({Action::EnableCreation,{},1}); // unsupported pages cannot enable loading
    s.modelQueue.push_back(Preset{"id","queued","hski","body","head"});
    TickImpl(&s,.016f);
    assert(!s.catalogRequested&&!s.sceneRequested&&!s.snapshot.admitted);
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
    assert(s.native.notice==NativeApi::MutationBlockReason());
    s.native.SuspendScene();s.native.Shutdown();
    assert(s.native.PerformanceScene()==0&&!s.snapshot.mutationsEnabled);
    std::cout<<"Actual plugin startup pump: 120 frames, repeated spawn/remove/move and lifecycle; zero game API pointers required\n";
}
