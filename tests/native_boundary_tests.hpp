#pragma once
namespace GakuActors {
struct NativeApiTestAccess {
    static inline bool valueType{},exception{};
    static inline void* received{};
    static void* ObjectClass(void*){return reinterpret_cast<void*>(0x20);}
    static bool IsValue(void*){return valueType;}
    static void* Unbox(void*){return reinterpret_cast<void*>(0x40);}
    static const char* MethodName(const void*){return "SyntheticMethod";}
    static void* Invoke(const void*,void* instance,void**,void** error) {
        received=instance;if(exception)*error=reinterpret_cast<void*>(0x60);return reinterpret_cast<void*>(0x80);
    }
    template<class Check> static void Run(Check check) {
        NativeApi api;api.objectClass_=ObjectClass;api.isValue_=IsValue;api.unbox_=Unbox;api.invoke_=Invoke;api.methodName_=MethodName;
        auto method=reinterpret_cast<const void*>(0x10);auto object=reinterpret_cast<void*>(0x30);
        valueType=false;exception=false;check(api.Call(method,object)==reinterpret_cast<void*>(0x80));check(received==object);
        valueType=true;api.Call(method,object);check(received==reinterpret_cast<void*>(0x40));
        api.Call(method);check(received==nullptr);
        bool rejected=false;try{api.Call(nullptr,object);}catch(const std::exception&){rejected=true;}check(rejected);
        exception=true;rejected=false;try{api.Call(method,object);}catch(const std::exception&){rejected=true;}check(rejected);
        // Recovery policy must hold below the UI, with a valid preset, an
        // admitted scene and repeated commands. All game API pointers are null:
        // reaching any native operation would fail the test immediately.
        received=nullptr;check(!api.RemoveActor(123));check(received==nullptr);
        check(!api.StartSpawn(Preset{},123));check(received==nullptr);
        NativeApi recovery;recovery.SetAdmittedScene(7);
        Preset valid{"id","test","hume","hume-cstm-0000","costume_head_hume-cstm-0000"};
        check(!recovery.MutationsEnabled());
        for(int i=0;i<128;++i)check(!recovery.StartSpawn(valid,123));
        check(recovery.notice==NativeApi::MutationBlockReason());
        check(!recovery.RemoveActor(123));
        float position[]{1,2,3};check(!recovery.MoveActor(123,position));
        recovery.SuspendScene();check(recovery.PerformanceScene()==0);
        recovery.SetAdmittedScene(7);recovery.Shutdown();check(recovery.PerformanceScene()==0);
    }
};
}
