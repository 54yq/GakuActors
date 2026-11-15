#pragma once
#include "Catalog.hpp"
#include "ModelReadiness.hpp"
#include "ActorOwnership.hpp"
#include "ActorModelAssets.hpp"
#include "ResourceCatalog.hpp"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace GakuActors {
// IL2CPP calls are restricted to the loader's Unity-thread callbacks. No game
// addresses, field offsets or managed objects are persisted in presets/UI.
class NativeApi {
    friend struct NativeApiTestAccess;
    HMODULE module_{};
    void* (*domainGet_)(){};
    const void** (*assemblies_)(void*,size_t*){};
    void* (*image_)(const void*){};
    void* (*classFind_)(void*,const char*,const char*){};
    void* (*objectClass_)(void*){};
    void* (*parent_)(void*){};
    bool (*isValue_)(void*){};
    bool (*isEnum_)(void*){};
    bool (*assignable_)(void*,void*){};
    void* (*objectNew_)(void*){};
    void* (*valueBox_)(void*,void*){};
    const char* (*className_)(void*){};
    const char* (*classNamespace_)(void*){};
    void* (*nestedTypes_)(void*,void**){};
    const void* (*methods_)(void*,void**){};
    const char* (*methodName_)(const void*){};
    uint32_t (*methodCount_)(const void*){};
    uint32_t (*methodFlags_)(const void*,uint32_t*){};
    void* (*param_)(const void*,uint32_t){};
    void* (*result_)(const void*){};
    int (*typeCode_)(void*){};
    bool (*byRef_)(void*){};
    void* (*fromType_)(void*){};
    void* (*invoke_)(const void*,void*,void**,void**){};
    void* (*unbox_)(void*){};
    int (*stringLength_)(void*){};
    const wchar_t* (*stringChars_)(void*){};
    void* (*stringNew_)(const char*){};
    const void* (*classType_)(void*){};
    void* (*typeObject_)(const void*){};
    uintptr_t (*arrayLength_)(void*){};
    void* (*arrayNew_)(void*,uintptr_t){};
    void* (*newHandle_)(void*,bool){};
    void* (*handleTarget_)(void*){};
    void (*freeHandle_)(void*){};
    int (*valueSize_)(void*,uint32_t*){};
    char* (*typeName_)(const void*){};
    void (*il2cppFree_)(void*){};
    const void* (*fields_)(void*,void**){};
    const char* (*fieldName_)(const void*){};
    void* (*fieldType_)(const void*){};
    uint32_t (*fieldFlags_)(const void*){};
    void* (*fieldValue_)(const void*,void*){};
    bool creationEnabled_{},faulted_{};
    struct ResourceScan {
        void* iterator{};const void* moveNext{},*current{},*converter{};
        std::vector<ResourceNamePattern> patterns;
        size_t visited{},added{},candidates{};
        std::vector<std::string> samples;
    } resourceScan_;
    std::vector<ActorLease> owned_;
    struct PoseBaseline {int instance{},scene{},index{};std::string unique;float position[3]{};};
    std::vector<PoseBaseline> poseBaselines_;
    struct Pending {
        bool active{},cancelled{};
        void* task{},*awaiter{},*cancellation{},*resultClass{};
        ActorLease lease;ModelAssets requested;
        std::string displayName;
        std::vector<ActorIdentity> baseline;
        float position[3]{},rotation[4]{0,0,0,1};
        std::chrono::steady_clock::time_point started;
    } pending_;
    struct ModelEdit {
        bool active{},restoring{},cancelled{};
        int manager{};ActorIdentity identity;
        void* original{},*task{},*awaiter{},*cancellation{},*resultClass{};
        std::array<std::string,3> requested{};
        std::vector<ActorIdentity> baseline;
        std::chrono::steady_clock::time_point started;
    } modelEdit_;
    std::vector<void*> frameRoots_;
    struct PreLivePending {void* list{},*original{},*replacement{},*fixed{},*task{};bool active{};} preLivePending_;
    int admittedScene_{};
    struct ResourceLease {
        void* handle{};void* ownerHandle{};int instance{},owner{},scene{};
        uint64_t generation{};SceneResourceKind kind{};bool original{},written{};
        std::string property,root,prop;void* actorHandle{};
    };
    std::vector<ResourceLease> resourceLeases_;
    struct ResourceCandidate { SceneResource item;void* handle{};void* actorHandle{};std::string root,prop; };
    std::vector<ResourceCandidate> resourceCandidates_;
    uint64_t resourceGeneration_{1};
    struct CameraLease {
        void* handle{};int scene{},instance{},originalFlags{},writtenFlags{};
        float originalColor[4]{},writtenColor[4]{};
        bool flagsWritten{},colorWritten{};
    } cameraLease_;
    const void* activeSceneMethod_{};
    template<class T> bool Export(T& fn,const char* name){fn=reinterpret_cast<T>(GetProcAddress(module_,name));return fn!=nullptr;}
public:
    void (*trace)(const char*){};
    void Trace(const char* message){if(trace)trace(message);}
    struct Pin {
        NativeApi* api{};void* handle{};
        Pin(NativeApi& a,void* object):api(&a),handle(object?a.newHandle_(object,false):nullptr){}
        ~Pin(){if(handle)api->freeHandle_(handle);}
        Pin(const Pin&)=delete;
        void* Get()const{return handle?api->handleTarget_(handle):nullptr;}
    };
    bool MutationsEnabled()const{return creationEnabled_&&!faulted_;}
    bool Busy()const{return pending_.active||modelEdit_.active;}
    bool Faulted()const{return faulted_;}
    bool EnableCreation(bool enabled) {
        if(enabled&&faulted_)return false;
        creationEnabled_=enabled;return true;
    }
    static constexpr const char* MutationBlockReason(){
        return "人物操作未启用或已因故障锁定；请查看诊断日志";
    }
    bool RejectMutation(const char* operation) {
        if(!faulted_)notice=MutationBlockReason();
        Trace((std::string("Scene mutation BLOCKED: ")+operation).c_str());
        return false;
    }
    std::string notice{"等待游戏运行时"};
    bool Ready()const{return module_&&invoke_&&freeHandle_;}
    bool Resolve(void* module) {
        module_=static_cast<HMODULE>(module);if(!module_)return false;
        const bool ok=Export(domainGet_,"il2cpp_domain_get")&&Export(assemblies_,"il2cpp_domain_get_assemblies")&&
            Export(image_,"il2cpp_assembly_get_image")&&Export(classFind_,"il2cpp_class_from_name")&&
            Export(objectClass_,"il2cpp_object_get_class")&&Export(parent_,"il2cpp_class_get_parent")&&
            Export(isValue_,"il2cpp_class_is_valuetype")&&Export(objectNew_,"il2cpp_object_new")&&
            Export(isEnum_,"il2cpp_class_is_enum")&&
            Export(assignable_,"il2cpp_class_is_assignable_from")&&
            Export(className_,"il2cpp_class_get_name")&&Export(classNamespace_,"il2cpp_class_get_namespace")&&
            Export(methods_,"il2cpp_class_get_methods")&&Export(methodName_,"il2cpp_method_get_name")&&
            Export(methodCount_,"il2cpp_method_get_param_count")&&Export(methodFlags_,"il2cpp_method_get_flags")&&
            Export(param_,"il2cpp_method_get_param")&&Export(result_,"il2cpp_method_get_return_type")&&
            Export(typeCode_,"il2cpp_type_get_type")&&Export(byRef_,"il2cpp_type_is_byref")&&
            Export(fromType_,"il2cpp_class_from_type")&&Export(invoke_,"il2cpp_runtime_invoke")&&
            Export(unbox_,"il2cpp_object_unbox")&&Export(stringLength_,"il2cpp_string_length")&&
            Export(stringChars_,"il2cpp_string_chars")&&Export(stringNew_,"il2cpp_string_new")&&
            Export(classType_,"il2cpp_class_get_type")&&Export(typeObject_,"il2cpp_type_get_object")&&
            Export(arrayLength_,"il2cpp_array_length")&&Export(arrayNew_,"il2cpp_array_new")&&
            Export(newHandle_,"il2cpp_gchandle_new")&&Export(handleTarget_,"il2cpp_gchandle_get_target")&&
            Export(valueSize_,"il2cpp_class_value_size")&&Export(freeHandle_,"il2cpp_gchandle_free");
        if(!ok){module_=nullptr;notice="当前 IL2CPP 导出不完整";}
        // Optional read-only diagnostics must not affect normal startup.
        Export(typeName_,"il2cpp_type_get_name");Export(il2cppFree_,"il2cpp_free");
        Export(fields_,"il2cpp_class_get_fields");Export(fieldName_,"il2cpp_field_get_name");
        Export(fieldType_,"il2cpp_field_get_type");
        Export(fieldFlags_,"il2cpp_field_get_flags");
        Export(fieldValue_,"il2cpp_field_get_value_object");
        Export(valueBox_,"il2cpp_value_box");
        Export(nestedTypes_,"il2cpp_class_get_nested_types");
        return ok;
    }
    void* NewManagedString(const char* value){return stringNew_?stringNew_(value):nullptr;}
    const void* PhotoBackgroundMethod() {
        if(!nestedTypes_)return nullptr;
        void* rules=Class("Campus.Common.AssetNames","ABRule"),*photo=nullptr,*it=nullptr;if(!rules)return nullptr;
        while(void* candidate=nestedTypes_(rules,&it))if(std::strcmp(className_(candidate),"PhotoBackground")==0){if(photo)return nullptr;photo=candidate;}
        void* time=Class("Campus.Common.Proto.Client.Enums","PhotoBackgroundTimeType");uint32_t align{};
        if(!photo||!time||!isEnum_(time)||valueSize_(time,&align)!=4||align!=alignof(int))return nullptr;
        const auto m=Method(photo,"GetEnvAssetId",2,true,{0x0e,0x11});
        return m&&typeCode_(result_(m))==0x0e&&fromType_(param_(m,1))==time?m:nullptr;
    }
    void* Class(const char* ns,const char* name) {
        size_t count{};const auto items=assemblies_(domainGet_(),&count);
        if(!items||count>8192)return nullptr;
        for(size_t i=0;i<count;++i)if(void* c=classFind_(image_(items[i]),ns,name))return c;
        return nullptr;
    }
    std::string ClassName(void* klass) {
        if(!klass)return {};
        return std::string(classNamespace_(klass))+"."+className_(klass);
    }
    std::string TypeName(void* type) {
        if(!type)return "<missing>";
        if(!typeName_||!il2cppFree_)return "<type-name export unavailable>";
        char* raw=typeName_(type);if(!raw)return "<unknown>";
        struct Free {char* value;void(*release)(void*);~Free(){release(value);}} free{raw,il2cppFree_};
        return std::string(raw);
    }
    void TraceSignature(const char* owner,const void* method) {
        if(!method)return;
        uint32_t impl{};
        std::string line="Diagnostic contract: "+std::string(owner)+"::"+methodName_(method)+"(";
        const auto count=methodCount_(method);if(count>64)throw std::runtime_error("诊断参数数异常");
        for(uint32_t i=0;i<count;++i){if(i)line+=", ";line+=TypeName(param_(method,i));}
        line+=") -> "+TypeName(result_(method));
        line+=((methodFlags_(method,&impl)&0x10)?" [static]":" [instance]");Trace(line.c_str());
    }
    void TraceTypeShape(void* klass) {
        if(!klass)return;
        const auto owner=TypeName(const_cast<void*>(classType_(klass)));
        Trace(("Diagnostic result shape: "+owner).c_str());
        if(!fields_||!fieldName_||!fieldType_){Trace("Diagnostic fields: optional exports unavailable");return;}
        void* it=nullptr;size_t count=0;
        while(const void* field=fields_(klass,&it)) {
            if(++count>64){Trace("Diagnostic fields: truncated at 64");break;}
            const char* name=fieldName_(field);
            Trace(("Diagnostic field: "+owner+"::"+(name?name:"<unnamed>")+" -> "+TypeName(fieldType_(field))).c_str());
        }
        // Inspect metadata only; do not call getters, GetAwaiter or GetResult.
        for(const char* getter:{"get_Index","get_index","get_IsSuccess","get_IsGenerating"})
            TraceSignature(owner.c_str(),Method(klass,getter,0));
    }
    void TraceTaskContract(const void* update) {
        if(!update)return;
        void* task=fromType_(result_(update));
        if(!task)return;
        const auto awaiter=Method(task,"GetAwaiter",0);TraceSignature("task",awaiter);
        if(!awaiter)return;
        void* awaiterClass=fromType_(result_(awaiter));
        const auto result=Method(awaiterClass,"GetResult",0);TraceSignature("awaiter",result);
        if(result)TraceTypeShape(fromType_(result_(result)));
    }
    bool DiagnoseContracts() {
        if(!PerformanceScene())return false;
        Trace("Diagnostic contracts BEGIN (metadata only; no creation/update/destruction invoked)");
        const auto utility=Class("Campus.Common","CampusActorUtility");
        const auto manager=Class("Campus.Common","CampusActorManager");
        const auto actor=Class("Campus.Common","CampusActorController");
        const auto livePlayer=Class("Campus.Live","LivePlayerPresenter");
        const auto live3d=Class("Campus.Live","Live3DPresenter");
        const auto liveBinder=Class("Campus.Common.Live","CampusLiveBinder");
        const auto confirmModel=Class("Campus.OutGame","PictureBookLivePlayConfirmSheetModel");
        const auto confirmPresenter=Class("Campus.OutGame","PictureBookLivePlayConfirmSheetPresenter");
        const auto costumePresenter=Class("Campus.OutGame","PictureBookLiveCostumeSelectSheetPresenter");
        const auto selectPresenter=Class("Campus.OutGame","PictureBookLiveSelectScreenPresenter");
        const auto fixedData=Class("Campus.Live.Data","LiveFixedData");
        struct Target {void* klass;const char* label;std::vector<const char*> names;};
        for(const auto& target:std::vector<Target>{
            {utility,"CampusActorUtility",{"UpdateCacheActorAsync","CreateActorDescriptor"}},
            {manager,"CampusActorManager",{"CreateActor","UpdateActorAsync","SafeUpdateActorAsync","UpdateCacheActorAsync","GetFreeActorIndex","ContainsActor","GetActor","GetActorList","TryDestroyActor","DestroyActor"}},
            {actor,"CampusActorController",{"BuildAsync","get_descriptor","get_settings","get_index","GetModelPartsList","GetBuildResources","get_IsBuildSuccess"}},
            {livePlayer,"LivePlayerPresenter",{"get_ThreeDLive","get_Actors","PreloadAssetsAsync","LoadCharacterAsync","Initialize3DActorsAsync","CreateDescriptors","CreateActorAsync"}},
            {live3d,"Live3DPresenter",{"SetActorsAsync","SetupLiveBinderAsync","SetActorsActive","ResetActorModel"}},
            {liveBinder,"CampusLiveBinder",{"get_Actors","get_VirtualActors","BindActor","SetupAsync"}},
            {confirmModel,"LiveConfirmModel",{"get_Costumes","set_Costumes"}},
            {confirmPresenter,"LiveConfirmPresenter",{"UpdateCostumes","OnClickChangeButton"}},
            {costumePresenter,"LiveCostumePresenter",{"SetData","OnClickHead","OnClickBody"}},
            {selectPresenter,"LiveSelectPresenter",{"OpenPlayConfirmSheetAsync","OpenCostumeChangeSheetAsync","MoveLiveScene"}},
            {fixedData,"LiveFixedData",{".ctor","get_Descriptors","GetOriginalActorDescriptors","GetAdditionalActorDescriptors"}}}) {
            size_t depth=0,total=0;
            for(void* c=target.klass;c&&depth++<24;c=parent_(c)) {
                void* it=nullptr;
                while(const void* method=methods_(c,&it)) {
                    if(++total>8192)throw std::runtime_error("诊断方法数量异常");
                    const char* name=methodName_(method);if(!name)continue;
                    if(std::none_of(target.names.begin(),target.names.end(),[&](const char* n){return std::strcmp(n,name)==0;}))continue;
                    TraceSignature(target.label,method);
                    if(!std::strcmp(name,"UpdateActorAsync")||!std::strcmp(name,"UpdateCacheActorAsync"))TraceTaskContract(method);
                }
            }
        }
        for(void* klass:{livePlayer,live3d,liveBinder,confirmModel,fixedData})if(klass)TraceTypeShape(klass);
        Trace("Diagnostic contracts END");return true;
    }
    // Most-derived declaration wins; ambiguous overloads fail closed. Exact
    // primitive argument types are checked before potentially mutating calls.
    const void* Method(void* klass,const char* name,int count,bool isStatic=false,std::initializer_list<int> codes={}) {
        for(void* c=klass;c;c=parent_(c)) {
            const void* found=nullptr;void* it=nullptr;
            while(const void* m=methods_(c,&it)) {
                if(std::strcmp(methodName_(m),name)||methodCount_(m)!=static_cast<uint32_t>(count))continue;
                uint32_t impl{};if(((methodFlags_(m,&impl)&0x10)!=0)!=isStatic)continue;
                bool match=true;uint32_t i=0;
                for(const int code:codes){void* type=param_(m,i++);if(!type||byRef_(type)||(code&&typeCode_(type)!=code)){match=false;break;}}
                if(!match)continue;
                if(found)return nullptr;
                found=m;
            }
            if(found)return found;
        }
        return nullptr;
    }
    const void* On(void* object,const char* name,int count=0){return object?Method(objectClass_(object),name,count):nullptr;}
    void* Call(const void* method,void* target=nullptr,void** args=nullptr) {
        if(!method)throw std::runtime_error("原生方法缺失或签名不明确");
        void* exception=nullptr;
        if(target&&isValue_(objectClass_(target)))target=unbox_(target);
        void* value=invoke_(method,target,args,&exception);
        if(exception) {
            std::string message=std::string("游戏方法返回异常：")+methodName_(method);
            if(className_&&classNamespace_&&newHandle_&&handleTarget_&&freeHandle_)try {
                Pin error(*this,exception);message+=" ["+ClassName(objectClass_(error.Get()))+"]";
                if(const auto getter=On(error.Get(),"get_Message")) {
                    void* nested=nullptr;void* detail=invoke_(getter,error.Get(),nullptr,&nested);
                    if(!nested&&detail&&stringLength_&&stringChars_)message+=" "+String(detail);
                }
            }catch(...){}
            throw std::runtime_error(message);
        }
        return value;
    }
    std::string String(void* value) {
        if(!value||ClassName(objectClass_(value))!="System.String")return {};
        const int count=stringLength_(value);if(count<0||count>4096)return {};
        const auto chars=stringChars_(value);if(!chars||!count)return {};
        const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars,count,nullptr,0,nullptr,nullptr);
        std::string output(bytes,'\0');if(bytes)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars,count,output.data(),bytes,nullptr,nullptr);
        return output;
    }
    std::string Text(void* object,const char* name) {
        const auto method=On(object,name);return method?String(Call(method,object)):std::string{};
    }
    int Int(void* object,const char* name) {
        const auto m=On(object,name);
        if(!m||typeCode_(result_(m))!=8)throw std::runtime_error("整数属性签名不符");
        void* boxed=Call(m,object);if(!boxed)throw std::runtime_error("整数属性为空");return *static_cast<int*>(unbox_(boxed));
    }
    bool Bool(void* object,const char* name) {
        const auto m=On(object,name);
        if(!m||typeCode_(result_(m))!=2)throw std::runtime_error("布尔属性签名不符");
        void* boxed=Call(m,object);if(!boxed)throw std::runtime_error("布尔属性为空");return *static_cast<bool*>(unbox_(boxed));
    }
    std::vector<void*> Items(void* list) {
        std::vector<void*> result;if(!list)return result;
        const int count=Int(list,"get_Count");if(count<0||count>16384)throw std::runtime_error("目录条目数超出检查范围");
        const auto item=Method(objectClass_(list),"get_Item",1,false,{8});
        if(!item&&count)throw std::runtime_error("目录索引接口不可用");
        for(int i=0;i<count;++i){void* args[]{&i};if(void* value=Call(item,list,args))result.push_back(value);}
        return result;
    }
    std::vector<void*> Objects(void* klass) {
        if(!klass)return {};
        void* resources=Class("UnityEngine","Resources");
        const auto find=Method(resources,"FindObjectsOfTypeAll",1,true,{0x12});
        Pin type(*this,typeObject_(classType_(klass)));void* args[]{type.Get()};
        Pin array(*this,Call(find,nullptr,args));if(!array.Get())return {};
        const auto count=arrayLength_(array.Get());if(count>16384)throw std::runtime_error("场景对象数量超出检查范围");
        // Unity x64 array header: Object + bounds + max_length. This ABI is
        // independently checked by the PC metadata/host target (64-bit only).
        auto items=reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*));
        std::vector<void*> output;
        for(uintptr_t i=0;i<count;++i)if(items[i]) {
            void* h=newHandle_(items[i],false);if(!h)throw std::runtime_error("无法保留场景对象");
            frameRoots_.push_back(h);output.push_back(items[i]);
        }
        return output;
    }
    void EndFrame(){for(void* h:frameRoots_)freeHandle_(h);frameRoots_.clear();}
    void* GameObject(void* component){return Call(On(component,"get_gameObject"),component);}
    int SceneOf(void* go) {
        void* boxed=Call(On(go,"get_scene"),go);if(!boxed)return 0;
        uint32_t align{};if(valueSize_(objectClass_(boxed),&align)!=4)throw std::runtime_error("Scene 布局不匹配");
        return *static_cast<int*>(unbox_(boxed));
    }
    std::string ActiveSceneName() {
        if(!activeSceneMethod_)activeSceneMethod_=Method(Class("UnityEngine.SceneManagement","SceneManager"),"GetActiveScene",0,true);
        Pin scene(*this,Call(activeSceneMethod_));
        return scene.Get()?Text(scene.Get(),"get_name"):std::string{};
    }
    struct ActorScene {std::string owner,active;int handle{};};
    ActorScene ObserveActorScene(void* actor) {
        if(!actor)return {};
        void* go=GameObject(actor);
        Pin scene(*this,Call(On(go,"get_scene"),go));
        ActorScene out;out.handle=SceneOf(go);out.owner=Text(scene.Get(),"get_name");out.active=ActiveSceneName();return out;
    }
    void SetAdmittedScene(int scene){if(scene!=admittedScene_){poseBaselines_.clear();if(modelEdit_.original&&!modelEdit_.active)DropModelEdit();}admittedScene_=scene;}
    int PerformanceScene()const{return admittedScene_;}
    void Pose(void* actor,float* position,float* rotation) {
        void* transform=Call(On(actor,"get_transform"),actor);
        void* p=Call(On(transform,"get_position"),transform);
        void* r=Call(On(transform,"get_rotation"),transform);
        uint32_t a{};
        if(!p||!r||valueSize_(objectClass_(p),&a)!=12||valueSize_(objectClass_(r),&a)!=16)throw std::runtime_error("角色位置结构不匹配");
        std::memcpy(position,unbox_(p),12);std::memcpy(rotation,unbox_(r),16);
    }
    std::vector<SceneActor> ScanScene() {
        std::vector<SceneActor> result;const int scene=PerformanceScene();
        if(!scene)return result;
        for(void* actor:Objects(Class("Campus.Common","CampusActorController"))) {
            void* go=GameObject(actor);if(!Bool(go,"get_activeInHierarchy"))continue;
            const int actorScene=SceneOf(go);if(scene&&actorScene!=scene)continue;
            SceneActor entry;entry.instance=Int(actor,"GetInstanceID");entry.scene=actorScene;entry.name=Text(go,"get_name");
            if(scene)Pose(actor,entry.position,entry.rotation);
            try {const auto identity=Identify(actor);entry.actorIndex=identity.index;entry.uniqueId=identity.unique;
                entry.identityValid=identity.instance==entry.instance&&identity.scene==entry.scene&&!identity.unique.empty();}
            catch(const std::exception&) {entry.identityValid=false;}
            // A missing descriptor or optional getter must not hide the actor
            // or stop the rest of the roster from being observed.
            try {
                Pin descriptor(*this,Call(On(actor,"get_descriptor"),actor));
                if(descriptor.Get()) {
                    entry.bodyAsset=Text(descriptor.Get(),"get_body");
                    entry.faceAsset=Text(descriptor.Get(),"get_face");
                    entry.hairAsset=Text(descriptor.Get(),"get_hair");
                }
            } catch(const std::exception&) { entry.bodyAsset.clear();entry.faceAsset.clear();entry.hairAsset.clear(); }
            for(const auto& lease:owned_)if(lease.ready&&lease.identity.instance==entry.instance&&lease.identity.scene==entry.scene&&LeaseMatches(lease,Identify(actor))){entry.owned=true;entry.preset=lease.preset;}
            entry.modelRestorable=modelEdit_.original&&modelEdit_.identity.instance==entry.instance&&modelEdit_.identity.scene==entry.scene&&modelEdit_.identity.unique==entry.uniqueId;
            result.push_back(std::move(entry));
        }
        // Observation never reconciles or releases game cache entries.
        return result;
    }
    std::string Identity(void* record) {
        const auto method=On(record,"GetCharacter");
        void* character=method?Call(method,record):nullptr;
        return character?Text(character,"get_Id"):std::string{};
    }
    const void* Sorted(void* object) {
        const auto m=object?Method(objectClass_(object),"GetAllWithSortByKey",1):nullptr;
        if(!m)throw std::runtime_error("游戏目录排序接口不可用");
        void* type=param_(m,0);void* klass=fromType_(type);uint32_t alignment{};
        if(byRef_(type)||(typeCode_(type)!=8&&!(isEnum_(klass)&&valueSize_(klass,&alignment)==4)))
            throw std::runtime_error("游戏目录排序参数不匹配");
        return m;
    }
    void* Factory(const std::string& name) {
        if(!ValidFactory(name))throw std::runtime_error("未知特殊人物接口");
        void* utility=Class("Campus.Common","CampusActorUtility");
        if(const auto m=Method(utility,name.c_str(),0,true))return Call(m);
        const auto m=Method(utility,name.c_str(),1,true);
        if(!m||byRef_(param_(m,0)))throw std::runtime_error("特殊人物接口不可用");
        Pin props(*this,arrayNew_(Class("System","String"),0));
        if(!props.Get()||!assignable_(fromType_(param_(m,0)),objectClass_(props.Get())))throw std::runtime_error("特殊人物配件参数类型不匹配");
        void* args[]{props.Get()};return Call(m,nullptr,args);
    }
    std::vector<Character> ScanCharacters() {
        std::vector<Character> out;void* master=Class("Campus.Common.Master","MasterManager");
        Trace("Catalog phase: CharacterMaster getter BEGIN");
        Pin chars(*this,Call(Method(master,"get_CharacterMaster",0,true)));int ascending=0;void* sort[]{&ascending};
        Trace("Catalog phase: CharacterMaster getter END; sorted list BEGIN");
        Pin characters(*this,Call(Sorted(chars.Get()),chars.Get(),sort));
        Trace("Catalog phase: CharacterMaster sorted list END; enumerate BEGIN");
        for(void* c:Items(characters.Get())) {
            const auto id=Text(c,"get_Id");if(!TextValid(id,128))continue;
            auto name=Text(c,"get_Name");if(name.empty())name=id;
            out.push_back({id,name,false});
        }
        Trace("Catalog phase: CharacterMaster enumerate END");
        return out;
    }
    Catalog ScanCatalog() {
        Catalog out;void* master=Class("Campus.Common.Master","MasterManager");
        auto load=[&](const char* getter){return Call(Method(master,getter,0,true));};
        out.characters=ScanCharacters();int ascending=0;void* sort[]{&ascending};
        for(bool head:{false,true}) {
            Trace(head?"Catalog phase: CostumeHeadMaster BEGIN":"Catalog phase: CostumeMaster BEGIN");
            Pin manager(*this,load(head?"get_CostumeHeadMaster":"get_CostumeMaster"));
            if(!manager.Get())throw std::runtime_error("游戏服装目录尚未就绪");
            Pin rows(*this,Call(Sorted(manager.Get()),manager.Get(),sort));
            for(void* row:Items(rows.Get())) {
                Part item;item.id=Text(row,"get_Id");item.name=Text(row,"get_Name");item.character=Identity(row);item.head=head;
                item.resource=Text(row,head?"get_HairAssetId":"get_AssetId");
                if(head)item.faceResource=Text(row,"get_FaceAssetId");
                try {
                    const auto thumb=On(row,"GetThumbAssetName");
                    if(thumb&&typeCode_(result_(thumb))==0x0e)item.thumbnail=String(Call(thumb,row));
                    if(!item.thumbnail.empty()&&!TextValid(item.thumbnail,256))item.thumbnail.clear();
                }catch(const std::exception&){} // Optional metadata must not reject the catalog.
                if(item.name.empty())item.name=item.id;if(item.id.empty()||item.character.empty())continue;
                (head?out.heads:out.bodies).push_back(item);
                if(!head) {
                    const auto defaultHead=Text(row,"get_DefaultCostumeHeadId");
                    auto h=defaultHead.empty()?Text(row,"get_CostumeHeadId"):defaultHead;
                    // The native relation may resolve aliases/default rules
                    // that are not direct foreign-key hits in CostumeHead.
                    for(const char* getter:{"GetDefaultCostumeHead","GetCostumeHead"})if(const auto m=On(row,getter)) {
                        try{Pin resolved(*this,Call(m,row));const auto id=Text(resolved.Get(),"get_Id");if(!id.empty()){h=id;break;}}catch(const std::exception&){}
                    }
                    if(!h.empty())out.defaults.push_back({"builtin:"+item.id,item.name,item.character,item.id,h,{},{},{},{},true});
                } else {
                    for(const char* getter:{"get_FaceAssetId","get_HairAssetId"})if(auto model=ParseModelResource(Text(row,getter))) {
                        model->source="头部目录资源字段";AddModelResource(out,std::move(*model));
                    }
                }
            }
            Trace(head?"Catalog phase: CostumeHeadMaster END":"Catalog phase: CostumeMaster END");
        }
        Trace("Catalog phase: optional special descriptors BEGIN");
        struct Special {const char* factory;const char* label;};
        for(const auto& entry:std::vector<Special>{{"CreateTeacherDescriptor","老师"},{"CreateTeacherGlassHairDescriptor","老师（眼镜／发型）"},
            {"CreateVocalTrainerDescriptor","歌唱训练员"},{"CreateDanceTrainerDescriptor","舞蹈训练员"},{"CreateVisualTrainerDescriptor","视觉训练员"}}) {
            try {
                Pin descriptor(*this,Factory(entry.factory));if(!descriptor.Get())continue;
                auto id=Text(descriptor.Get(),"GetCharacterId");if(id.empty())id=entry.factory;
                ModelAssets model{id,Text(descriptor.Get(),"get_body"),Text(descriptor.Get(),"get_face"),Text(descriptor.Get(),"get_hair"),entry.factory};
                if(model.body.empty()||model.face.empty()||model.hair.empty()) {
                    const auto getAssets=On(descriptor.Get(),"get_assets");
                    Pin assets(*this,getAssets?Call(getAssets,descriptor.Get()):nullptr);
                    if(assets.Get()&&ClassName(objectClass_(assets.Get()))=="System.String[]") {
                        const auto n=arrayLength_(assets.Get());
                        if(n<=128) {
                            auto items=reinterpret_cast<void**>(static_cast<char*>(assets.Get())+4*sizeof(void*));
                            for(uintptr_t i=0;i<n;++i) {
                                const auto asset=String(items[i]);
                                if(asset.find("_body")!=std::string::npos&&model.body.empty())model.body=asset;
                                if(asset.find("_face")!=std::string::npos&&model.face.empty())model.face=asset;
                                if(asset.find("_hair")!=std::string::npos&&model.hair.empty())model.hair=asset;
                            }
                        }
                    }
                }
                if(!model.body.empty()&&!model.face.empty()&&!model.hair.empty())out.models.push_back(model);
                auto found=std::find_if(out.characters.begin(),out.characters.end(),[&](const auto& c){return c.id==id;});
                if(found==out.characters.end())out.characters.push_back({id,entry.label,true});else found->special=true;
                out.defaults.push_back({std::string("builtin:")+entry.factory,entry.label,id,{},{},entry.factory,{},{},{},true});
            } catch(const std::exception&) {/* Missing optional factory never rejects normal outfits. */}
        }
        out.defaults.erase(std::remove_if(out.defaults.begin(),out.defaults.end(),[&](const auto& p){return p.factory.empty()&&!FindPart(out.heads,p.head);}),out.defaults.end());
        Trace("Catalog phase: optional special descriptors END");
        out.notice="已读取游戏人物、服装与头部目录；资源加载状态尚待验证";return out;
    }
    void* Descriptor(const Preset& p) {
        if(!p.factory.empty())return Factory(p.factory);
        void* utility=Class("Campus.Common","CampusActorUtility");
        if(!p.bodyAsset.empty()) {
            const auto method=Method(utility,"CreateActorDescriptor",3,true,{0x0e,0x0e,0x0e});
            Pin body(*this,stringNew_(p.bodyAsset.c_str())),face(*this,stringNew_(p.faceAsset.c_str())),hair(*this,stringNew_(p.hairAsset.c_str()));
            void* args[]{body.Get(),face.Get(),hair.Get()};return Call(method,nullptr,args);
        }
        const auto method=Method(utility,"CreateActorDescriptor",2,true,{0x0e,0x0e});
        Pin body(*this,stringNew_(p.body.c_str())),head(*this,stringNew_(p.head.c_str()));
        void* args[]{body.Get(),head.Get()};return Call(method,nullptr,args);
    }
    // Prepare an independent shallow descriptor copy with a separate asset
    // list. This does not update a game actor. Other reference-valued fields
    // remain shared and must be audited before calling any mutation API.
    void* PrepareModelDescriptor(void* original,const std::array<std::string,3>& requested) {
        if(!original)throw std::runtime_error("人物描述为空");
        const std::array<std::string,3> before{Text(original,"get_body"),Text(original,"get_face"),Text(original,"get_hair")};
        const auto getAssets=On(original,"get_assets");
        Pin assets(*this,Call(getAssets,original));
        if(!assets.Get())throw std::runtime_error("人物资源列表为空");
        const int n=Int(assets.Get(),"get_Count");if(n<0||n>128)throw std::runtime_error("人物资源列表长度异常");
        const auto get=Method(objectClass_(assets.Get()),"get_Item",1,false,{8});
        if(!get||typeCode_(result_(get))!=0x0e)throw std::runtime_error("人物资源列表元素类型不匹配");
        std::vector<std::string> input,output;
        for(int i=0;i<n;++i){void* args[]{&i};const auto value=String(Call(get,assets.Get(),args));if(value.empty())throw std::runtime_error("人物资源列表包含空值");input.push_back(value);}
        if(!ReplaceActorModelAssets(input,before,requested,output))throw std::runtime_error("无法唯一识别身体、脸和头发资源位置");
        const auto clone=On(original,"MemberwiseClone");
        Pin copy(*this,Call(clone,original));
        if(!copy.Get()||copy.Get()==original||objectClass_(copy.Get())!=objectClass_(original))throw std::runtime_error("人物描述复制失败");
        Pin list(*this,objectNew_(objectClass_(assets.Get())));
        if(!list.Get())throw std::runtime_error("资源列表分配失败");
        const auto ctor=On(list.Get(),".ctor");const auto add=Method(objectClass_(list.Get()),"Add",1,false,{0x0e});
        if(!ctor||!add)throw std::runtime_error("资源列表构造接口不匹配");
        Call(ctor,list.Get());
        for(const auto& value:output){Pin item(*this,stringNew_(value.c_str()));if(!item.Get())throw std::runtime_error("资源名称分配失败");void* args[]{item.Get()};Call(add,list.Get(),args);}
        const auto set=On(copy.Get(),"set_assets",1);
        if(!set||!assignable_(fromType_(param_(set,0)),objectClass_(list.Get())))throw std::runtime_error("资源列表设置接口不匹配");
        void* args[]{list.Get()};Call(set,copy.Get(),args);
        if(Text(copy.Get(),"get_body")!=requested[0]||Text(copy.Get(),"get_face")!=requested[1]||Text(copy.Get(),"get_hair")!=requested[2]||
           Text(original,"get_body")!=before[0]||Text(original,"get_face")!=before[1]||Text(original,"get_hair")!=before[2])
            throw std::runtime_error("描述副本资源校验失败");
        return copy.Get();
    }
    ModelAssets ResolveModelAssets(const Preset& preset) {
        Pin descriptor(*this,Descriptor(preset));
        if(!descriptor.Get())throw std::runtime_error("模型描述为空");
        ModelAssets model{preset.character,Text(descriptor.Get(),"get_body"),Text(descriptor.Get(),"get_face"),Text(descriptor.Get(),"get_hair"),preset.factory.empty()?preset.id:preset.factory};
        if(model.body.empty()||model.face.empty()||model.hair.empty()) {
            const auto getter=On(descriptor.Get(),"get_assets");
            Pin assets(*this,getter?Call(getter,descriptor.Get()):nullptr);
            if(assets.Get()&&ClassName(objectClass_(assets.Get()))=="System.String[]") {
                const auto n=arrayLength_(assets.Get());if(n>128)throw std::runtime_error("模型描述资源数异常");
                auto items=reinterpret_cast<void**>(static_cast<char*>(assets.Get())+4*sizeof(void*));
                for(uintptr_t i=0;i<n;++i) {
                    const auto a=String(items[i]);
                    if(model.body.empty()&&a.find("_body")!=std::string::npos)model.body=a;
                    if(model.face.empty()&&a.find("_face")!=std::string::npos)model.face=a;
                    if(model.hair.empty()&&a.find("_hair")!=std::string::npos)model.hair=a;
                }
            }
        }
        if(model.body.empty()||model.face.empty()||model.hair.empty())throw std::runtime_error("原生描述未提供完整身体、脸、头发资源");
        return model;
    }
    #include "NativeSpawn.inl"
    #include "NativeModelEdit.inl"
    #include "NativeLiveBindingDiagnostics.inl"
    #include "NativePreLiveDiagnostics.inl"
    #include "NativeResources.inl"
    #include "NativeCamera.inl"
    #include "NativeThumbnail.inl"
    #include "NativeResourceIndex.inl"
    bool DiagnoseActorManagerRelations() {
        const int scene=PerformanceScene();if(!scene)return false;
        Trace("Diagnostic manager relationships BEGIN (read only; no update or bind)");
        void* manager=nullptr;
        try{manager=Manager();}catch(const std::exception& e){Trace((std::string("Diagnostic manager unavailable: ")+e.what()).c_str());}
        if(!manager){Trace("Diagnostic manager relationships: no unique active manager");return false;}
        const int managerId=Int(manager,"GetInstanceID");size_t count=0;
        for(void* actor:Objects(Class("Campus.Common","CampusActorController"))) {
            try {
                if(SceneOf(GameObject(actor))!=scene)continue;
                if(++count>64){Trace("Diagnostic manager relationships: truncated at 64");break;}
                const auto observed=Identify(actor);
                void* mapped=AtIndex(manager,observed.index);
                if(!mapped){Trace(("Diagnostic manager mapping: manager="+std::to_string(managerId)+" observed="+std::to_string(observed.instance)+" index="+std::to_string(observed.index)+" mapped=none").c_str());continue;}
                const auto registered=Identify(mapped);
                Trace(("Diagnostic manager mapping: manager="+std::to_string(managerId)+" observed="+std::to_string(observed.instance)+" index="+std::to_string(observed.index)+" scene="+std::to_string(observed.scene)+" unique="+observed.unique+" mapped="+std::to_string(registered.instance)+" mapped_scene="+std::to_string(registered.scene)+" mapped_unique="+registered.unique+" same="+std::to_string(registered.instance==observed.instance)+" active="+std::to_string(observed.active)+" build="+std::to_string(observed.build)+" rendered="+std::to_string(observed.rendered)).c_str());
            }catch(const std::exception& e){Trace((std::string("Diagnostic manager mapping unavailable: ")+e.what()).c_str());}
        }
        Trace("Diagnostic manager relationships END");return true;
    }
    bool DiagnoseActors() {
        const int scene=PerformanceScene();if(!scene)return false;
        Trace(("Diagnostic actors BEGIN: owner_scene="+std::to_string(scene)+" (read only; no humanoid bone lookup)").c_str());
        size_t count=0;std::vector<void*> shapes;
        const auto actors=Objects(Class("Campus.Common","CampusActorController"));
        std::vector<std::pair<int,std::array<std::string,3>>> choices;
        for(void* candidate:actors)try {
            if(SceneOf(GameObject(candidate))!=scene)continue;
            Pin descriptor(*this,Call(On(candidate,"get_descriptor"),candidate));
            std::array<std::string,3> parts{Text(descriptor.Get(),"get_body"),Text(descriptor.Get(),"get_face"),Text(descriptor.Get(),"get_hair")};
            if(!parts[0].empty()&&!parts[1].empty()&&!parts[2].empty())choices.push_back({Int(candidate,"GetInstanceID"),std::move(parts)});
        }catch(const std::exception&){}
        for(void* actor:actors) {
            int instance=0;
            try {
                void* go=GameObject(actor);if(SceneOf(go)!=scene)continue;
                instance=Int(actor,"GetInstanceID");
                if(++count>64){Trace("Diagnostic actors: truncated at 64");break;}
                Trace(("Diagnostic actor: instance="+std::to_string(instance)+" scene="+std::to_string(scene)+" index="+std::to_string(Int(actor,"get_index"))+" name="+Text(go,"get_name")+" active="+std::to_string(Bool(go,"get_activeInHierarchy"))+" build="+std::to_string(Bool(actor,"get_IsBuildSuccess"))+" renderer_active="+std::to_string(Bool(actor,"IsRendererActive"))).c_str());
                try {
                    Pin descriptor(*this,Call(On(actor,"get_descriptor"),actor));
                    Trace(("Diagnostic actor resources: instance="+std::to_string(instance)+" unique="+Text(descriptor.Get(),"get_uniqueId")+" body="+Text(descriptor.Get(),"get_body")+" face="+Text(descriptor.Get(),"get_face")+" hair="+Text(descriptor.Get(),"get_hair")).c_str());
                    try {
                        const std::array<std::string,3> same{Text(descriptor.Get(),"get_body"),Text(descriptor.Get(),"get_face"),Text(descriptor.Get(),"get_hair")};
                        Pin copy(*this,PrepareModelDescriptor(descriptor.Get(),same));
                        Trace(("Diagnostic descriptor copy: instance="+std::to_string(instance)+(copy.Get()?" verified":" unavailable")).c_str());
                        const auto other=std::find_if(choices.begin(),choices.end(),[&](const auto& x){return x.first!=instance&&x.second!=same;});
                        if(other!=choices.end()) {
                            Pin swapped(*this,PrepareModelDescriptor(descriptor.Get(),other->second));
                            Trace(("Diagnostic descriptor cross-copy: instance="+std::to_string(instance)+" source="+std::to_string(other->first)+(swapped.Get()?" verified":" unavailable")).c_str());
                        }
                    }catch(const std::exception& e){Trace(("Diagnostic descriptor copy unavailable: instance="+std::to_string(instance)+" reason="+e.what()).c_str());}
                    try {
                        Pin assets(*this,Call(On(descriptor.Get(),"get_assets"),descriptor.Get()));
                        if(assets.Get()) {
                            const auto assetCount=Int(assets.Get(),"get_Count");
                            if(assetCount<0||assetCount>128)throw std::runtime_error("资源列表长度异常");
                            const auto item=Method(objectClass_(assets.Get()),"get_Item",1,false,{8});
                            if(!item||typeCode_(result_(item))!=0x0e)throw std::runtime_error("资源列表的元素类型不是字符串");
                            for(int i=0;i<assetCount;++i){void* args[]{&i};Trace(("Diagnostic descriptor asset: instance="+std::to_string(instance)+" slot="+std::to_string(i)+" value="+String(Call(item,assets.Get(),args))).c_str());}
                        } else Trace("Diagnostic descriptor assets: unavailable");
                    } catch(const std::exception& e){Trace((std::string("Diagnostic descriptor assets unavailable: ")+e.what()).c_str());}
                    // Capture descriptor metadata without executing additional
                    // getters: restoring only three model names may discard
                    // other character properties. Inspect each class once.
                    for(void* klass=descriptor.Get()?objectClass_(descriptor.Get()):nullptr;klass;klass=parent_(klass)) {
                        if(std::find(shapes.begin(),shapes.end(),klass)!=shapes.end())break;
                        if(shapes.size()>=128)break;
                        shapes.push_back(klass);TraceTypeShape(klass);
                        void* it=nullptr;size_t methodCount=0;
                        while(const void* method=methods_(klass,&it)) {
                            if(++methodCount>4096)break;
                            const char* name=methodName_(method);
                            if(name&&(!std::strncmp(name,"get_",4)||!std::strncmp(name,"set_",4)))TraceSignature("actor descriptor",method);
                        }
                    }
                } catch(const std::exception& e){Trace(("Diagnostic descriptor unavailable: instance="+std::to_string(instance)+" reason="+e.what()).c_str());}
                try {
                    Pin parts(*this,Call(On(actor,"GetModelPartsList"),actor));
                    Trace(("Diagnostic parts collection: instance="+std::to_string(instance)+" type="+(parts.Get()?TypeName(const_cast<void*>(classType_(objectClass_(parts.Get())))):"<null>")).c_str());
                    int partCount=0,boneEntries=0;
                    for(void* part:Items(parts.Get())) {
                        ++partCount;const int bones=Int(part,"get_boneCount");boneEntries+=bones;
                        Trace(("Diagnostic native part: instance="+std::to_string(instance)+" asset="+Text(part,"get_assetName")+" parts_id="+Text(part,"get_partsId")+" bone_entries="+std::to_string(bones)+" renderer_active="+std::to_string(Bool(part,"get_rendererActive"))).c_str());
                        void* klass=objectClass_(part);
                        // Describe all inherited part fields once per class;
                        // their types identify native VL skinning structures.
                        for(size_t depth=0;klass&&depth<8;++depth,klass=parent_(klass)) {
                            if(std::find(shapes.begin(),shapes.end(),klass)!=shapes.end())continue;
                            shapes.push_back(klass);TraceTypeShape(klass);
                        }
                    }
                    Trace(("Diagnostic native parts summary: instance="+std::to_string(instance)+" parts="+std::to_string(partCount)+" bone_entries="+std::to_string(boneEntries)+" (entries across parts; not unique humanoid bones)").c_str());
                } catch(const std::exception& e){Trace(("Diagnostic native parts unavailable: instance="+std::to_string(instance)+" reason="+e.what()).c_str());}
                try {
                    void* rendererClass=Class("UnityEngine","SkinnedMeshRenderer");
                    const auto components=Method(objectClass_(actor),"GetComponentsInChildren",2,false,{0x12,2});
                    if(!rendererClass||!components)throw std::runtime_error("Unity 蒙皮查询接口不可用");
                    Pin type(*this,typeObject_(classType_(rendererClass)));bool inactive=true;void* args[]{type.Get(),&inactive};
                    Pin array(*this,Call(components,actor,args));
                    const auto n=array.Get()?arrayLength_(array.Get()):0;if(n>1024)throw std::runtime_error("Unity 蒙皮渲染器数量异常");
                    auto items=array.Get()?reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*)):nullptr;
                    int meshes=0,vertices=0,materials=0,bones=0;
                    for(uintptr_t i=0;i<n;++i)if(items[i]) {
                        void* renderer=items[i];Pin mesh(*this,Call(On(renderer,"get_sharedMesh"),renderer));
                        if(!mesh.Get())continue;
                        Pin mats(*this,Call(On(renderer,"get_sharedMaterials"),renderer));
                        Pin boneArray(*this,Call(On(renderer,"get_bones"),renderer));
                        const auto mc=mats.Get()?arrayLength_(mats.Get()):0,bc=boneArray.Get()?arrayLength_(boneArray.Get()):0;
                        if(mc>256||bc>16384)throw std::runtime_error("Unity 材质或骨骼数量异常");
                        const int vc=Int(mesh.Get(),"get_vertexCount");
                        ++meshes;vertices+=vc;materials+=static_cast<int>(mc);bones+=static_cast<int>(bc);
                        Trace(("Diagnostic Unity mesh: instance="+std::to_string(instance)+" name="+Text(mesh.Get(),"get_name")+" vertices="+std::to_string(vc)+" materials="+std::to_string(mc)+" bone_entries="+std::to_string(bc)).c_str());
                    }
                    Trace(("Diagnostic Unity meshes summary: instance="+std::to_string(instance)+" meshes="+std::to_string(meshes)+" vertices="+std::to_string(vertices)+" materials="+std::to_string(materials)+" bone_entries="+std::to_string(bones)+" (does not cover all native VL skinning; zero alone is not a load failure)").c_str());
                } catch(const std::exception& e){Trace(("Diagnostic Unity meshes unavailable: instance="+std::to_string(instance)+" reason="+e.what()).c_str());}
            } catch(const std::exception& e){Trace(("Diagnostic actor unavailable: instance="+std::to_string(instance)+" reason="+e.what()).c_str());}
        }
        Trace(("Diagnostic actors END: inspected="+std::to_string((std::min)(count,size_t{64}))).c_str());return true;
    }
    void SuspendScene() {
        StopResourceScan();
        CancelModelEdit();
        creationEnabled_=false;CancelPending();
        for(auto it=owned_.begin();it!=owned_.end();) {
            if(!it->ready){++it;continue;}
            bool released=false;try{released=ReleaseLease(*it);}catch(...){}
            if(released)it=owned_.erase(it);else{Fault("离开场景时未确认独立实例释放，保留记录");++it;}
        }
        admittedScene_=0;
        notice="当前页面休眠；进入拍摄或 Live 后可按需读取目录";
    }
    void Shutdown() {
        // The host may shut down off the Unity thread. Do not invoke Unity or
        // destroy actors here; the process/owning scene handles destruction.
        creationEnabled_=false;admittedScene_=0;StopResourceScan(false);DropPending(false);DropModelEdit();ClearPreLivePending();owned_.clear();EndFrame();
    }
};
}
