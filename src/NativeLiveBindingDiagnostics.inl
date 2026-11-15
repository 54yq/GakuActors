// Read-only Live binding diagnostics. Runs only from an explicit catalog scan
// on the Unity thread. No references are kept after this frame.
void* DiagnosticReferenceField(void* object,const char* name,void* expected=nullptr) {
    if(!object||!fields_||!fieldType_||!fieldFlags_||!fieldValue_)return nullptr;
    const auto field=FindField(objectClass_(object),name);if(!field)return nullptr;
    if(fieldFlags_(field)&0x10)return nullptr;
    void* type=fieldType_(field);if(!type||byRef_(type))return nullptr;
    const auto code=typeCode_(type);
    // IL2CPP represents System.String with its own type code, although the
    // field is still a managed reference (e.g. targetCharacterId).
    if(code!=0x0e&&code!=0x12&&code!=0x15&&code!=0x1d&&code!=0x1c)return nullptr;
    Pin value(*this,fieldValue_(field,object));
    if(!value.Get())return nullptr;
    if(expected&&!assignable_(expected,objectClass_(value.Get())))return nullptr;
    return value.Get();
}
void DiagnosticActorCollection(const char* label,void* collection) {
    if(!collection){Trace((std::string("Diagnostic live collection: ")+label+" unavailable").c_str());return;}
    void* klass=objectClass_(collection);const auto type=TypeName(const_cast<void*>(classType_(klass)));
    const int code=typeCode_(const_cast<void*>(classType_(klass)));
    const void* item=nullptr;int count=0;
    if(code==0x1d) {
        const auto length=arrayLength_(collection);if(length>64){Trace((std::string("Diagnostic live collection: ")+label+" invalid array length").c_str());return;}
        count=static_cast<int>(length);item=Method(klass,"GetValue",1,false,{8});
    } else {
        const auto countMethod=On(collection,"get_Count");item=Method(klass,"get_Item",1,false,{8});
        if(!countMethod||typeCode_(result_(countMethod))!=8){Trace((std::string("Diagnostic live collection: ")+label+" type="+type+" count unavailable").c_str());return;}
        count=Int(collection,"get_Count");
    }
    if(count<0||count>64){Trace((std::string("Diagnostic live collection: ")+label+" invalid count").c_str());return;}
    Trace((std::string("Diagnostic live collection: ")+label+" type="+type+" count="+std::to_string(count)).c_str());
    if(!item){Trace((std::string("Diagnostic live collection: ")+label+" item accessor unavailable").c_str());return;}
    void* actorClass=Class("Campus.Common","CampusActorController");
    for(int i=0;i<count;++i)try {
        void* args[]{&i};Pin entry(*this,Call(item,collection,args));if(!entry.Get())continue;
        Trace((std::string("Diagnostic live entry: ")+label+" index="+std::to_string(i)+" type="+TypeName(const_cast<void*>(classType_(objectClass_(entry.Get()))))).c_str());
        for(const char* field:{"Item1","Item2","Item3","Item4"}) {
            Pin value(*this,DiagnosticReferenceField(entry.Get(),field));if(!value.Get())continue;
            const auto valueType=TypeName(const_cast<void*>(classType_(objectClass_(value.Get()))));
            if(actorClass&&assignable_(actorClass,objectClass_(value.Get()))) {
                const auto actor=Identify(value.Get());
                Trace((std::string("Diagnostic live actor ref: ")+label+" index="+std::to_string(i)+" field="+field+" instance="+std::to_string(actor.instance)+" actor_index="+std::to_string(actor.index)+" scene="+std::to_string(actor.scene)+" unique="+actor.unique+" active="+std::to_string(actor.active)+" build="+std::to_string(actor.build)+" rendered="+std::to_string(actor.rendered)).c_str());
            } else Trace((std::string("Diagnostic live entry reference: ")+label+" index="+std::to_string(i)+" field="+field+" type="+valueType).c_str());
        }
    }catch(const std::exception& e){Trace((std::string("Diagnostic live entry unavailable: ")+label+" index="+std::to_string(i)+" reason="+e.what()).c_str());}
}
bool DiagnoseLiveBindings() {
    const int scene=PerformanceScene();if(!scene||!fields_||!fieldFlags_||!fieldValue_)return false;
    void* sceneClass=Class("Campus.Live","LiveScenePresenter"),*componentClass=Class("UnityEngine","Component");
    if(!sceneClass||!componentClass||!assignable_(componentClass,sceneClass))return false;
    void* presenter=nullptr;
    for(void* candidate:Objects(sceneClass))try {
        if(objectClass_(candidate)!=sceneClass)continue;
        void* go=GameObject(candidate);if(SceneOf(go)!=scene||!Bool(go,"get_activeInHierarchy"))continue;
        if(presenter){Trace("Diagnostic live bindings: more than one active presenter; skipped");return false;}
        presenter=candidate;
    }catch(const std::exception&){}
    if(!presenter){Trace("Diagnostic live bindings: no unique active presenter");return false;}
    void* playerClass=Class("Campus.Live","LivePlayerPresenter"),*live3dClass=Class("Campus.Live","Live3DPresenter"),*binderClass=Class("Campus.Common.Live","CampusLiveBinder");
    if(!playerClass||!live3dClass||!binderClass){Trace("Diagnostic live bindings: required types unavailable");return false;}
    Pin player(*this,DiagnosticReferenceField(presenter,"_livePlayer",playerClass));
    if(!player.Get()){Trace("Diagnostic live bindings: live player unavailable");return false;}
    Pin live3d(*this,DiagnosticReferenceField(player.Get(),"_3dLive",live3dClass));
    if(!live3d.Get()){Trace("Diagnostic live bindings: 3D presenter unavailable");return false;}
    Trace(("Diagnostic live bindings BEGIN: scene="+std::to_string(scene)+" (read only)").c_str());
    Pin playerActors(*this,DiagnosticReferenceField(player.Get(),"_actors"));DiagnosticActorCollection("player._actors",playerActors.Get());
    Pin liveActors(*this,DiagnosticReferenceField(live3d.Get(),"_actors"));DiagnosticActorCollection("3d._actors",liveActors.Get());
    Pin binder(*this,DiagnosticReferenceField(live3d.Get(),"_binder",binderClass));
    if(binder.Get()) {
        Pin actors(*this,DiagnosticReferenceField(binder.Get(),"<Actors>k__BackingField"));DiagnosticActorCollection("binder.Actors",actors.Get());
        Pin virtualActors(*this,DiagnosticReferenceField(binder.Get(),"<VirtualActors>k__BackingField"));DiagnosticActorCollection("binder.VirtualActors",virtualActors.Get());
    } else Trace("Diagnostic live bindings: binder unavailable");
    Trace("Diagnostic live bindings END");return true;
}
