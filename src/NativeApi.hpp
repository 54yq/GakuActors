#pragma once
#include "Catalog.hpp"
#include "ModelReadiness.hpp"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cctype>

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
    const char* (*className_)(void*){};
    const char* (*classNamespace_)(void*){};
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
    std::vector<void*> frameRoots_;
    int admittedScene_{};
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
    // Recovery build: no scene mutation path is compiled. Do not re-enable
    // until allocation and ownership can be proven independently of game cache.
    static constexpr bool MutationsEnabled(){return false;}
    static constexpr const char* MutationBlockReason(){
        return "保护模式：人物新增、移除和移动已停用；等待独立实例加载修复";
    }
    bool RejectMutation(const char* operation) {
        notice=MutationBlockReason();
        Trace((std::string("Scene mutation BLOCKED (recovery build): ")+operation).c_str());
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
        return ok;
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
        if(exception)throw std::runtime_error(std::string("游戏方法返回异常：")+methodName_(method));
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
    void SetAdmittedScene(int scene){admittedScene_=scene;}
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
    Catalog ScanCatalog() {
        Catalog out;void* master=Class("Campus.Common.Master","MasterManager");
        auto load=[&](const char* getter){return Call(Method(master,getter,0,true));};
        Trace("Catalog phase: CharacterMaster getter BEGIN");
        Pin chars(*this,load("get_CharacterMaster"));int ascending=0;void* sort[]{&ascending};
        Trace("Catalog phase: CharacterMaster getter END; sorted list BEGIN");
        Pin characters(*this,Call(Sorted(chars.Get()),chars.Get(),sort));
        Trace("Catalog phase: CharacterMaster sorted list END; enumerate BEGIN");
        for(void* c:Items(characters.Get())) {
            const auto id=Text(c,"get_Id");if(!TextValid(id,128))continue;
            auto name=Text(c,"get_Name");if(name.empty())name=id;
            out.characters.push_back({id,name,false});
        }
        Trace("Catalog phase: CharacterMaster enumerate END");
        for(bool head:{false,true}) {
            Trace(head?"Catalog phase: CostumeHeadMaster BEGIN":"Catalog phase: CostumeMaster BEGIN");
            Pin manager(*this,load(head?"get_CostumeHeadMaster":"get_CostumeMaster"));
            if(!manager.Get())throw std::runtime_error("游戏服装目录尚未就绪");
            Pin rows(*this,Call(Sorted(manager.Get()),manager.Get(),sort));
            for(void* row:Items(rows.Get())) {
                Part item;item.id=Text(row,"get_Id");item.name=Text(row,"get_Name");item.character=Identity(row);item.head=head;
                item.resource=Text(row,head?"get_HairAssetId":"get_AssetId");
                if(item.name.empty())item.name=item.id;if(item.id.empty()||item.character.empty())continue;
                (head?out.heads:out.bodies).push_back(item);
                if(!head) {
                    const auto defaultHead=Text(row,"get_DefaultCostumeHeadId");
                    auto h=defaultHead.empty()?Text(row,"get_CostumeHeadId"):defaultHead;
                    if(!h.empty())out.defaults.push_back({"builtin:"+item.id,item.name,item.character,item.id,h,{},{},{},{},true});
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
    bool StartSpawn(const Preset&,int) {return RejectMutation("spawn");}
    bool RemoveActor(int) {return RejectMutation("remove");}
    bool MoveActor(int,const float*) {return RejectMutation("move");}
    ModelReadiness InspectModel(void* actor) {
        ModelReadiness state;state.buildSuccess=Bool(actor,"get_IsBuildSuccess");state.rendererActive=Bool(actor,"IsRendererActive");
        Pin parts(*this,Call(On(actor,"GetModelPartsList"),actor));
        for(void* part:Items(parts.Get())) {
            ++state.parts;
            std::string name=Text(part,"get_assetName");
            if(name.empty())name=Text(part,"get_name");
            if(On(part,"get_gameObject")){void* go=GameObject(part);name+=" "+Text(go,"get_name");}
            Trace(("Spawn built model part: "+name).c_str());
            for(auto& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            state.bodyPart|=name.find("_body")!=std::string::npos||name.find("geo_body")!=std::string::npos;
            state.facePart|=name.find("_face")!=std::string::npos||name.find("geo_face")!=std::string::npos;
        }
        void* rendererClass=Class("UnityEngine","SkinnedMeshRenderer");
        const auto components=Method(objectClass_(actor),"GetComponentsInChildren",2,false,{0x12,2});
        if(!rendererClass||!components)throw std::runtime_error("模型蒙皮渲染器检查接口不可用");
        Pin type(*this,typeObject_(classType_(rendererClass)));bool inactive=true;void* args[]{type.Get(),&inactive};
        Pin array(*this,Call(components,actor,args));if(!array.Get())return state;
        const auto count=arrayLength_(array.Get());if(count>1024)throw std::runtime_error("人物渲染器数量超出检查范围");
        auto items=reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*));
        for(uintptr_t i=0;i<count;++i)if(items[i]) {
            void* renderer=items[i];
            Pin mesh(*this,Call(On(renderer,"get_sharedMesh"),renderer));if(!mesh.Get())continue;
            const int vertices=Int(mesh.Get(),"get_vertexCount");if(vertices<=0)continue;
            Pin materials(*this,Call(On(renderer,"get_sharedMaterials"),renderer));
            Pin bones(*this,Call(On(renderer,"get_bones"),renderer));
            const auto materialCount=materials.Get()?arrayLength_(materials.Get()):0;
            const auto boneCount=bones.Get()?arrayLength_(bones.Get()):0;
            if(materialCount>256||boneCount>16384)throw std::runtime_error("人物材质或骨骼数量异常");
            ++state.meshes;state.vertices+=vertices;state.materials+=static_cast<int>(materialCount);state.bones+=static_cast<int>(boneCount);
        }
        return state;
    }
    void SuspendScene() {
        admittedScene_=0;
        notice="当前页面休眠；进入拍摄或 Live 后可按需读取目录";
    }
    void Shutdown() {
        // Release only our temporary GC roots. Never destroy scene objects or
        // cancel game tasks during shutdown in the recovery build.
        admittedScene_=0;EndFrame();
    }
};
}
