//! Scene tree and user annotations. No Unity calls, guessed tags never change
//! visibility. Persistent identities use scene name plus a unique full path.
use serde_json::{json,Value};
use std::{collections::{BTreeMap,HashMap,HashSet},fs::{self,OpenOptions},io::{self,Write},path::Path,time::{SystemTime,UNIX_EPOCH}};

#[derive(Clone,Debug)]
pub struct TreeRow {
    pub path: Vec<String>,
    pub value:Value,
    /// Runtime key for the UI selection; must be revalidated by the plugin.
    pub key:String,
    /// Stable scene/path identity. None means that saving a note is unsafe.
    pub note_key:Option<String>,
    /// Stable when possible; generation-independent fallback within this session.
    pub collapse_key:String,
    pub depth:usize,
    pub has_children:bool,
    pub expanded:bool,
    pub guess:Option<&'static str>,
    pub note:String,
}
fn str_field<'a>(v:&'a Value,k:&str)->&'a str {v[k].as_str().unwrap_or("")}
fn id(v:&Value,k:&str)->i64 {v[k].as_i64().unwrap_or(0)}
fn selected_key(v:&Value)->String {format!("{}:{}:{}",v["scene"],v["kind"],v["instance"])}

/// Conservative name-based suggestions only; uncertain items stay unlabelled.
pub fn guess(name:&str)->Option<&'static str> {
    let s=name.to_lowercase();
    let tokens:Vec<&str>=s.split(|c:char|!c.is_ascii_alphanumeric()).filter(|w|!w.is_empty()).collect();
    let matches=|english:&[&str],chinese:&[&str]|english.iter().any(|word|tokens.iter().any(|t|t.starts_with(word)))||chinese.iter().any(|word|s.contains(word));
    if matches(&["light","lamp","spotlight"],&["灯光","灯具"]){Some("灯光")}
    else if matches(&["floor","ground"],&["地板","地面"]){Some("地面")}
    else if matches(&["chair","bench","seat","desk","table"],&["椅","凳","桌"]){Some("桌椅")}
    else if matches(&["stage","stg"],&["舞台"]){Some("舞台")}
    else if matches(&["wall"],&["壁","墙"]){Some("墙面")}
    else if matches(&["sky","cloud"],&["天空","云"]){Some("天空")}
    else if matches(&["tree","plant","flower","bush"],&["树","植物","花"]){Some("植物")}
    else if matches(&["screen","monitor","display"],&["屏幕"]){Some("屏幕")}
    else {None}
}

pub fn function_tag(name: &str) -> Option<&'static str> {
    let name = name.to_lowercase().replace(['_', '-', ' '], "");
    [
        (&["floodlight", "泛光灯"][..], "泛光灯"),
        (&["bloom", "glow", "泛光"], "泛光"),
        (&["colorgrading", "colorcorrection", "lutfilter", "调色"], "色彩分级"),
        (&["filter", "滤镜"], "滤镜"),
        (&["depthoffield", "dof", "景深"], "景深"),
        (&["lensflare", "paraffin", "光晕"], "镜头光晕"),
        (&["vignette", "暗角"], "暗角"),
        (&["tonemap"], "色调映射"),
        (&["motionblur"], "动态模糊"),
        (&["truss", "lightrig", "lightframe", "灯架"], "灯架"),
        (&["spotlight", "聚光灯"], "聚光灯"),
        (&["pointlight"], "点光源"),
        (&["decolight", "reallight", "lightfixture"], "舞台灯具"),
        (&["ledscreen", "ledwall"], "舞台屏幕"),
        (&["backdrop", "幕布"], "背景幕布"),
        (&["decal"], "贴花"),
        (&["trolley", "移动箱"], "移动箱"),
        (&["dolly", "摄影车"], "轨道设备"),
        (&["microphone", "麦克风"], "麦克风"),
        (&["suitcase", "行李箱"], "行李箱"),
    ].into_iter().find(|(names, _)| names.iter().any(|marker| name.contains(marker))).map(|(_, tag)| tag)
}

pub fn category(v: &Value) -> &'static str {
    let name = str_field(v, "name");
    match id(v, "kind") {
        1 => "角色道具",
        2 => "背景与装饰",
        3 => "灯光与后处理",
        _ => match function_tag(name) {
            Some("贴花" | "背景幕布" | "舞台屏幕") => "背景与装饰",
            Some("泛光灯" | "泛光" | "色彩分级" | "滤镜" | "景深" | "镜头光晕" | "暗角" |
                "色调映射" | "动态模糊" | "灯架" | "聚光灯" | "点光源" | "舞台灯具") => "灯光与后处理",
            _ => "舞台结构",
        },
    }
}

/// Keep native resource identities and hide duplicate scene rows for recognized props.
pub fn separated_resources(resources: &[Value]) -> Vec<Value> {
    let moving: HashSet<_> = resources.iter().filter(|v| id(v, "kind") == 1)
        .map(|v| (id(v, "scene"), id(v, "instance"))).collect();
    let parents: HashMap<_, _> = resources.iter().filter(|v| id(v, "kind") >= 2)
        .map(|v| ((id(v, "scene"), id(v, "instance")), id(v, "parent"))).collect();
    let mut protected = HashSet::new();
    for &(scene, prop) in &moving {
        let mut current = prop;
        for _ in 0..128 {
            let Some(&parent) = parents.get(&(scene, current)) else { break };
            if parent == 0 || !protected.insert((scene, parent)) { break; }
            current = parent;
        }
    }
    resources.iter().filter_map(|v| {
        if id(v, "kind") < 2 { return Some(v.clone()); }
        let scene = id(v, "scene");
        let instance = id(v, "instance");
        if moving.contains(&(scene, instance)) { return None; }
        // The controllable prop is the root of its visual subtree. Keep that
        // identity, and list its descendants under it for inspection.
        let mut row = v.clone();
        let mut current = instance;
        for _ in 0..128 {
            if moving.contains(&(scene, current)) {
                row["moving_member"] = true.into();
                row["locked"] = true.into();
                return Some(row);
            }
            let Some(&parent) = parents.get(&(scene, current)) else { break };
            if parent == 0 { break; }
            current = parent;
        }
        if protected.contains(&(scene, instance)) {
            row["locked"] = true.into();
            row["contains_moving_asset"] = true.into();
        }
        Some(row)
    }).collect()
}

/// Build visible rows for either the scene tree or props. Expanding and search
/// are view-only. Search retains all ancestors and temporarily expands matches.
pub fn build(resources:&[Value],scene_name:&str,props:bool,collapsed:&HashSet<String>,query:&str,notes:&BTreeMap<String,String>)->Vec<TreeRow> {
    let items:Vec<Value>=resources.iter().filter(|v|{
        let member=v["moving_member"]==true;
        if props { id(v,"kind")==1||member } else { id(v,"kind")>=2&&!member }
    }).cloned().collect();
    let n=items.len();let mut ids:HashMap<(i64,i64),Vec<usize>>=HashMap::new();
    for (i,v) in items.iter().enumerate(){ids.entry((id(v,"scene"),id(v,"instance"))).or_default().push(i);}
    let mut parent=vec![None;n];let mut unsafe_path=vec![false;n];let mut children=vec![Vec::<usize>::new();n];
    for (i,v) in items.iter().enumerate(){let p=id(v,"parent");if p!=0 {if let Some(xs)=ids.get(&(id(v,"scene"),p)) {if xs.len()==1&&xs[0]!=i {parent[i]=Some(xs[0]);}}if parent[i].is_none(){unsafe_path[i]=true;}}}
    // Cut cycles, which otherwise make a subtree disappear or loop forever.
    for i in 0..n {let mut seen=HashSet::new();let mut cur=Some(i);while let Some(x)=cur {if !seen.insert(x){parent[i]=None;unsafe_path[i]=true;break;}cur=parent[x];}}
    for i in 0..n {if let Some(p)=parent[i]{children[p].push(i);}}
    let mut paths=Vec::with_capacity(n);let mut path_counts:HashMap<String,usize>=HashMap::new();
    for i in 0..n {
        let mut chain=Vec::new();let mut cur=Some(i);let mut valid=true;let mut seen=HashSet::new();
        while let Some(x)=cur {if !seen.insert(x)||chain.len()>=128||unsafe_path[x] {valid=false;break;}let name=str_field(&items[x],"name");if name.is_empty() {valid=false;break;}chain.push(name.to_owned());cur=parent[x];}
        chain.reverse();let path=if valid&&!scene_name.is_empty(){Some(json!([scene_name,chain]).to_string())}else{None};
        if let Some(p)=&path {*path_counts.entry(p.clone()).or_default()+=1;}paths.push(path);
    }
    let stable:Vec<Option<String>>=(0..n).map(|i|{
        let mut cur=Some(i);while let Some(x)=cur {let p=paths[x].as_ref()?;if path_counts.get(p)!=Some(&1){return None;}cur=parent[x];}paths[i].clone()
    }).collect();
    let keys:Vec<String>=items.iter().enumerate().map(|(i,v)|stable[i].clone().unwrap_or_else(||format!("runtime:{}:{}",id(v,"scene"),id(v,"instance")))).collect();
    let q=query.trim().to_lowercase();let mut included=vec![q.is_empty();n];
    if !q.is_empty() {for i in 0..n {let v=&items[i];let note=stable[i].as_ref().and_then(|k|notes.get(k)).map(String::as_str).unwrap_or("");
        let candidate=format!("{} {} {} {} {}",str_field(v,"name"),category(v),guess(str_field(v,"name")).unwrap_or(""),function_tag(str_field(v,"name")).unwrap_or(""),note).to_lowercase();
        if candidate.contains(&q){let mut cur=Some(i);while let Some(x)=cur{if included[x]{break;}included[x]=true;cur=parent[x];}}
    }}
    let mut output=Vec::new();
    let mut pending:Vec<_>=(0..n).rev().filter(|&i|parent[i].is_none()).map(|i|(i,0)).collect();
    while let Some((i,depth))=pending.pop() {
        if !included[i] {continue;}
        let expandable=children[i].iter().any(|c|included[*c]);let expanded=expandable&&(!q.is_empty()||!collapsed.contains(&keys[i]));
        let note=stable[i].as_ref().and_then(|k|notes.get(k)).cloned().unwrap_or_default();
        let path=stable[i].as_ref().and_then(|k|serde_json::from_str::<(String,Vec<String>)>(k).ok()).map(|(_,names)|names).unwrap_or_default();
        output.push(TreeRow{path,value:items[i].clone(),key:selected_key(&items[i]),note_key:stable[i].clone(),collapse_key:keys[i].clone(),depth,has_children:expandable,expanded,guess:guess(str_field(&items[i],"name")),note});
        if expanded {pending.extend(children[i].iter().rev().map(|&child|(child,depth+1)));}
    }
    output
}

/// Open explicitly to detect corrupted or oversized files; callers should not
/// save when this fails. No notes are created or changed by merely browsing.
pub fn load_notes(path:&Path)->io::Result<BTreeMap<String,String>> {
    let bytes=match fs::read(path){Ok(v)=>v,Err(e) if e.kind()==io::ErrorKind::NotFound=>return Ok(BTreeMap::new()),Err(e)=>return Err(e)};
    if bytes.len()>2*1024*1024 {return Err(io::Error::new(io::ErrorKind::InvalidData,"备注文件过大"));}
    let v:Value=serde_json::from_slice(&bytes).map_err(io::Error::other)?;
    if v["version"]!=1 {return Err(io::Error::new(io::ErrorKind::InvalidData,"备注文件版本不支持"));}
    let entries=v["notes"].as_object().ok_or_else(||io::Error::new(io::ErrorKind::InvalidData,"备注格式错误"))?;
    if entries.len()>10000 {return Err(io::Error::new(io::ErrorKind::InvalidData,"备注数量过多"));}
    let mut out=BTreeMap::new();for (k,v) in entries {let s=v.as_str().ok_or_else(||io::Error::new(io::ErrorKind::InvalidData,"备注内容错误"))?;
        if k.len()>4096||!valid_note(s){return Err(io::Error::new(io::ErrorKind::InvalidData,"备注内容无效"));}out.insert(k.clone(),s.to_owned());}Ok(out)
}
#[cfg(windows)]
fn replace_file(from:&Path,to:&Path)->io::Result<()> {
    use std::os::windows::ffi::OsStrExt;
    #[link(name="kernel32")] extern "system" {fn MoveFileExW(from:*const u16,to:*const u16,flags:u32)->i32;}
    let a:Vec<u16>=from.as_os_str().encode_wide().chain(Some(0)).collect();let b:Vec<u16>=to.as_os_str().encode_wide().chain(Some(0)).collect();
    if unsafe{MoveFileExW(a.as_ptr(),b.as_ptr(),0x1|0x8)}==0{Err(io::Error::last_os_error())}else{Ok(())}
}
#[cfg(not(windows))]
fn replace_file(from:&Path,to:&Path)->io::Result<()> {fs::rename(from,to)}
/// Call only after a successful `load_notes`. Writes a unique temporary file in
/// the same directory, syncs it, then replaces the target atomically.
pub fn save_notes(path:&Path,notes:&BTreeMap<String,String>)->io::Result<()> {
    if notes.len()>10000||notes.iter().any(|(k,v)|k.len()>4096||!valid_note(v)){return Err(io::Error::new(io::ErrorKind::InvalidInput,"备注内容无效"));}
    let bytes=serde_json::to_vec(&json!({"version":1,"notes":notes})).map_err(io::Error::other)?;
    if bytes.len()>2*1024*1024{return Err(io::Error::new(io::ErrorKind::InvalidInput,"备注文件过大"));}
    write_atomic(path, &bytes)
}

pub(crate) fn write_atomic(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let parent = path.parent().ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "保存路径无效"))?;
    fs::create_dir_all(parent)?;
    let nonce = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();
    let temp = path.with_extension(format!("{}.{}.tmp", std::process::id(), nonce));
    let mut file = OpenOptions::new().create_new(true).write(true).open(&temp)?;
    let result = file.write_all(bytes).and_then(|_| file.sync_all());
    drop(file);
    let result = result.and_then(|_| replace_file(&temp, path));
    if result.is_err() { let _ = fs::remove_file(temp); }
    result
}

#[cfg(test)]mod tests {
    use super::*;
    fn node(id:i32,parent:i32,name:&str)->Value {json!({"scene":10,"generation":1,"kind":4,"instance":id,"parent":parent,"name":name})}
    #[test] fn hierarchy_search_and_stable_identity(){
        let nodes=vec![node(1,0,"Stage"),node(2,1,"Floor"),node(3,2,"Table")];let notes=BTreeMap::new();let mut closed=HashSet::new();
        let rows=build(&nodes,"Live",false,&closed,"",&notes);assert_eq!(rows.len(),3);assert_eq!(rows[2].depth,2);assert_eq!(rows[1].guess,Some("地面"));assert!(rows[2].note_key.is_some());
        let stable=rows[2].note_key.clone();closed.insert(rows[0].collapse_key.clone());assert_eq!(build(&nodes,"Live",false,&closed,"",&notes).len(),1);
        let search=build(&nodes,"Live",false,&closed,"桌椅",&notes);assert_eq!(search.len(),3);
        let mut changed=nodes.clone();for x in &mut changed{x["scene"]=999.into();x["generation"]=20.into();}assert_eq!(build(&changed,"Live",false,&HashSet::new(),"",&notes)[2].note_key,stable);
        assert_ne!(build(&nodes,"Other",false,&HashSet::new(),"",&notes)[2].note_key,stable);
    }
    #[test] fn guess_does_not_match_unrelated_words(){assert_eq!(guess("street"),None);assert_eq!(guess("highlight"),None);assert_eq!(guess("mdl_env_stg-parts"),Some("舞台"));}
    #[test]
    fn moving_props_are_not_fixed_rows_and_protect_shared_ancestors() {
        let prop = json!({"scene":10,"generation":1,"kind":1,"instance":2,"name":"Prop_Microphone"});
        let resources = separated_resources(&[node(1,0,"Stage"),node(2,1,"Prop_Microphone"),
            node(3,2,"MicrophoneMesh"),node(4,1,"PostEffect_Bloom"),prop]);
        assert_eq!(resources.len(), 4);
        assert_eq!(resources[0]["locked"], true);
        assert_eq!(resources[0]["contains_moving_asset"], true);
        assert_eq!(category(&resources[2]), "灯光与后处理");
        let fixed = build(&resources, "Live", false, &HashSet::new(), "泛光", &BTreeMap::new());
        assert_eq!(fixed.len(), 2);
        let props = build(&resources, "Live", true, &HashSet::new(), "麦克风", &BTreeMap::new());
        assert_eq!(props.len(), 2);
        let parts = build(&resources, "Live", true, &HashSet::new(), "MicrophoneMesh", &BTreeMap::new());
        assert_eq!(parts.len(), 2);
        assert_eq!(parts[1].depth, 1);
        assert_eq!(parts[1].value["locked"], true);
    }
    #[test] fn ambiguous_paths_are_not_persisted(){let nodes=vec![node(1,0,"Same"),node(2,0,"Same")];let rows=build(&nodes,"Live",false,&HashSet::new(),"",&BTreeMap::new());assert!(rows.iter().all(|x|x.note_key.is_none()));assert!(build(&nodes,"",false,&HashSet::new(),"",&BTreeMap::new()).iter().all(|x|x.note_key.is_none()));}
    #[test] fn note_storage_roundtrip_and_rejects_corruption(){let nonce=SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();let dir=std::env::current_dir().unwrap().join("target").join(format!("gaku-scene-notes-{}-{nonce}",std::process::id()));fs::create_dir_all(&dir).unwrap();let path=dir.join("notes.json");let mut v=load_notes(&path).unwrap();v.insert("scene/path".into(),"桌椅".into());save_notes(&path,&v).unwrap();assert_eq!(load_notes(&path).unwrap(),v);v.insert("other".into(),"舞台".into());save_notes(&path,&v).unwrap();assert_eq!(load_notes(&path).unwrap(),v);fs::write(&path,b"broken").unwrap();assert!(load_notes(&path).is_err());fs::remove_file(path).unwrap();fs::remove_dir(dir).unwrap();}
}

pub fn valid_note(text:&str)->bool {text.len()<=1024&&!text.chars().any(|c|c.is_control()&&c!='\n')}
