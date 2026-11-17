//! Native resource workspace. Local HTTP and file reads run away from the UI thread.
use crate::slint_ui::{Entry,Manager};
use serde_json::{json,Value};
use slint::{ModelRc,VecModel};
use std::{collections::BTreeMap,io::{Read,Write},net::{TcpStream,SocketAddr},path::PathBuf,rc::Rc,sync::mpsc,time::{Duration,Instant}};

fn encode(s:&str)->String {s.bytes().map(|b|if b.is_ascii_alphanumeric()||b"-._~".contains(&b){(b as char).to_string()}else{format!("%{b:02X}")}).collect()}
fn raw_request(path:&str,body:Option<(Vec<u8>,&str)>)->Result<Value,String>{
    let addr:SocketAddr="127.0.0.1:8777".parse().unwrap();
    let mut stream=TcpStream::connect_timeout(&addr,Duration::from_secs(3)).map_err(|e|format!("资源服务尚未就绪：{e}"))?;
    stream.set_read_timeout(Some(Duration::from_secs(30))).ok();stream.set_write_timeout(Some(Duration::from_secs(30))).ok();
    let (method,data,ctype)=match body {Some((b,t))=>("POST",b,t),None=>("GET",vec![],"")};
    let header=format!("{method} {path} HTTP/1.1\r\nHost: 127.0.0.1:8777\r\nConnection: close\r\nX-Gakumas-Client: 1\r\nContent-Type: {ctype}\r\nContent-Length: {}\r\n\r\n",data.len());
    stream.write_all(header.as_bytes()).and_then(|_|stream.write_all(&data)).map_err(|e|e.to_string())?;
    let mut response=Vec::new();stream.take(140*1024*1024).read_to_end(&mut response).map_err(|e|e.to_string())?;
    let split=response.windows(4).position(|x|x==b"\r\n\r\n").ok_or("资源服务响应无效")?;
    let head=String::from_utf8_lossy(&response[..split]);let ok=head.lines().next().is_some_and(|l|l.contains(" 200 ")||l.contains(" 202 "));
    let value:Value=serde_json::from_slice(&response[split+4..]).map_err(|e|format!("资源服务响应解析失败：{e}"))?;
    if ok {Ok(value)} else {Err(value["error"].as_str().unwrap_or("资源服务请求失败").into())}
}
fn request(path:&str,body:Option<(Vec<u8>,&str)>)->Result<Value,String>{
    // Never send catalog mutations to an unrelated service occupying the port.
    let meta=raw_request("/api/meta",None)?;
    let expected=std::env::var_os("GAKUACTORS_RESOURCE_DATA").map(PathBuf::from).or_else(||std::env::var_os("LOCALAPPDATA").map(|v|PathBuf::from(v).join("GakuActors/ResourceLibrary"))).ok_or("无法定位资源库数据目录")?;
    let actual=PathBuf::from(text(&meta,"data_root"));
    if text(&meta,"version")!="0.1.0" || expected.canonicalize().ok().zip(actual.canonicalize().ok()).map_or(true,|(a,b)|a!=b) {return Err("资源服务的数据目录不匹配，请检查 8777 端口。".into());}
    if path=="/api/meta"&&body.is_none(){return Ok(meta);}
    raw_request(path,body)
}
fn get(path:&str)->Result<Value,String>{request(path,None)}
fn post(path:&str,v:Value)->Result<Value,String>{request(path,Some((v.to_string().into_bytes(),"application/json")))}
fn text<'a>(v:&'a Value,k:&str)->&'a str {v[k].as_str().unwrap_or("")}
fn bytes(n:u64)->String {if n>=1048576 {format!("{:.2} MiB",n as f64/1048576.)}else{format!("{:.1} KiB",n as f64/1024.)}}
fn model(v:Vec<Entry>)->ModelRc<Entry>{Rc::new(VecModel::from(v)).into()}
#[derive(Debug)] struct Reply {kind:String,seq:u64,result:Result<Value,String>}

pub struct Library {tx:mpsc::Sender<Reply>,rx:mpsc::Receiver<Reply>,seq:u64,search_seq:u64,selection:BTreeMap<String,String>,items:Vec<Value>,jobs:Vec<Value>,job:String,page:usize,total:usize,plan:Option<Value>,fingerprint:String,data_root:PathBuf,last_jobs:Instant,last_meta:Instant,busy:bool}
impl Library {
 pub fn new()->Self {let(tx,rx)=mpsc::channel();Self{tx,rx,seq:0,search_seq:0,selection:BTreeMap::new(),items:vec![],jobs:vec![],job:String::new(),page:1,total:0,plan:None,fingerprint:String::new(),data_root:PathBuf::new(),last_jobs:Instant::now()-Duration::from_secs(5),last_meta:Instant::now()-Duration::from_secs(10),busy:false}}
 fn spawn<F>(&mut self,kind:&str,f:F)->u64 where F:FnOnce()->Result<Value,String>+Send+'static {self.seq+=1;let seq=self.seq;let tx=self.tx.clone();let kind=kind.to_string();std::thread::spawn(move||{let result=f();let _=tx.send(Reply{kind,seq,result});});seq}
 fn search(&mut self,ui:&Manager) {let categories=["","model","texture","motion","stage","audio","other"];let category=categories.get(ui.get_library_category().max(0) as usize).unwrap_or(&"");let path=format!("/api/resources?q={}&category={}&page={}",encode(&ui.get_library_query()),category,self.page);self.search_seq=self.spawn("search",move||get(&path));}
 fn jobs(&mut self){self.last_jobs=Instant::now();self.spawn("jobs",||get("/api/jobs"));}
 fn meta(&mut self){self.last_meta=Instant::now();self.spawn("meta",||get("/api/meta"));}
 fn redraw(&self,ui:&Manager) {
   ui.set_library_selected(model(self.selection.iter().map(|(key,name)|Entry{key:key.as_str().into(),label:name.as_str().into(),..Default::default()}).collect()));
   ui.set_library_job_summary(self.jobs.first().map(|j|format!("{} · {} · {}/{}",job_status(text(j,"status")),text(j,"message"),j["completed"],j["total"])).unwrap_or_else(||"暂无任务".into()).into());
   ui.set_library_selection(format!("已选择 {} 项资源",self.selection.len()).into());ui.set_library_can_plan(!self.selection.is_empty()&&!self.busy);ui.set_library_can_submit(self.plan.is_some()&&!self.busy);
   ui.set_library_plan(self.plan.as_ref().map(|p|format!("{} 个文件 · {}\n依赖完整，可以开始处理。",p["files"],bytes(p["total_bytes"].as_u64().unwrap_or(0)))).unwrap_or_else(||"选择资源后检查依赖和下载体积。".into()).into());
   ui.set_library_page(format!("{} / {} · {} 项",self.page,((self.total+39)/40).max(1),self.total).into());
   ui.set_library_resources(model(self.items.iter().map(|e|{let k=text(e,"key");Entry{key:k.into(),label:text(e,"name").into(),detail:format!("{} · {} · {} · {} 个依赖{}",category_label(text(e,"category")),k,bytes(e["size"].as_u64().unwrap_or(0)),e["dependencies"].as_array().map_or(0,Vec::len),if e["cached"]==true{" · 发现缓存（使用时校验）"}else{""}).into(),selected:self.selection.contains_key(k),..Default::default()}}).collect()));
   ui.set_library_jobs(model(self.jobs.iter().map(|j|{let k=text(j,"id");let names=j["selected_names"].as_array().map(|a|a.iter().filter_map(Value::as_str).collect::<Vec<_>>().join(" · ")).unwrap_or_default();Entry{key:k.into(),label:names.into(),detail:format!("{} · {}/{} · {}",job_status(text(j,"status")),j["completed"],j["total"],text(j,"message")).into(),selected:self.job==k,..Default::default()}}).collect()));
   let job=self.jobs.iter().find(|j|text(j,"id")==self.job);let running=job.is_some_and(|j|matches!(text(j,"status"),"running"|"queued"));
   ui.set_library_can_cancel(running);ui.set_library_can_retry(job.is_some()&&!running&&!self.busy);ui.set_library_can_open(job.is_some_and(|j|!text(j,"output").is_empty() && std::path::Path::new(text(j,"output")).is_dir()));
   ui.set_library_job_detail(job.map(|j|format!("{}\n{}\n导出文件：{} 个\n{}",job_status(text(j,"status")),if text(j,"error").is_empty(){text(j,"message")}else{text(j,"error")},j["artifacts"].as_array().map_or(0,Vec::len),text(j,"output"))).unwrap_or_else(||"选择任务以查看进度和导出位置。".into()).into());
 }
 pub fn action(&mut self,ui:&Manager,action:&str,arg:&str){match action {
  "refresh"=>{self.meta();self.search(ui);self.jobs();},"jobs"=>self.jobs(),"search"|"category"=>{self.page=1;self.search(ui);},"prev"=>{self.page=self.page.saturating_sub(1).max(1);self.search(ui);},"next"=>{if self.page*40<self.total{self.page+=1;self.search(ui);}},
  "select"=>{if self.busy{return;}if self.selection.remove(arg).is_none(){if self.selection.len()>=100{ui.set_library_status("每次最多选择 100 项资源。".into());}else if let Some(i)=self.items.iter().find(|e|text(e,"key")==arg){self.selection.insert(arg.into(),text(i,"name").into());}}self.plan=None;},"clear"=>{if self.busy{return;}self.selection.clear();self.plan=None;},
  "plan"=>{if !self.selection.is_empty()&&!self.busy{self.busy=true;let keys:Vec<_>=self.selection.keys().cloned().collect();self.spawn("plan",move||post("/api/plan",json!({"keys":keys})));ui.set_library_status("正在检查依赖…".into());}},
  "submit"=>{if self.plan.is_some()&&!self.busy{self.busy=true;let keys:Vec<_>=self.selection.keys().cloned().collect();let mode=["auto","download","model","texture","motion"].get(ui.get_library_format().max(0) as usize).unwrap_or(&"auto").to_string();self.spawn("submit",move||post("/api/jobs",json!({"keys":keys,"mode":mode,"max_mb":256,"unity_version":"6000.0.77f1"})));ui.set_library_status("正在提交任务…".into());}},
  "job"=>self.job=arg.into(),"cancel"|"retry"=>{if !self.job.is_empty()&&!self.busy{self.busy=true;let id=self.job.clone();let path=if action=="cancel"{"/api/cancel"}else{"/api/retry"};self.spawn("job-action",move||post(path,json!({"id":id})));}},
  "open"=>{if let Some(j)=self.jobs.iter().find(|j|text(j,"id")==self.job){let p=PathBuf::from(text(j,"output"));if let (Ok(p),Ok(root))=(p.canonicalize(),self.data_root.canonicalize()){if p.starts_with(root.join("jobs")) {use std::os::windows::process::CommandExt;let _=std::process::Command::new("explorer.exe").arg(p).creation_flags(0x08000000).spawn();}}}},
  "import"=>{if !self.busy{if let Some(path)=choose_file(){self.busy=true;self.spawn("import",move||{let data=std::fs::read(path).map_err(|e|e.to_string())?;if data.is_empty()||data.len()>128*1048576{return Err("清单文件大小无效".into());}request("/api/import",Some((data,"application/octet-stream")))});ui.set_library_status("正在导入并校验清单…".into());}}},_=>{}
 }self.redraw(ui);}
 pub fn poll(&mut self,ui:&Manager){if ui.get_library_mode()&&self.last_jobs.elapsed()>Duration::from_secs(3){self.jobs();}if ui.get_library_mode()&&self.last_meta.elapsed()>Duration::from_secs(8){self.meta();}while let Ok(r)=self.rx.try_recv(){if r.kind=="search"&&r.seq!=self.search_seq{continue;}if matches!(r.kind.as_str(),"plan"|"submit"|"job-action"|"import"){self.busy=false;}match r.result {Err(e)=>{ui.set_library_status(e.into());},Ok(v)=>match r.kind.as_str(){
  "meta"=>{let fp=text(&v["active"],"fingerprint").to_string();if fp!=self.fingerprint{self.selection.clear();self.plan=None;self.search(ui);}self.fingerprint=fp;self.data_root=PathBuf::from(text(&v,"data_root"));ui.set_library_manifest(if v["active"].is_object(){format!("修订 {} · {} 项",v["active"]["revision"],v["active"]["entry_count"])}else{"尚未导入清单".into()}.into());ui.set_library_status("资源库已连接 · 可离线浏览已保存清单".into());},
  "search"=>{self.items=v["items"].as_array().cloned().unwrap_or_default();self.total=v["total"].as_u64().unwrap_or(0) as usize;},"jobs"=>{self.jobs=v.as_array().cloned().unwrap_or_default();},
  "plan"=>{let selected=v["selected"].as_array().map(|a|a.iter().filter_map(Value::as_str).collect::<std::collections::BTreeSet<_>>()).unwrap_or_default();let current=self.selection.keys().map(String::as_str).collect::<std::collections::BTreeSet<_>>();if selected!=current || text(&v["manifest"],"fingerprint")!=self.fingerprint {self.plan=None;ui.set_library_status("清单或选择已改变，请重新检查依赖。".into());}else if v["missing"].as_array().is_some_and(|a|!a.is_empty()){self.plan=None;ui.set_library_status("存在缺失依赖，无法提交任务。".into());}else{self.plan=Some(v);ui.set_library_status("依赖检查完成。".into());}},
  "submit"=>{self.selection.clear();self.plan=None;self.job=text(&v,"id").into();ui.set_library_status("任务已提交，可继续使用桌面程序。".into());self.jobs();self.search(ui);},"job-action"=>{ui.set_library_status("任务操作已提交。".into());self.jobs();},"import"=>{self.selection.clear();self.plan=None;self.page=1;ui.set_library_status("清单导入成功。".into());self.meta();self.search(ui);},_=>{}}}self.redraw(ui);}}
}
fn category_label(s:&str)->&str{match s{"model"=>"模型部件","texture"=>"图片与贴图","motion"=>"动作","stage"=>"舞台与场景","audio"=>"音频",_=>"其他资源"}}
fn job_status(s:&str)->&str{match s{"queued"=>"等待处理","running"=>"处理中","completed"=>"已完成","failed"=>"失败","canceled"=>"已取消","interrupted"=>"已中断",_=>"未知"}}

#[repr(C)] struct OpenFile {size:u32,owner:*mut std::ffi::c_void,instance:*mut std::ffi::c_void,filter:*const u16,custom:*mut u16,max_custom:u32,index:u32,file:*mut u16,max_file:u32,file_title:*mut u16,max_title:u32,initial_dir:*const u16,title:*const u16,flags:u32,file_offset:u16,ext_offset:u16,default_ext:*const u16,custom_data:isize,hook:*mut std::ffi::c_void,template:*const u16,reserved:*mut std::ffi::c_void,reserved2:u32,flags_ex:u32}
#[link(name="comdlg32")] extern "system" {fn GetOpenFileNameW(file:*mut OpenFile)->i32;}
fn choose_file()->Option<PathBuf>{let mut buf=[0u16;32768];let filter:Vec<u16>="清单文件\0*.json;octocacheevai;*.manifest\0所有文件\0*.*\0\0".encode_utf16().collect();let mut f:OpenFile=unsafe{std::mem::zeroed()};f.size=std::mem::size_of::<OpenFile>() as u32;f.filter=filter.as_ptr();f.file=buf.as_mut_ptr();f.max_file=buf.len() as u32;f.flags=0x1000|0x800|0x8;if unsafe{GetOpenFileNameW(&mut f)}==0{return None;}let end=buf.iter().position(|c|*c==0).unwrap_or(buf.len());Some(PathBuf::from(String::from_utf16_lossy(&buf[..end])))}
#[cfg(test)] mod tests {#[test]fn encodes_utf8_query(){assert_eq!(super::encode("脸 &x"),"%E8%84%B8%20%26x");}#[test]fn status_names(){assert_eq!(super::job_status("completed"),"已完成");}}
