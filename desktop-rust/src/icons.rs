//! Persistent official PNG cache. Normal UI reads are offline; explicit
//! prefetch can download manifest-listed images into the plugin cache.
use std::io::{BufRead,BufReader,Read,Write};
use std::os::windows::process::CommandExt;
use std::path::Path;
use std::process::{Child,ChildStdin,ChildStdout,Command,Stdio};
use serde_json::{json,Value};

pub struct Reader {child:Child,input:ChildStdin,output:BufReader<ChildStdout>}
impl Drop for Reader {fn drop(&mut self){let _=self.child.kill();let _=self.child.wait();}}
fn command(game:&Path)->Result<Command,String>{
    // Send the embedded sources over stdin instead of a large -c argument,
    // keeping well below the Windows command-line length limit.
    let bootstrap="import json,sys,types\nb=json.loads(sys.stdin.readline())\nm=types.ModuleType('music_catalog')\nexec(b['music'],m.__dict__)\nsys.modules['music_catalog']=m\nexec(b['icons'],globals())";
    let mut command=Command::new(super::music::python()?);
    command.args(["-u","-c",bootstrap]).arg(game).creation_flags(0x08000000).env("PYTHONIOENCODING","utf-8");
    if let Some(deps)=std::env::current_exe().ok().and_then(|p|p.parent().map(|p|p.join("GakuActors.Desktop.deps"))).filter(|p|p.is_dir()){command.env("PYTHONPATH",deps);}
    Ok(command)
}
fn initialize(input:&mut ChildStdin)->Result<(),String>{
    let sources=json!({"music":super::music::SCRIPT,"icons":include_str!("model_icon_cache.py")}).to_string()+"\n";
    input.write_all(sources.as_bytes()).and_then(|_|input.flush()).map_err(|e|e.to_string())
}
impl Reader {
    pub fn start(game:&Path)->Result<Self,String>{
        let mut command=command(game)?;command.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
        let mut child=command.spawn().map_err(|e|format!("无法启动本地图标读取器：{e}"))?;
        let mut input=child.stdin.take().ok_or("图标输入不可用")?;initialize(&mut input)?;let output=BufReader::new(child.stdout.take().ok_or("图标输出不可用")?);
        Ok(Self{child,input,output})
    }
    pub fn read(&mut self,name:&str)->Result<super::thumbnail::Bitmap,String>{
        self.read_mode(name,false)
    }
    pub fn read_cached(&mut self,name:&str)->Result<super::thumbnail::Bitmap,String>{self.read_mode(name,true)}
    fn read_mode(&mut self,name:&str,cached_only:bool)->Result<super::thumbnail::Bitmap,String>{
        let request=json!({"name":name,"cached_only":cached_only}).to_string()+"\n";self.input.write_all(request.as_bytes()).and_then(|_|self.input.flush()).map_err(|e|e.to_string())?;
        let mut line=String::new();self.output.by_ref().take(750_000).read_line(&mut line).map_err(|e|e.to_string())?;
        let reply:Value=serde_json::from_str(&line).map_err(|_|"本地图标读取器未返回有效数据")?;
        let data=reply["data"].as_str().unwrap_or("");if data.is_empty(){return Err(reply["status"].as_str().unwrap_or("官方图标不可用").into());}super::thumbnail::decode(data)
    }
}
pub fn prefetch(game:&Path,download:bool)->Result<(),String>{
    let mut command=command(game)?;command.arg("--prefetch").stdin(Stdio::piped());if download{command.arg("--download");}
    let mut child=command.spawn().map_err(|e|e.to_string())?;let mut input=child.stdin.take().ok_or("图标输入不可用")?;initialize(&mut input)?;drop(input);
    if child.wait().map_err(|e|e.to_string())?.success(){Ok(())}else{Err("部分图标未预缓存成功，请查看 model-icons/prefetch-report.json".into())}
}
pub fn cached_pair(names:&(String,String),reader:&mut Option<Reader>,start_failed:&mut bool)->Option<(Option<super::thumbnail::Bitmap>,Option<super::thumbnail::Bitmap>)>{
    if names.0.is_empty()&&names.1.is_empty(){return None;}
    if reader.is_none()&&!*start_failed{match super::music::game_root().and_then(|root|Reader::start(&root)){Ok(value)=>*reader=Some(value),Err(_)=>*start_failed=true}}
    let reader=reader.as_mut()?;
    let body=if names.0.is_empty(){None}else{Some(reader.read_cached(&names.0).ok()?)};
    let head=if names.1.is_empty(){None}else{Some(reader.read_cached(&names.1).ok()?)};
    Some((body,head))
}
// Only exact catalog relations are used; no filename or hairstyle guessing.
pub fn names(snapshot:&Value,request:&Value)->(String,String){
    let find=|kind:&str,id:&str|snapshot[kind].as_array().and_then(|ps|ps.iter().find(|p|p["id"].as_str()==Some(id))).and_then(|p|p["thumbnail"].as_str()).unwrap_or("").to_owned();
    if request["action"]=="load_part_thumbnail"{let id=request["part_id"].as_str().unwrap_or("");return if request["kind"]=="head"{(String::new(),find("heads",id))}else{(find("bodies",id),String::new())};}
    let preset=snapshot["presets"].as_array().and_then(|ps|ps.iter().find(|p|p["id"]==request["preset_id"]));
    let Some(p)=preset else{return (String::new(),String::new());};
    let mut body=find("bodies",p["body"].as_str().unwrap_or(""));let mut head=find("heads",p["head"].as_str().unwrap_or(""));
    if body.is_empty()&&p["bodyAsset"].as_str().is_some_and(|s|!s.is_empty()){
        let rows:Vec<_>=snapshot["bodies"].as_array().map(|xs|xs.iter().filter(|x|x["resource"]==p["bodyAsset"]).collect()).unwrap_or_default();if rows.len()==1{body=rows[0]["thumbnail"].as_str().unwrap_or("").into();}
    }
    if head.is_empty()&&p["faceAsset"].as_str().is_some_and(|s|!s.is_empty())&&p["hairAsset"].as_str().is_some_and(|s|!s.is_empty()){
        let rows:Vec<_>=snapshot["heads"].as_array().map(|xs|xs.iter().filter(|x|x["resource"]==p["hairAsset"]&&x["face_resource"]==p["faceAsset"]).collect()).unwrap_or_default();if rows.len()==1{head=rows[0]["thumbnail"].as_str().unwrap_or("").into();}
    }
    (body,head)
}
pub fn fill(image:&mut Option<super::thumbnail::Bitmap>,name:&str,status:&mut String,reader:&mut Option<Reader>,start_failed:&mut bool){
    if image.is_some()||name.is_empty(){return;}
    if reader.is_none()&&!*start_failed{match super::music::game_root().and_then(|root|Reader::start(&root)){Ok(value)=>*reader=Some(value),Err(_)=>*start_failed=true}}
    if let Some(reader)=reader{match reader.read(name){Ok(bitmap)=>{*image=Some(bitmap);*status="系统模型预览图 · 本地缓存".into();},Err(error)=>*status=error}}
}
#[cfg(test)]mod tests{use super::*;#[test]fn raw_heads_require_both_native_relations(){let mut s=json!({"presets":[{"id":"p","hairAsset":"hair","faceAsset":"face"}],"heads":[{"id":"h1","resource":"hair","face_resource":"face","thumbnail":"exact"},{"id":"h2","resource":"hair","face_resource":"other","thumbnail":"wrong"}]});assert_eq!(names(&s,&json!({"action":"load_thumbnail","preset_id":"p"})).1,"exact");s["heads"][1]["face_resource"]="face".into();assert!(names(&s,&json!({"action":"load_thumbnail","preset_id":"p"})).1.is_empty());}}
