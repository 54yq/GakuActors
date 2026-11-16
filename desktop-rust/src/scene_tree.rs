//! Scene tree and user annotations. No Unity calls, guessed tags never change
//! visibility. Persistent identities use scene name plus a unique full path.
use serde_json::{json,Value};
use std::{collections::{BTreeMap,HashMap,HashSet},fs::{self,OpenOptions},io::{self,Write},path::Path,time::{SystemTime,UNIX_EPOCH}};

#[derive(Clone,Debug)]
pub struct TreeRow {
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

/// Build visible rows for either the scene tree or props. Expanding and search
/// are view-only. Search retains all ancestors and temporarily expands matches.
pub fn build(resources:&[Value],scene_name:&str,props:bool,collapsed:&HashSet<String>,query:&str,notes:&BTreeMap<String,String>)->Vec<TreeRow> {
    let items:Vec<Value>=resources.iter().filter(|v|if props{id(v,"kind")==1}else{id(v,"kind")>=2}).cloned().collect();
    let n=items.len();let mut ids:HashMap<(i64,i64),Vec<usize>>=HashMap::new();
    for (i,v) in items.iter().enumerate(){ids.entry((id(v,"scene"),id(v,"instance"))).or_default().push(i);}
    let mut parent=vec![None;n];let mut unsafe_path=vec![false;n];let mut children=vec![Vec::<usize>::new();n];
    if !props {for (i,v) in items.iter().enumerate(){let p=id(v,"parent");if p!=0 {if let Some(xs)=ids.get(&(id(v,"scene"),p)) {if xs.len()==1&&xs[0]!=i {parent[i]=Some(xs[0]);}}if parent[i].is_none(){unsafe_path[i]=true;}}}}
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
        let candidate=format!("{} {} {}",str_field(v,"name"),guess(str_field(v,"name")).unwrap_or(""),note).to_lowercase();
        if candidate.contains(&q){let mut cur=Some(i);while let Some(x)=cur{if included[x]{break;}included[x]=true;cur=parent[x];}}
    }}
    fn walk(i:usize,depth:usize,items:&[Value],children:&[Vec<usize>],stable:&[Option<String>],keys:&[String],included:&[bool],collapsed:&HashSet<String>,q:&str,notes:&BTreeMap<String,String>,out:&mut Vec<TreeRow>) {
        if !included[i] {return;}
        let expandable=children[i].iter().any(|c|included[*c]);let expanded=expandable&&(!q.is_empty()||!collapsed.contains(&keys[i]));
        let note=stable[i].as_ref().and_then(|k|notes.get(k)).cloned().unwrap_or_default();
        out.push(TreeRow{value:items[i].clone(),key:selected_key(&items[i]),note_key:stable[i].clone(),collapse_key:keys[i].clone(),depth,has_children:expandable,expanded,guess:guess(str_field(&items[i],"name")),note});
        if expanded {for &child in &children[i]{walk(child,depth+1,items,children,stable,keys,included,collapsed,q,notes,out);}}
    }
    let mut output=Vec::new();for i in 0..n {if parent[i].is_none(){walk(i,0,&items,&children,&stable,&keys,&included,collapsed,&q,notes,&mut output);}}
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
        if k.len()>4096||s.len()>1024||s.chars().any(char::is_control){return Err(io::Error::new(io::ErrorKind::InvalidData,"备注内容无效"));}out.insert(k.clone(),s.to_owned());}Ok(out)
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
    if notes.len()>10000||notes.iter().any(|(k,v)|k.len()>4096||v.len()>1024||v.chars().any(char::is_control)){return Err(io::Error::new(io::ErrorKind::InvalidInput,"备注内容无效"));}
    let parent=path.parent().ok_or_else(||io::Error::new(io::ErrorKind::InvalidInput,"备注路径无效"))?;fs::create_dir_all(parent)?;
    let nonce=SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();let temp=path.with_extension(format!("{}.{}.tmp",std::process::id(),nonce));
    let result=(||{let mut f=OpenOptions::new().create_new(true).write(true).open(&temp)?;let bytes=serde_json::to_vec(&json!({"version":1,"notes":notes})).map_err(io::Error::other)?;if bytes.len()>2*1024*1024{return Err(io::Error::new(io::ErrorKind::InvalidInput,"备注文件过大"));}f.write_all(&bytes)?;f.sync_all()?;drop(f);replace_file(&temp,path)})();
    if result.is_err(){let _=fs::remove_file(temp);}result
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
    #[test] fn ambiguous_paths_are_not_persisted(){let nodes=vec![node(1,0,"Same"),node(2,0,"Same")];let rows=build(&nodes,"Live",false,&HashSet::new(),"",&BTreeMap::new());assert!(rows.iter().all(|x|x.note_key.is_none()));assert!(build(&nodes,"",false,&HashSet::new(),"",&BTreeMap::new()).iter().all(|x|x.note_key.is_none()));}
    #[test] fn note_storage_roundtrip_and_rejects_corruption(){let nonce=SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_nanos();let dir=std::env::current_dir().unwrap().join("target").join(format!("gaku-scene-notes-{}-{nonce}",std::process::id()));fs::create_dir_all(&dir).unwrap();let path=dir.join("notes.json");let mut v=load_notes(&path).unwrap();v.insert("scene/path".into(),"桌椅".into());save_notes(&path,&v).unwrap();assert_eq!(load_notes(&path).unwrap(),v);v.insert("other".into(),"舞台".into());save_notes(&path,&v).unwrap();assert_eq!(load_notes(&path).unwrap(),v);fs::write(&path,b"broken").unwrap();assert!(load_notes(&path).is_err());fs::remove_file(path).unwrap();fs::remove_dir(dir).unwrap();}
}
