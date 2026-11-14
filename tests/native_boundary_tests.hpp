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
    static inline bool tupleSuccess{},wrongFieldType{};
    static inline void* tupleActor{};
    static const void* TupleFields(void* c,void** it) {
        if(c!=Tag(0x701))return nullptr;
        const auto n=reinterpret_cast<uintptr_t>(*it);*it=Tag(n+1);
        return n==0?Tag(0x901):n==1?Tag(0x902):nullptr;
    }
    template<class Check> static void TupleDecoder(Check check) {
        NativeApi api;invoked=0;tupleSuccess=true;wrongFieldType=false;tupleActor=Tag(0xa01);
        api.objectClass_=[](void* o)->void*{return o==Tag(0x801)?Tag(0x701):Tag(0x700);};
        api.isValue_=[](void* c){return c==Tag(0x701);};api.parent_=[](void*)->void*{return nullptr;};
        api.classNamespace_=[](void* c){return c==Tag(0x701)?"System":"Campus.Common";};
        api.className_=[](void* c){return c==Tag(0x701)?"ValueTuple`2":c==Tag(0x702)?"ICampusActorController":"WrongType";};
        api.fields_=TupleFields;api.fieldName_=[](const void* f){return f==Tag(0x901)?"Item1":"Item2";};
        api.fieldType_=[](const void* f)->void*{return f==Tag(0x901)?Tag(0xb01):wrongFieldType?Tag(0xb03):Tag(0xb02);};
        api.typeCode_=[](void* t){return t==Tag(0xb01)?2:0x12;};
        api.fromType_=[](void* t)->void*{return t==Tag(0xb02)?Tag(0x702):Tag(0x703);};
        api.fieldValue_=[](const void* f,void*)->void*{return f==Tag(0x901)?Tag(0x803):tupleActor;};
        api.unbox_=[](void*)->void*{return &tupleSuccess;};
        api.newHandle_=[](void* o,bool)->void*{return o;};api.handleTarget_=[](void* h)->void*{return h;};api.freeHandle_=[](void*){};
        api.invoke_=[](const void*,void*,void**,void**)->void*{++invoked;return nullptr;};
        const auto ok=api.DecodeBuildResult(Tag(0x801),Tag(0x701));check(ok.success&&ok.actor==tupleActor);
        tupleSuccess=false;const auto failed=api.DecodeBuildResult(Tag(0x801),Tag(0x701));check(!failed.success);
        tupleActor=nullptr;check(api.DecodeBuildResult(Tag(0x801),Tag(0x701)).actor==nullptr);
        bool rejected=false;wrongFieldType=true;
        try{api.DecodeBuildResult(Tag(0x801),Tag(0x701));}catch(const std::exception&){rejected=true;}check(rejected);
        wrongFieldType=false;rejected=false;
        try{api.DecodeBuildResult(Tag(0x802),Tag(0x701));}catch(const std::exception&){rejected=true;}check(rejected);
        check(invoked==0); // No GetResult/index/property call during field decoding.
    }
    template<class Check> static void PendingAndRemovalGuards(Check check) {
        NativeApi api;invoked=0;
        api.module_=reinterpret_cast<HMODULE>(1);api.freeHandle_=[](void*){};
        api.invoke_=[](const void*,void*,void**,void**)->void*{++invoked;return nullptr;};
        api.EnableCreation(true);api.pending_.active=true;
        Preset valid{"id","test","hume","body","head"};
        for(int i=0;i<16;++i)check(!api.StartSpawn(valid,123));
        check(api.Busy()&&invoked==0);
        float position[]{1,2,3};check(!api.RemoveActor(-101));check(!api.MoveActor(-101,position));
        api.pending_.active=false;
        ActorLease forged;forged.manager=3;forged.ready=true;forged.identity.instance=-101;forged.identity.scene=-50;
        api.SetAdmittedScene(-50);api.owned_.push_back(forged);
        check(!api.RemoveActor(-101));check(api.Faulted()&&invoked==0);
        check(!api.EnableCreation(true));check(!api.StartSpawn(valid,123));
        api.Shutdown();check(invoked==0);
    }
    template<class Check> static void Run(Check check) {
        NativeApi api;api.objectClass_=ObjectClass;api.isValue_=IsValue;api.unbox_=Unbox;api.invoke_=Invoke;api.methodName_=MethodName;
        auto method=reinterpret_cast<const void*>(0x10);auto object=reinterpret_cast<void*>(0x30);
        valueType=false;exception=false;check(api.Call(method,object)==reinterpret_cast<void*>(0x80));check(received==object);
        valueType=true;api.Call(method,object);check(received==reinterpret_cast<void*>(0x40));
        api.Call(method);check(received==nullptr);
        bool rejected=false;try{api.Call(nullptr,object);}catch(const std::exception&){rejected=true;}check(rejected);
        exception=true;rejected=false;try{api.Call(method,object);}catch(const std::exception&){rejected=true;}check(rejected);
        // Default-off policy must hold below the UI, with a valid preset, an
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
        check(recovery.EnableCreation(true));check(recovery.MutationsEnabled());
        check(!recovery.StartSpawn(valid,123)); // No runtime exports: cannot mutate.
        recovery.Fault("synthetic ambiguous ownership");check(!recovery.MutationsEnabled());
        check(!recovery.EnableCreation(true));
        recovery.SuspendScene();check(!recovery.EnableCreation(true)); // scene changes do not reset a fault
        check(!recovery.DestroyLease(ActorLease{})); // unproven lease enters no game API
        MetadataOnly(check);
        TupleDecoder(check);
        PendingAndRemovalGuards(check);
    }
};
}
