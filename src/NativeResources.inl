// Included inside NativeApi. All methods are called from the Unity thread.
// The scan is explicit and bounded. Resource handles are never sent to the UI.
static bool SupportedPropGeometry(const std::string& root,const std::string& prop,const std::string& geometry) {
    return root=="Root_Hknesuitcase"&&prop=="Prop_Hknesuitcase"&&geometry=="Geo_Hknesuitcase";
}
bool ResourceActive(const ResourceLease& lease) {
    void* object=lease.handle?handleTarget_(lease.handle):nullptr;
    void* owner=lease.ownerHandle?handleTarget_(lease.ownerHandle):nullptr;
    if(!object||!owner||Int(object,"GetInstanceID")!=lease.instance||
       Int(owner,"GetInstanceID")!=lease.owner)return false;
    void* go=lease.property=="activeSelf"?object:GameObject(object);
    if(!go||SceneOf(go)!=lease.scene)return false;
    if(lease.kind==SceneResourceKind::Prop) {
        void* actor=lease.actorHandle?handleTarget_(lease.actorHandle):nullptr;
        if(!actor||SceneOf(GameObject(actor))!=lease.scene)return false;
        void* root = ResolvePropRoot(actor, owner, lease.scene, lease.root, lease.prop);
        if (!root) return false;
        if (object != owner && PropGeometry(root, lease.scene, lease.root, lease.prop) != object)
            return false;
    }
    if(lease.property!="activeSelf") {
        void* ownerGo=GameObject(owner);if(!ownerGo||SceneOf(ownerGo)!=lease.scene)return false;
        void* t=Call(On(object,"get_transform"),object);void* parent=Call(On(owner,"get_transform"),owner);
        const auto method=Method(objectClass_(t),"IsChildOf",1);
        if(!method||typeCode_(result_(method))!=2)return false;
        void* args[]{parent};Pin answer(*this,Call(method,t,args));
        if(!answer.Get()||!*static_cast<bool*>(unbox_(answer.Get())))return false;
    }
    return true;
}
bool ResourceRead(const ResourceLease& lease) {
    if(!ResourceActive(lease))throw std::runtime_error("资源身份已变化");
    return Bool(handleTarget_(lease.handle),("get_"+lease.property).c_str());
}
void ResourceSet(const ResourceLease& lease,bool value) {
    if(!ResourceActive(lease))throw std::runtime_error("资源身份已变化");
    void* object=handleTarget_(lease.handle);
    const char* setter=lease.property=="activeSelf"?"SetActive":
        lease.property=="forceRenderingOff"?"set_forceRenderingOff":"set_enabled";
    const auto method=Method(objectClass_(object),setter,1,false,{2});
    if(!method || typeCode_(result_(method))!=1)throw std::runtime_error("资源显隐接口不匹配");
    void* args[]{&value};Call(method,object,args);
    if(ResourceRead(lease)!=value)throw std::runtime_error("资源显隐回读失败");
}
void ReleaseResourceLease(ResourceLease& lease) {
    if(lease.handle)freeHandle_(lease.handle);
    if(lease.ownerHandle)freeHandle_(lease.ownerHandle);
    if(lease.actorHandle)freeHandle_(lease.actorHandle);
    lease.handle=nullptr;lease.ownerHandle=nullptr;lease.actorHandle=nullptr;
}
bool RestoreResources(int owner=0,SceneResourceKind kind=SceneResourceKind::ActorModel,bool all=true) {
    bool clean=true;
    for(auto it=resourceLeases_.begin();it!=resourceLeases_.end();) {
        if(!all&&(it->owner!=owner||it->kind!=kind)){++it;continue;}
        try {
            bool current=ResourceRead(*it);
            if(current!=it->original) {
                if(current!=it->written){clean=false;++it;continue;}
                ResourceSet(*it,it->original);
            }
            ReleaseResourceLease(*it);it=resourceLeases_.erase(it);
        } catch(...) {clean=false;++it;}
    }
    return clean;
}
bool SaveAndWriteResource(void* object,void* owner,int scene,SceneResourceKind kind,const char* property,bool value,void* actor=nullptr,const std::string& root={},const std::string& prop={}) {
    if(!object||!owner)return false;
    ResourceLease lease;lease.instance=Int(object,"GetInstanceID");lease.owner=Int(owner,"GetInstanceID");
    lease.scene=scene;lease.generation=resourceGeneration_;lease.kind=kind;lease.property=property;
    if(!lease.instance||!lease.owner)return false;
    for(const auto& previous:resourceLeases_)
        if(previous.instance==lease.instance&&previous.property==lease.property)return false;
    lease.handle=newHandle_(object,false);lease.ownerHandle=newHandle_(owner,false);lease.actorHandle=actor?newHandle_(actor,false):nullptr;lease.root=root;lease.prop=prop;
    if(!lease.handle||!lease.ownerHandle||(actor&&!lease.actorHandle)){ReleaseResourceLease(lease);return false;}
    try {lease.original=ResourceRead(lease);lease.written=value;}
    catch(...) {ReleaseResourceLease(lease);throw;}
    resourceLeases_.push_back(std::move(lease));
    if(resourceLeases_.back().original!=value)ResourceSet(resourceLeases_.back(),value);
    return true;
}
bool ResourceHidden(int owner,SceneResourceKind kind) const {
    return std::any_of(resourceLeases_.begin(),resourceLeases_.end(),[&](const auto& r){return r.owner==owner&&r.kind==kind;});
}
static bool ProtectedSceneName(const std::string& lower) {
    for(const char* part:{"collider","col","bound","trigger","actor","chara","camera","cinemachine","canvas","eventsystem","uiroot","manager","singleton","presenter","dispatcher","plugin","wrapper","criware","octo","skeleton","root_costume"})
        if(lower.find(part)!=std::string::npos)return true;
    return lower.rfind("ui",0)==0||lower.rfind("api_",0)==0||lower=="api";
}
static bool SupportedProp(const std::string& root, const std::string& prop) {
    return scene_props::IsSupportedPair(root, prop);
}
std::vector<void*> Children(void* transform) {
    std::vector<void*> result;if(!transform)return result;
    int n=Int(transform,"get_childCount");if(n<0||n>512)throw std::runtime_error("节点数量异常");
    const auto get=Method(objectClass_(transform),"GetChild",1,false,{8});
    for(int i=0;i<n;++i){void* args[]{&i};if(void* child=Call(get,transform,args))result.push_back(child);}
    return result;
}
void* UniqueResourceChild(void* parent, const std::string& name) {
    void* found = nullptr;
    for (void* child : Children(parent)) {
        if (Text(child, "get_name") != name) continue;
        if (found) return nullptr;
        found = child;
    }
    return found;
}
void* ResolvePropRoot(void* actor, void* prop, int scene, const std::string& rootName, const std::string& propName) {
    if (!actor || !prop || !SupportedProp(rootName, propName) ||
        SceneOf(GameObject(actor)) != scene || SceneOf(prop) != scene) return nullptr;
    void* actorTransform = Call(On(actor, "get_transform"), actor);
    void* propTransform = Call(On(prop, "get_transform"), prop);
    if (!actorTransform || !propTransform) return nullptr;
    void* root = Call(On(propTransform, "get_parent"), propTransform);
    if (!root || UniqueResourceChild(root, propName) != propTransform) return nullptr;
    void* parent = Call(On(root, "get_parent"), root);
    if (!parent || UniqueResourceChild(parent, rootName) != root) return nullptr;
    for (int depth = 0; parent && depth < scene_props::MaxPropDepth; ++depth) {
        if (parent == actorTransform) return root;
        parent = Call(On(parent, "get_parent"), parent);
    }
    return nullptr;
}
void* PropGeometry(void* root, int scene, const std::string& rootName, const std::string& propName) {
    constexpr const char* geometryName = "Geo_Hknesuitcase";
    if (!SupportedPropGeometry(rootName, propName, geometryName)) return nullptr;
    void* node = UniqueResourceChild(root, geometryName);
    void* go = node ? GameObject(node) : nullptr;
    return go && SceneOf(go) == scene ? go : nullptr;
}
bool SafeSceneTarget(void* target) {
    if(!target)return false;
    std::vector<void*> todo{target};
    for(size_t i=0;i<todo.size();++i) {
        void* go=GameObject(todo[i]);if(!go)return false;
        std::string name=Text(go,"get_name");std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(ProtectedSceneName(name))return false;
        for(void* child:Children(todo[i])){if(todo.size()>=4096)return false;todo.push_back(child);}
    }
    void* current=target;
    for(int depth=0;depth<64;++depth) {
        current=Call(On(current,"get_parent"),current);if(!current)return true;
        void* go=GameObject(current);if(!go)return false;
        std::string name=Text(go,"get_name");std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(ProtectedSceneName(name))return false;
    }
    return false;
}
int ActiveSceneHandle() {
    if(!activeSceneMethod_)activeSceneMethod_=Method(Class("UnityEngine.SceneManagement","SceneManager"),"GetActiveScene",0,true);
    Pin scene(*this,Call(activeSceneMethod_));if(!scene.Get())return 0;
    uint32_t align{};if(valueSize_(objectClass_(scene.Get()),&align)!=4)throw std::runtime_error("Scene 布局不匹配");
    return *static_cast<int*>(unbox_(scene.Get()));
}
std::vector<void*> ActiveSceneObjects() {
    std::vector<void*> output;
    if(!activeSceneMethod_)activeSceneMethod_=Method(Class("UnityEngine.SceneManagement","SceneManager"),"GetActiveScene",0,true);
    Pin scene(*this,Call(activeSceneMethod_));if(!scene.Get())return output;
    const auto roots=On(scene.Get(),"GetRootGameObjects");
    if(!roots||typeCode_(result_(roots))!=0x1d)throw std::runtime_error("当前场景根节点接口不可用");
    Pin array(*this,Call(roots,scene.Get()));if(!array.Get())return output;
    const auto count=arrayLength_(array.Get());if(count>4096)throw std::runtime_error("场景根节点数量异常");
    for(uintptr_t i=0;i<count;++i) {
        auto values=reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*));
        if(!values[i])continue;
        void* handle=newHandle_(values[i],false);if(!handle)throw std::runtime_error("无法保留场景根节点");
        frameRoots_.push_back(handle);output.push_back(values[i]);
    }
    for(size_t i=0;i<output.size();++i) {
        void* transform=Call(On(output[i],"get_transform"),output[i]);
        for(void* child:Children(transform)) {
            void* go=GameObject(child);if(!go)continue;
            if(output.size()>=4096)throw std::runtime_error("场景节点超过扫描上限");
            void* handle=newHandle_(go,false);if(!handle)throw std::runtime_error("无法保留场景节点");
            frameRoots_.push_back(handle);output.push_back(go);
        }
    }
    return output;
}
void ClearResourceCandidates() {
    for(auto& candidate:resourceCandidates_){if(candidate.handle)freeHandle_(candidate.handle);if(candidate.actorHandle)freeHandle_(candidate.actorHandle);}
    resourceCandidates_.clear();
}
void RememberResource(std::vector<SceneResource>& items,SceneResource item,void* object,void* actor=nullptr,const std::string& root={},const std::string& prop={}) {
    void* handle=newHandle_(object,false);if(!handle)throw std::runtime_error("无法保留资源身份");
    void* actorHandle=actor?newHandle_(actor,false):nullptr;
    if(actor&&!actorHandle){freeHandle_(handle);throw std::runtime_error("无法保留道具所属人物");}
    resourceCandidates_.push_back({item,handle,actorHandle,root,prop});items.push_back(std::move(item));
}
// Opt-in diagnostics for identifying which actor renderers belong to a prop.
// Only observations are logged; all targets remain owned by the game.
void TraceSuitcaseRenderers(void* actor) {
    if(!trace)return;
    try {
        void* renderer=Class("UnityEngine","Renderer");
        auto method=Method(objectClass_(actor),"GetComponentsInChildren",2,false,{0x12,2});
        if(!renderer||!method)return;
        Pin type(*this,typeObject_(classType_(renderer)));bool includeInactive=true;void* args[]{type.Get(),&includeInactive};
        Pin array(*this,Call(method,actor,args));if(!array.Get())return;
        const auto count=arrayLength_(array.Get());if(count>512)return;
        auto values=reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*));
        for(uintptr_t i=0;i<count;++i) {
            void* render=values[i];if(!render)continue;
            void* t=Call(On(render,"get_transform"),render);std::string path;
            for(int depth=0;t&&depth<64;++depth) {
                path=Text(t,"get_name")+(path.empty()?"":"/"+path);
                t=Call(On(t,"get_parent"),t);
            }
            void* go=GameObject(render);
            Trace((std::string("Suitcase diagnostic renderer path=")+path+" active="+(go&&Bool(go,"get_activeInHierarchy")?"1":"0")+" enabled="+(Bool(render,"get_enabled")?"1":"0")).c_str());
        }
    }catch(const std::exception& e){Trace((std::string("Suitcase diagnostic unavailable: ")+e.what()).c_str());}
}
std::vector<SceneResource> ScanResources() {
    std::vector<SceneResource> items;if(!PerformanceScene())return items;
    ClearResourceCandidates();++resourceGeneration_;if(!resourceGeneration_)++resourceGeneration_;
    const int scene = PerformanceScene();
    struct ActorNodes { void* actor; void* transform; std::string label; };
    std::vector<ActorNodes> actors;
    std::vector<void*> protectedTransforms;
    for (void* actor : Objects(Class("Campus.Common", "CampusActorController"))) {
        void* go = GameObject(actor);
        if (!go || SceneOf(go) != scene || !Bool(go, "get_activeInHierarchy")) continue;
        const int id = Int(actor, "GetInstanceID");
        if (!id) continue;
        auto label = Text(go, "get_name");
        RememberResource(items, {SceneResourceKind::ActorModel, id, id, scene, resourceGeneration_,
            label, ResourceHidden(id, SceneResourceKind::ActorModel)}, actor);
        void* transform = Call(On(go, "get_transform"), go);
        if (!transform) continue;
        protectedTransforms.push_back(transform);
        actors.push_back({actor, transform, std::move(label)});
    }
    const auto isOtherActor = [&](void* node) {
        return std::find(protectedTransforms.begin(), protectedTransforms.end(), node) != protectedTransforms.end();
    };
    for (const auto& actor : actors) {
        try {
            for (const auto& match : scene_props::ScanActorProps(actor.transform,
                [&](void* parent) { return Children(parent); },
                [&](void* node) { return Text(node, "get_name"); }, isOtherActor)) {
                const auto& [root, propTransform, rootName, propName] = match;
                void* prop = GameObject(propTransform);
                if (!prop || SceneOf(prop) != scene) continue;
                const int propId = Int(prop, "GetInstanceID");
                if (!propId) continue;
                const auto displayName = scene_props::DisplayName(propName);
                const auto label = actor.label + " / " + displayName +
                    ((propName == "Prop_Microphone" || propName == "Prop_Hknesuitcase") ? " · " + propName : "");
                SceneResource item{SceneResourceKind::Prop, propId, propId, scene, resourceGeneration_,
                    label, ResourceHidden(propId, SceneResourceKind::Prop)};
                item.active = Bool(prop, "get_activeSelf");
                item.locked = propName == "Prop_Hknesuitcase" && !PropGeometry(root, scene, rootName, propName);
                RememberResource(items, std::move(item), prop, actor.actor, rootName, propName);
                if (propName == "Prop_Hknesuitcase") TraceSuitcaseRenderers(actor.actor);
            }
        } catch (const std::exception& e) {
            Trace((std::string("Actor prop scan incomplete: ") + e.what()).c_str());
        }
    }
    int activeScene{};
    try { activeScene = ActiveSceneHandle(); }
    catch (const std::exception& e) {
        Trace((std::string("Active scene unavailable: ") + e.what()).c_str());
        return items;
    }
    for(void* camera:Objects(Class("UnityEngine","Camera"))) {
        void* go=GameObject(camera);if(!go||SceneOf(go)!=activeScene)continue;
        if(void* transform=Call(On(camera,"get_transform"),camera))protectedTransforms.push_back(transform);
    }
    std::vector<void*> stageObjects;
    try{stageObjects=ActiveSceneObjects();}
    catch(const std::exception& e){Trace((std::string("Scene node scan unavailable: ")+e.what()).c_str());return items;}
    struct Node {void* go{};void* transform{};int id{},parent{},depth{};std::string name;bool locked{};};
    std::vector<Node> nodes;std::unordered_map<int,size_t> byId;
    for(void* go:stageObjects) {
        if(!activeScene||SceneOf(go)!=activeScene)continue;
        Node node;node.go=go;node.id=Int(go,"GetInstanceID");if(!node.id)continue;
        node.name=Text(go,"get_name");node.transform=Call(On(go,"get_transform"),go);
        void* parent=Call(On(node.transform,"get_parent"),node.transform);
        if(parent){void* parentGo=GameObject(parent);if(parentGo)node.parent=Int(parentGo,"GetInstanceID");}
        std::string lower=node.name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        node.locked=ProtectedSceneName(lower);
        auto found=byId.find(node.parent);if(found!=byId.end()){node.depth=nodes[found->second].depth+1;node.locked=node.locked||nodes[found->second].locked;}
        byId[node.id]=nodes.size();nodes.push_back(std::move(node));
    }
    for(size_t i=nodes.size();i>0;--i)if(nodes[i-1].locked) {
        auto found=byId.find(nodes[i-1].parent);if(found!=byId.end())nodes[found->second].locked=true;
    }
    for(auto& node:nodes) {
        const auto& name=node.name;SceneResourceKind kind=SceneResourceKind::SceneNode;
        if(name=="Lighting&PostEffects")kind=SceneResourceKind::Lighting;
        else if(name.rfind("env_",0)==0||name.rfind("mdl_env_",0)==0||name=="Background")kind=SceneResourceKind::Background;
        void* transform=node.transform;bool protectedActor=node.locked;
        for(void* t:protectedTransforms) {
            const auto child=Method(objectClass_(t),"IsChildOf",1);
            if(!child||typeCode_(result_(child))!=2){protectedActor=true;break;}
            void* args[]{transform};Pin answer(*this,Call(child,t,args));
            if(!answer.Get()||*static_cast<bool*>(unbox_(answer.Get()))){protectedActor=true;break;}
            void* reverseArgs[]{t};Pin reverse(*this,Call(child,transform,reverseArgs));
            if(!reverse.Get()||*static_cast<bool*>(unbox_(reverse.Get()))){protectedActor=true;break;}
        }
        const bool active=Bool(node.go,"get_activeSelf"),owned=ResourceHidden(node.id,kind);
        SceneResource item{kind,node.id,node.id,activeScene,resourceGeneration_,name,owned};
        item.parent=node.parent;item.depth=node.depth;item.active=active;item.locked=protectedActor||(!active&&!owned);
        RememberResource(items,std::move(item),node.go);
    }
    return items;
}
bool SetResourceHidden(const SceneResource& item,bool hidden) {
    if(!PerformanceScene()||item.generation!=resourceGeneration_)return false;
    const auto candidate=std::find_if(resourceCandidates_.begin(),resourceCandidates_.end(),[&](const auto& c){return c.item.generation==item.generation&&c.item.kind==item.kind&&c.item.scene==item.scene&&c.item.instance==item.instance;});
    if(candidate==resourceCandidates_.end())return false;
    if(candidate->item.locked)return false;
    if ((item.kind == SceneResourceKind::ActorModel || item.kind == SceneResourceKind::Prop) &&
        item.scene != PerformanceScene()) return false;
    void* object=handleTarget_(candidate->handle);if(!object||Int(object,"GetInstanceID")!=item.instance)return false;
    if(item.kind==SceneResourceKind::ActorModel ? SceneOf(GameObject(object))!=item.scene : SceneOf(object)!=item.scene)return false;
    if((item.kind==SceneResourceKind::Background||item.kind==SceneResourceKind::Lighting||item.kind==SceneResourceKind::SceneNode)&&ActiveSceneHandle()!=item.scene)return false;
    if(item.kind==SceneResourceKind::Background||item.kind==SceneResourceKind::Lighting||item.kind==SceneResourceKind::SceneNode) {
        void* target=Call(On(object,"get_transform"),object);
        if(!SafeSceneTarget(target))return false;
        for(void* klass:{Class("Campus.Common","CampusActorController"),Class("UnityEngine","Camera")})
            for(void* protectedObject:Objects(klass)) {
                void* go=GameObject(protectedObject);if(!go)continue;
                const int scene=SceneOf(go);if(scene!=item.scene&&scene!=PerformanceScene())continue;
                void* t=Call(On(protectedObject,"get_transform"),protectedObject);
                const auto method=Method(objectClass_(t),"IsChildOf",1);
                if(!method||typeCode_(result_(method))!=2)return false;
                void* a[]{target};Pin left(*this,Call(method,t,a));
                void* b[]{t};Pin right(*this,Call(method,target,b));
                if(!left.Get()||!right.Get()||*static_cast<bool*>(unbox_(left.Get()))||*static_cast<bool*>(unbox_(right.Get())))return false;
            }
    }
    void* propGeometry=nullptr;
    if(item.kind==SceneResourceKind::Prop) {
        void* actor=candidate->actorHandle?handleTarget_(candidate->actorHandle):nullptr;
        void* root = ResolvePropRoot(actor, object, item.scene, candidate->root, candidate->prop);
        if (!root) return false;
        if (candidate->prop == "Prop_Hknesuitcase") {
            propGeometry = PropGeometry(root, item.scene, candidate->root, candidate->prop);
            if (!propGeometry) return false;
        }
    }
    if(!hidden)return RestoreResources(item.owner,item.kind,false);
    if(ResourceHidden(item.owner,item.kind))return true;
    if(item.kind==SceneResourceKind::ActorModel) {
        void* actor=object;
        void* renderer=Class("UnityEngine","Renderer");if(!renderer)return false;
        auto method=Method(objectClass_(actor),"GetComponentsInChildren",2,false,{0x12,2});if(!method)return false;
        Pin type(*this,typeObject_(classType_(renderer)));bool includeInactive=true;void* args[]{type.Get(),&includeInactive};
        Pin array(*this,Call(method,actor,args));if(!array.Get())return false;
        const auto count=arrayLength_(array.Get());if(!count||count>512)return false;
        auto values=reinterpret_cast<void**>(static_cast<char*>(array.Get())+4*sizeof(void*));
        try {
            for(uintptr_t i=0;i<count;++i) {
                void* render=values[i];if(!render)throw std::runtime_error("渲染器无效");
                const bool force=On(render,"get_forceRenderingOff")&&Method(objectClass_(render),"set_forceRenderingOff",1,false,{2});
                if(!SaveAndWriteResource(render,actor,item.scene,item.kind,force?"forceRenderingOff":"enabled",force))throw std::runtime_error("渲染器保存失败");
            }
            return true;
        } catch(...) {RestoreResources(item.owner,item.kind,false);throw;}
    }
    void* actor=candidate->actorHandle?handleTarget_(candidate->actorHandle):nullptr;
    if(propGeometry) {
        try {
            if(!SaveAndWriteResource(object,object,item.scene,item.kind,"activeSelf",false,actor,candidate->root,candidate->prop)||
               !SaveAndWriteResource(propGeometry,object,item.scene,item.kind,"activeSelf",false,actor,candidate->root,candidate->prop))
                throw std::runtime_error("行李箱显隐保存失败");
            return true;
        } catch(...) {RestoreResources(item.owner,item.kind,false);throw;}
    }
    return SaveAndWriteResource(object,object,item.scene,item.kind,"activeSelf",false,actor,candidate->root,candidate->prop);
}
void EndResourceSession() {
    if(!RestoreCamera())Trace("Camera restore incomplete; retaining identity-checked record");
    if(!RestoreResources())Trace("Resource restore incomplete; retaining identity-checked records");
    ClearResourceCandidates();
    ++resourceGeneration_;if(!resourceGeneration_)++resourceGeneration_;
}
