#pragma once
namespace GakuActors {
struct NativeApiTestAccess {
    static inline bool valueType{},exception{};
    static inline void* received{};
    static void* ObjectClass(void*){return reinterpret_cast<void*>(0x20);}
    static bool IsValue(void*){return valueType;}
    static void* Unbox(void*){return reinterpret_cast<void*>(0x40);}
    static void* Invoke(const void*,void* instance,void**,void** error) {
        received=instance;if(exception)*error=reinterpret_cast<void*>(0x60);return reinterpret_cast<void*>(0x80);
    }
    template<class Check> static void Run(Check check) {
        NativeApi api;api.objectClass_=ObjectClass;api.isValue_=IsValue;api.unbox_=Unbox;api.invoke_=Invoke;
        auto method=reinterpret_cast<const void*>(0x10);auto object=reinterpret_cast<void*>(0x30);
        valueType=false;exception=false;check(api.Call(method,object)==reinterpret_cast<void*>(0x80));check(received==object);
        valueType=true;api.Call(method,object);check(received==reinterpret_cast<void*>(0x40));
        api.Call(method);check(received==nullptr);
        bool rejected=false;try{api.Call(nullptr,object);}catch(const std::exception&){rejected=true;}check(rejected);
        exception=true;rejected=false;try{api.Call(method,object);}catch(const std::exception&){rejected=true;}check(rejected);
        // An official actor cannot be destroyed simply by forging its instance
        // ID in a UI command; no native method is entered without ownership.
        received=nullptr;check(!api.RemoveActor(123));check(received==nullptr);
        check(!api.StartSpawn(Preset{},123));check(received==nullptr);
    }
};
}
