// Included inside NativeApi. All methods are called from the Unity thread.
// The scan is explicit and bounded. Resource handles are never sent to the UI.
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
        void* transform=Call(On(actor,"get_transform"),actor);void* root=nullptr;int roots=0;
        for(void* node:Children(transform))if(Text(node,"get_name")==lease.root){root=node;++roots;}
        if(roots!=1)return false;
        void* found=nullptr;int matches=0;
        for(void* node:Children(root))if(Text(node,"get_name")==lease.prop){found=GameObject(node);++matches;}
        if(matches!=1||found!=object)return false;
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
static bool SupportedProp(const std::string& root,const std::string& prop) {
    if(root.size()<=5||root.size()>120||root.compare(0,5,"Root_")||prop!="Prop_"+root.substr(5))return false;
    std::string lower=root.substr(5);
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    for(const char* part:{"body","face","hair","head","bone","skeleton","hand","foot","costume"})
        if(lower.find(part)!=std::string::npos)return false;
    return true;
}
std::vector<void*> Children(void* transform) {
    std::vector<void*> result;if(!transform)return result;
    int n=Int(transform,"get_childCount");if(n<0||n>512)throw std::runtime_error("节点数量异常");
    const auto get=Method(objectClass_(transform),"GetChild",1,false,{8});
    for(int i=0;i<n;++i){void* args[]{&i};if(void* child=Call(get,transform,args))result.push_back(child);}
    return result;
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
std::vector<SceneResource> ScanResources() {
    std::vector<SceneResource> items;if(!PerformanceScene())return items;
    ClearResourceCandidates();++resourceGeneration_;if(!resourceGeneration_)++resourceGeneration_;
    const int activeScene=ActiveSceneHandle();std::vector<void*> protectedTransforms;
    for(void* actor:Objects(Class("Campus.Common","CampusActorController"))) {
        void* go=GameObject(actor);if(!go||SceneOf(go)!=PerformanceScene()||!Bool(go,"get_activeInHierarchy"))continue;
        const int id=Int(actor,"GetInstanceID");if(!id)continue;
        const auto label=Text(go,"get_name");
        RememberResource(items,{SceneResourceKind::ActorModel,id,id,PerformanceScene(),resourceGeneration_,label,ResourceHidden(id,SceneResourceKind::ActorModel)},actor);
        void* transform=Call(On(go,"get_transform"),go);
        protectedTransforms.push_back(transform);const auto roots=Children(transform);
        for(void* root:roots) {
            const auto rn=Text(root,"get_name");if(rn.size()<=5||rn.compare(0,5,"Root_"))continue;
            if(std::count_if(roots.begin(),roots.end(),[&](void* r){return Text(r,"get_name")==rn;})!=1)continue;
            const auto pn="Prop_"+rn.substr(5);if(!SupportedProp(rn,pn))continue;
            void* found=nullptr;int matches=0;
            for(void* prop:Children(root))if(Text(prop,"get_name")==pn){found=prop;++matches;}
            if(matches!=1)continue;
            void* propGo=GameObject(found);if(!propGo||SceneOf(propGo)!=PerformanceScene())continue;
            const int propId=Int(propGo,"GetInstanceID");if(!propId)continue;
            RememberResource(items,{SceneResourceKind::Prop,propId,propId,PerformanceScene(),resourceGeneration_,label+" / "+pn,ResourceHidden(propId,SceneResourceKind::Prop)},propGo,actor,rn,pn);
        }
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
    if(item.kind==SceneResourceKind::Prop) {
        void* actor=candidate->actorHandle?handleTarget_(candidate->actorHandle):nullptr;
        if(!actor||SceneOf(GameObject(actor))!=item.scene)return false;
        void* transform=Call(On(actor,"get_transform"),actor);void* root=nullptr;int roots=0;
        for(void* node:Children(transform))if(Text(node,"get_name")==candidate->root){root=node;++roots;}
        if(roots!=1)return false;
        void* found=nullptr;int matches=0;
        for(void* node:Children(root))if(Text(node,"get_name")==candidate->prop){found=GameObject(node);++matches;}
        if(matches!=1||found!=object)return false;
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
    return SaveAndWriteResource(object,object,item.scene,item.kind,"activeSelf",false,
        candidate->actorHandle?handleTarget_(candidate->actorHandle):nullptr,candidate->root,candidate->prop);
}
void EndResourceSession() {
    if(!RestoreCamera())Trace("Camera restore incomplete; retaining identity-checked record");
    if(!RestoreResources())Trace("Resource restore incomplete; retaining identity-checked records");
    ClearResourceCandidates();
    ++resourceGeneration_;if(!resourceGeneration_)++resourceGeneration_;
}
