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
    s.modelQueue.push_back(Preset{"id","queued","hski","body","head"});
    TickImpl(&s,.016f);
    assert(!s.catalogRequested&&!s.sceneRequested&&!s.snapshot.admitted);
    assert(s.modelQueue.empty()); // no deferred model resolution leaks into startup
    s.commands.push_back({Action::Spawn,Preset{"id","test","hski","body","head"},1});
    TickImpl(&s,.016f);
    assert(s.native.owned.empty()&&s.native.pending.task==nullptr);
    std::cout<<"Actual plugin startup pump: 120 frames, denied scan and denied spawn; zero game API pointers required\n";
}
