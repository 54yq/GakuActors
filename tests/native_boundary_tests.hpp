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
    static inline int allocated{},freed{},invoked{};
    static inline std::vector<std::string> diagnostics;
    static void* Tag(uintptr_t value){return reinterpret_cast<void*>(value);}
    static const void* Methods(void* c,void** it) {
        if(*it)return nullptr;*it=Tag(1);
        if(c==Tag(0x101))return Tag(0x202);
        if(c==Tag(0x102))return Tag(0x203);
        return nullptr;
    }
    static const char* Name(const void* m){return m==Tag(0x201)?"SyntheticUpdate":m==Tag(0x202)?"GetAwaiter":"GetResult";}
    static void* Result(const void* m){return m==Tag(0x201)?Tag(0x101):m==Tag(0x202)?Tag(0x102):Tag(0x103);}
    static char* TypeName(const void* t) {
        const char* name=t==Tag(0x101)?"SyntheticTask":t==Tag(0x102)?"SyntheticAwaiter":t==Tag(0x103)?"System.ValueTuple<System.Boolean, SyntheticActor>":"System.Boolean";
        const size_t n=std::strlen(name)+1;char* copy=new char[n];std::memcpy(copy,name,n);++allocated;return copy;
    }
    static const void* Fields(void*,void** it){if(*it)return nullptr;*it=Tag(1);return Tag(0x301);}
    template<class Check> static void MetadataOnly(Check check) {
        NativeApi api;allocated=freed=invoked=0;diagnostics.clear();
        api.trace=[](const char* line){diagnostics.emplace_back(line);};
        api.typeName_=TypeName;api.il2cppFree_=[](void* p){++freed;delete[] static_cast<char*>(p);};
        api.classType_=[](void* c)->const void*{return c;};api.fromType_=[](void* t)->void*{return t;};
        api.parent_=[](void*)->void*{return nullptr;};api.methods_=Methods;api.methodName_=Name;
        api.methodCount_=[](const void*)->uint32_t{return 0;};
        api.methodFlags_=[](const void*,uint32_t*)->uint32_t{return 0;};api.result_=Result;
        api.fields_=Fields;api.fieldName_=[](const void*){return "Item1";};
        api.fieldType_=[](const void*)->void*{return Tag(0x104);};
        api.invoke_=[](const void*,void*,void**,void**)->void*{++invoked;return nullptr;};
        // Synthetic tuple deliberately lacks index getters, matching the old
        // failure condition. Discover its shape without executing the task.
        api.TraceSignature("synthetic",Tag(0x201));api.TraceTaskContract(Tag(0x201));
        check(invoked==0);check(allocated>0&&allocated==freed);
        check(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& s){return s.find("GetResult() -> System.ValueTuple")!=std::string::npos;}));
        check(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& s){return s.find("::Item1 -> System.Boolean")!=std::string::npos;}));
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
        check(!recovery.DiagnoseContracts());check(!recovery.DiagnoseActors());
        MetadataOnly(check);
    }
};
}
