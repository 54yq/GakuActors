#pragma once
namespace GakuActors {
struct NativeApiTestAccess {
    static inline int diagnosticFieldReads{};
    template<class Check> static void LiveDiagnosticFieldGuard(Check check) {
        NativeApi api;diagnosticFieldReads=0;
        api.objectClass_=[](void*)->void*{return Tag(0xd01);};
        api.parent_=[](void*)->void*{return nullptr;};
        api.fields_=[](void*,void** it)->const void*{if(*it)return nullptr;*it=Tag(1);return Tag(0xd02);};
        api.fieldName_=[](const void*){return "candidate";};
        api.fieldType_=[](const void*)->void*{return Tag(0xd03);};
        api.fieldFlags_=[](const void*)->uint32_t{return 0;};
        api.byRef_=[](void*){return false;};api.typeCode_=[](void*){return 8;};
        api.fieldValue_=[](const void*,void*)->void*{++diagnosticFieldReads;return nullptr;};
        check(!api.DiagnosticReferenceField(Tag(0xd04),"candidate")&&diagnosticFieldReads==0);
        api.typeCode_=[](void*){return 0x12;};api.byRef_=[](void*){return true;};
        check(!api.DiagnosticReferenceField(Tag(0xd04),"candidate")&&diagnosticFieldReads==0);
        api.byRef_=[](void*){return false;};
        check(!api.DiagnosticReferenceField(Tag(0xd04),"candidate")&&diagnosticFieldReads==1);
        api.typeCode_=[](void*){return 0x0e;};
        check(!api.DiagnosticReferenceField(Tag(0xd04),"candidate")&&diagnosticFieldReads==2);
        api.fieldFlags_=[](const void*)->uint32_t{return 0x10;};
        check(!api.DiagnosticReferenceField(Tag(0xd04),"candidate")&&diagnosticFieldReads==2);
    }
    struct FakeResource {int id,scene;bool active;};
    static inline FakeResource fakeResource{41,-7,true};
    static inline int fakeInt{},fakeScene{};static inline bool fakeBool{},fakeSetterFails{};
    static void* ResourceClass(void* p) {return p==&fakeScene?Tag(0x410):p==&fakeResource?Tag(0x411):Tag(0x412);}
    static const void* ResourceMethods(void* c,void** it) {
        if(c!=Tag(0x411))return nullptr;
        uintptr_t index=reinterpret_cast<uintptr_t>(*it);*it=Tag(index+1);
        return index<4?Tag(0x420+index):nullptr;
    }
    static const char* ResourceMethodName(const void* m) {
        if(m==Tag(0x420))return "GetInstanceID";
        if(m==Tag(0x421))return "get_scene";
        if(m==Tag(0x422))return "get_activeSelf";
        return "SetActive";
    }
    static void* ResourceInvoke(const void* m,void* object,void** args,void** ex) {
        if(object!=&fakeResource){*ex=Tag(0x499);return nullptr;}
        if(m==Tag(0x420)){fakeInt=fakeResource.id;return &fakeInt;}
        if(m==Tag(0x421)){fakeScene=fakeResource.scene;return &fakeScene;}
        if(m==Tag(0x422)){fakeBool=fakeResource.active;return &fakeBool;}
        if(m==Tag(0x423)){fakeResource.active=*static_cast<bool*>(args[0]);if(fakeSetterFails)*ex=Tag(0x499);return nullptr;}
        return nullptr;
    }
    template<class Check> static void ResourceWrites(Check check) {
        NativeApi api;fakeResource={41,-7,true};fakeSetterFails=false;
        api.objectClass_=ResourceClass;api.parent_=[](void*)->void*{return nullptr;};
        api.methods_=ResourceMethods;api.methodName_=ResourceMethodName;
        api.methodCount_=[](const void* m)->uint32_t{return m==Tag(0x423)?1:0;};
        api.methodFlags_=[](const void*,uint32_t*)->uint32_t{return 0;};
        api.param_=[](const void*,uint32_t)->void*{return Tag(2);};
        api.result_=[](const void* m)->void*{return Tag(m==Tag(0x420)?8:m==Tag(0x422)?2:m==Tag(0x423)?1:0x11);};
        api.typeCode_=[](void* t)->int{return static_cast<int>(reinterpret_cast<uintptr_t>(t));};
        api.byRef_=[](void*){return false;};api.isValue_=[](void* c){return c==Tag(0x410);};
        api.invoke_=ResourceInvoke;api.unbox_=[](void* p)->void*{return p;};
        api.valueSize_=[](void*,uint32_t*)->int{return 4;};
        api.newHandle_=[](void* p,bool)->void*{return p;};api.handleTarget_=[](void* p)->void*{return p;};api.freeHandle_=[](void*){};
        check(api.SaveAndWriteResource(&fakeResource,&fakeResource,-7,SceneResourceKind::Background,"activeSelf",false));
        check(!fakeResource.active&&api.resourceLeases_.size()==1);
        fakeResource.scene=-8;check(!api.RestoreResources());check(!fakeResource.active);
        fakeResource.scene=-7;check(api.RestoreResources()&&fakeResource.active&&api.resourceLeases_.empty());
        fakeSetterFails=true;bool threw=false;
        try{api.SaveAndWriteResource(&fakeResource,&fakeResource,-7,SceneResourceKind::Background,"activeSelf",false);}catch(...){threw=true;}
        check(threw&&!fakeResource.active&&api.resourceLeases_.size()==1);
        fakeSetterFails=false;check(api.RestoreResources()&&fakeResource.active);
        fakeResource.active=false;
        check(api.SaveAndWriteResource(&fakeResource,&fakeResource,-7,SceneResourceKind::Background,"activeSelf",false));
        check(api.RestoreResources()&&!fakeResource.active);
    }
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
    static inline size_t indexPosition{},indexCalls{},indexFreed{};
    static inline bool indexMore{};
    template<class Check> static void ResourceIndexBudget(Check check) {
        NativeApi api;Catalog catalog;indexPosition=indexCalls=indexFreed=0;
        check(!api.StartResourceScan(catalog)); // no scene: zero native exports required
        api.StepResourceScan(catalog);check(!api.ResourceScanActive());
        api.objectClass_=[](void* o)->void*{return o==Tag(0xd02)?Tag(0xe02):Tag(0xe01);};
        api.isValue_=[](void*){return false;};
        api.classNamespace_=[](void*){return "System";};api.className_=[](void* c){return c==Tag(0xe02)?"String":"SyntheticIterator";};
        api.stringLength_=[](void*){return 9;};api.stringChars_=[](void*)->const wchar_t*{return L"unrelated";};
        api.unbox_=[](void*)->void*{return &indexMore;};
        api.methods_=[](void*,void**)->const void*{return nullptr;};api.parent_=[](void*)->void*{return nullptr;};
        api.handleTarget_=[](void* h)->void*{return h;};api.newHandle_=[](void* o,bool)->void*{return o;};
        api.freeHandle_=[](void* h){if(h==Tag(0xd01))++indexFreed;};
        api.invoke_=[](const void* m,void*,void**,void**)->void* {
            ++indexCalls;
            if(m==Tag(0xf01)){indexMore=indexPosition++<600;return Tag(0xd03);}
            return Tag(0xd02);
        };
        api.SetAdmittedScene(-50);api.resourceScan_.iterator=Tag(0xd01);api.resourceScan_.moveNext=Tag(0xf01);api.resourceScan_.current=Tag(0xf02);
        api.StepResourceScan(catalog);check(api.resourceScan_.visited==512&&api.ResourceScanActive());
        check(indexCalls==1024); // 512 MoveNext + Current, no bundle loader/converter call
        api.StepResourceScan(catalog);check(!api.ResourceScanActive());check(indexFreed==1);
        check(catalog.resources.empty()&&catalog.resourceNotice.find("600")!=std::string::npos);
        api.resourceScan_.iterator=Tag(0xd01);api.resourceScan_.moveNext=Tag(0xf01);api.resourceScan_.current=Tag(0xf02);
        const size_t previous=indexCalls;api.SetAdmittedScene(0);api.StepResourceScan(catalog);
        check(indexCalls==previous&&indexFreed==2&&!api.ResourceScanActive());
    }
    template<class Check> static void Run(Check check) {
        check(NativeApi::SupportedProp("Root_Microphone","Prop_Microphone"));
        check(NativeApi::SupportedPropGeometry("Root_Hknesuitcase","Prop_Hknesuitcase","Geo_Hknesuitcase"));
        check(!NativeApi::SupportedPropGeometry("Root_Microphone","Prop_Microphone","Geo_Hknesuitcase"));
        check(!NativeApi::SupportedPropGeometry("Root_Hknesuitcase","Prop_Hknesuitcase","Geo_Body"));
        check(NativeApi::SupportedProp("Root_Penlight","Prop_Penlight"));

        {
            struct Node { std::string name; std::vector<void*> children; };
            Node microphone{"Prop_Microphone"};
            Node suitcase{"Prop_Hknesuitcase"}, geometry{"Geo_Hknesuitcase"};
            Node penlight{"Prop_Penlightklljduo"};
            Node waterGun{"Prop_Water_Gun"}, candy{"Prop_CharacterCandy"}, pinwheel{"Prop_Pinwheel"};
            Node body{"Prop_Body"}, duplicate{"Prop_Duplicate"}, duplicate2{"Prop_Duplicate"};
            Node unrelated{"Prop_StageOnly"}, foreignProp{"Prop_Penlight"};
            Node rootMic{"Root_Microphone", {&microphone}};
            Node rootBag{"Root_Hknesuitcase", {&suitcase, &geometry}};
            Node rootLight{"Root_Penlightklljduo", {&penlight}};
            Node rootGun{"Root_Water_Gun", {&waterGun}}, rootCandy{"Root_CharacterCandy", {&candy}};
            Node rootPinwheel{"Root_Pinwheel", {&pinwheel}};
            Node rootBody{"Root_Body", {&body}}, rootDuplicate{"Root_Duplicate", {&duplicate, &duplicate2}};
            Node rootForeign{"Root_Penlight", {&foreignProp}};
            Node foreignActor{"Actor", {&rootForeign}}, wrapper{"Model", {&rootBag, &rootLight, &rootGun, &rootCandy, &rootPinwheel, &rootBody, &rootDuplicate}};
            Node actor{"Actor", {&wrapper, &rootMic, &unrelated, &foreignActor}};
            const auto props = scene_props::ScanActorProps(&actor,
                [](void* node) { return static_cast<Node*>(node)->children; },
                [](void* node) { return static_cast<Node*>(node)->name; },
                [&](void* node) { return node == &foreignActor; });
            const bool complete = props.size() == 6 && std::all_of(props.begin(), props.end(), [&](const auto& prop) {
                return prop.object == &microphone || prop.object == &suitcase || prop.object == &penlight ||
                    prop.object == &waterGun || prop.object == &candy || prop.object == &pinwheel;
            });
            check(complete && scene_props::DisplayName("Prop_Penlightklljduo") == "荧光棒（Penlightklljduo）");
            check(scene_props::DisplayName("Prop_Water_Gun") == "水枪（Water_Gun）" &&
                scene_props::DisplayName("Prop_CharacterCandy") == "糖果（CharacterCandy）" &&
                scene_props::DisplayName("Prop_Pinwheel") == "Prop_Pinwheel");
        }

        check(!NativeApi::SupportedProp("Root_Body","Prop_Body"));
        check(!NativeApi::SupportedProp("Root_Microphone","Prop_Body"));
        check(!NativeApi::SupportedProp("Root_","Prop_"));
        NativeApi resources;SceneResource unknown;unknown.generation=1;unknown.instance=10;
        check(resources.ScanResources().empty());
        check(!resources.SetResourceHidden(unknown,true));
        check(resources.RestoreResources());
        const unsigned char sample[]{'a','b','c'};check(NativeApi::ThumbnailBase64(sample,3)=="YWJj");
        check(NativeApi::ThumbnailBase64(sample,2)=="YWI=");
        check(NativeApi::ThumbnailBase64(sample,1)=="YQ==");
        std::string thumbnailStatus;check(resources.ReadLoadedThumbnail("test",thumbnailStatus).empty());
        check(resources.RestoreCamera());float color[]{0,1,0,1};check(!resources.ApplyCameraColor(color));
        float same[]{0,1,0,1},different[]{0,0,0,1};check(resources.CameraSame(color,same));check(!resources.CameraSame(color,different));
        check(NativeApi::ProtectedSceneName("characteractor"));check(NativeApi::ProtectedSceneName("wall_collider"));
        check(!NativeApi::ProtectedSceneName("stage_decor"));
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
        recovery.pending_.active=true;recovery.pending_.lease.preset="id";
        recovery.SyncPresetActorNames(valid);check(recovery.pending_.displayName=="test");
        recovery.pending_.active=false;
        recovery.SetAdmittedScene(7);recovery.Shutdown();check(recovery.PerformanceScene()==0);
        check(!recovery.DiagnoseContracts());check(!recovery.DiagnoseActors());check(!recovery.DiagnoseActorManagerRelations());check(!recovery.DiagnoseLiveBindings());
        check(recovery.EnableCreation(true));check(recovery.MutationsEnabled());
        check(!recovery.StartSpawn(valid,123)); // No runtime exports: cannot mutate.
        recovery.Fault("synthetic ambiguous ownership");check(!recovery.MutationsEnabled());
        check(!recovery.EnableCreation(true));
        recovery.SuspendScene();check(!recovery.EnableCreation(true)); // scene changes do not reset a fault
        check(!recovery.DestroyLease(ActorLease{})); // unproven lease enters no game API
        // Both apply and restore must be blocked before accessing the game
        // after the observed actor identity change.
        NativeApi model;model.SetAdmittedScene(7);model.module_=reinterpret_cast<HMODULE>(1);
        model.invoke_=Invoke;model.freeHandle_=[](void*){};model.domainGet_=[]()->void*{return nullptr;};
        model.assemblies_=[](void*,size_t* n)->const void**{*n=0;return nullptr;};
        check(!model.MutationsEnabled());
        check(!model.StartActorModel(123,7,"test",{"body","face","hair"},false));
        check(model.notice.find("暂时停用")!=std::string::npos&&!model.Busy());
        check(!model.StartActorModel(123,7,"test",{},true)&&model.notice.find("暂时停用")!=std::string::npos&&!model.Busy());
        MetadataOnly(check);
        TupleDecoder(check);
        PendingAndRemovalGuards(check);
        ResourceIndexBudget(check);
        ResourceWrites(check);
        LiveDiagnosticFieldGuard(check);
    }
};
}
