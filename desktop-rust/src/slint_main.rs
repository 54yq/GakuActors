#![windows_subsystem = "windows"]
#[rustfmt::skip]
#[path="main.rs"] #[allow(dead_code)] mod legacy;
mod slint_ui;
#[cfg(test)]
mod desktop_visual_checks;
mod offline_catalog;
mod offline_presets;
mod color_dialog;
mod scene_tree;
mod scene_workspace;
mod motion_assets;
mod people_catalog;
mod music_window;
mod resource_library;
use slint_ui::*;
use slint::{ComponentHandle,ModelRc,VecModel,SharedString,Image,SharedPixelBuffer,Rgba8Pixel};
use serde_json::{json,Value};
use std::{rc::Rc,cell::RefCell,collections::{BTreeMap,HashMap,HashSet,VecDeque},sync::mpsc,time::{Duration,Instant}};

const PAGE:usize=24;
const CACHE:usize=128;
fn s<'a>(v:&'a Value,k:&str)->&'a str {v[k].as_str().unwrap_or("")}
fn rows(v:&Value,k:&str)->Vec<Value>{v[k].as_array().cloned().unwrap_or_default()}
fn flag(v:&Value,k:&str)->bool{v[k].as_bool().unwrap_or(false)}
// Canonical display is [-180, 180); the plugin accepts equivalent 0–360°.
fn signed_yaw(value:f64)->f64 {(value+180.0).rem_euclid(360.0)-180.0}
#[cfg(test)] mod yaw_tests {
    #[test] fn signed_yaw_wraps_and_preserves_equivalent_rotations(){
        for (input,expected) in [(0.0,0.0),(90.0,90.0),(270.0,-90.0),(180.0,-180.0),(-181.0,179.0),(360.0,0.0),(540.0,-180.0)] {assert!((super::signed_yaw(input)-expected).abs()<1e-9);}
    }
}
fn catalog_notice(value:&str)->String{if value.contains("not found master db file"){"游戏已连接，但人物数据库尚未就绪。请等待游戏完成加载后点击“重试目录”。".into()}else{value.to_owned()}}
fn actor_visibility<'a>(resources: &'a [Value], actor: &Value) -> Option<&'a Value> {
    resources.iter().find(|resource| resource["kind"] == 0 && resource["owner"] == actor["instance"])
}
fn stage_markers(actors: &[Value], selection: &str) -> Vec<StageMarker> {
    fn coordinate(actor: &Value, axis: usize) -> Option<f64> {
        actor["position"][axis].as_f64().filter(|v| v.is_finite())
    }
    let positioned: Vec<_> = actors.iter().filter(|a|flag(a,"pose_supported")).filter_map(|a| Some((a, coordinate(a,0)?, coordinate(a,2)?))).collect();
    let limit = positioned.iter().fold(2.5_f64, |limit, (_,x,z)| limit.max(x.abs()).max(z.abs()));
    positioned.into_iter().map(|(actor,x,z)| {
        let id=key(actor);
        let name=s(actor,"name");
        StageMarker {
            key:id.clone().into(), name:name.into(),
            label:name.chars().last().unwrap_or('•').to_string().into(),
            x:(0.5+x/(2.0*limit)) as f32, z:(0.5+z/(2.0*limit)) as f32,
            selected:id==selection, ..Default::default()
        }
    }).collect()
}
fn model<T:Clone+'static>(xs:Vec<T>)->ModelRc<T>{Rc::new(VecModel::from(xs)).into()}
fn strings(xs:Vec<String>)->ModelRc<SharedString>{model(xs.into_iter().map(Into::into).collect())}
fn key(v:&Value)->String{format!("{}:{}:{}",v["scene"],v["kind"],v["instance"])}
fn image_key(snapshot:&Value,base:&str,request:&Value)->String {
    let names=legacy::icons::names(snapshot,request);
    let preset=if request["action"]=="load_thumbnail" {snapshot["presets"].as_array().into_iter().flatten().find(|p|p["id"]==request["preset_id"]).map(|p|format!("{}:{}:{}:{}:{}",s(p,"body"),s(p,"head"),s(p,"bodyAsset"),s(p,"faceAsset"),s(p,"hairAsset"))).unwrap_or_default()}else{String::new()};
    format!("{base}|{}|{}|{preset}",names.0,names.1)
}
fn initial_window_size(work_width:i32,work_height:i32,scale:f32)->(u32,u32) {
    let scale=if scale.is_finite()&&scale>0.0{scale}else{1.0};
    let available_width=(work_width-32).max(1) as f32;let available_height=(work_height-56).max(1) as f32;
    let fit=(available_width*0.94/(1320.0*scale)).min(available_height*0.94/(960.0*scale)).min(1.0);
    ((1320.0*scale*fit).round().max(1.0) as u32,(960.0*scale*fit).round().max(1.0) as u32)
}
// Native DPI is reliable only after winit creates the actual HWND. Applying a
// physical size before show() can turn 1320 logical pixels into 1320 physical
// pixels on a 150% desktop, leaving only 880 logical pixels for the layout.
fn apply_initial_window_size(window:&slint::Window)->bool {
    use slint::winit_030::{WinitWindowAccessor,winit::{dpi::PhysicalPosition,raw_window_handle::{HasWindowHandle,RawWindowHandle}}};
    #[repr(C)] #[derive(Default)] struct Rect{left:i32,top:i32,right:i32,bottom:i32}
    #[repr(C)] struct MonitorInfo{size:u32,monitor:Rect,work:Rect,flags:u32}
    #[link(name="user32")] extern "system" {
        fn MonitorFromWindow(hwnd:*mut std::ffi::c_void,flags:u32)->*mut std::ffi::c_void;
        fn GetMonitorInfoW(monitor:*mut std::ffi::c_void,info:*mut MonitorInfo)->i32;
        fn GetDpiForWindow(hwnd:*mut std::ffi::c_void)->u32;
    }
    let configuration=window.with_winit_window(|native| {
        let handle=native.window_handle().ok()?;
        let RawWindowHandle::Win32(handle)=handle.as_raw() else{return None;};
        let hwnd=handle.hwnd.get() as *mut std::ffi::c_void;
        let mut info=MonitorInfo{size:std::mem::size_of::<MonitorInfo>() as u32,monitor:Rect::default(),work:Rect::default(),flags:0};
        if unsafe{GetMonitorInfoW(MonitorFromWindow(hwnd,2),&mut info)}==0{return None;}
        let dpi=unsafe{GetDpiForWindow(hwnd)};
        let scale=if dpi>0{dpi as f32/96.0}else{native.scale_factor() as f32};
        let width=info.work.right-info.work.left;let height=info.work.bottom-info.work.top;
        let (w,h)=initial_window_size(width,height,scale);
        let outer=native.outer_size();let inner=native.inner_size();
        let frame_w=outer.width.saturating_sub(inner.width) as i32;let frame_h=outer.height.saturating_sub(inner.height) as i32;
        Some((w,h,info.work.left+((width-w as i32-frame_w)/2).max(0),info.work.top+((height-h as i32-frame_h)/2).max(0)))
    }).flatten();
    let Some((width,height,x,y))=configuration else{return false;};
    window.set_size(slint::PhysicalSize::new(width,height));
    window.with_winit_window(|native|native.set_outer_position(PhysicalPosition::new(x,y)));
    true
}
#[cfg(test)] mod window_sizing_tests {
    #[test] fn uses_work_area_and_respects_scaling(){assert_eq!(super::initial_window_size(1920,1080,1.0),(1320,960));for (sw,sh,dpi) in [(1024,768,1.5),(1920,1080,1.5),(2560,1440,1.25)] {let (w,h)=super::initial_window_size(sw,sh,dpi);assert!(w<=(sw-32) as u32&&h<=(sh-56) as u32);assert!((w as f64/h as f64-1.375).abs()<0.01);}assert_eq!(super::initial_window_size(3840,2160,1.0),(1320,960));}
    #[test] fn explains_unavailable_game_database(){assert!(super::catalog_notice("not found master db file").contains("重试目录"));}
    #[test] fn high_dpi_startup_preserves_logical_size_when_space_allows(){assert_eq!(super::initial_window_size(2560,1600,1.5),(1980,1440));assert_eq!(super::initial_window_size(3840,2160,2.0),(2640,1920));}
}
fn image(b:Option<legacy::thumbnail::Bitmap>)->Image {
    let Some(b)=b else{return Image::default()};let b=b.fit(192,192);let mut rgba=b.bgra;
    for p in rgba.chunks_exact_mut(4){p.swap(0,2);}
    Image::from_rgba8(SharedPixelBuffer::<Rgba8Pixel>::clone_from_slice(&rgba,b.width,b.height))
}
#[derive(Clone,Default)] struct Pictures{body:Image,head:Image}
enum Work {Reconnect,ReadCatalog,Command(Value),Picture(String,Value),SaveNotes(std::path::PathBuf,BTreeMap<String,String>)}
enum Event {IconsUpdated,Snapshot(Value),Picture(String,Option<legacy::thumbnail::Bitmap>,Option<legacy::thumbnail::Bitmap>),CatalogRead(Result<String,String>),PresetWriteFailed(String),NotesSaved(Result<(),String>),Probe(String),RetryFailed(String),Connected,Disconnected}

fn offline_snapshot(game: Option<&std::path::Path>) -> Value {
    let mut snapshot = json!({"status":"等待游戏和 GakuActors 插件","runtime":false});
    if let Some(game) = game {
        match offline_presets::load(game) {
            Ok(presets) => {
                snapshot["presets"] = presets.into();
                snapshot["writable"] = true.into();
            }
            Err(error) => snapshot["store_notice"] = error.into(),
        }
    }
    snapshot
}

// One pipe owner. Commands are drained before thumbnail work; memory and queues
// are bounded. No UI callback performs I/O or decodes images.
fn worker(mut client:Option<legacy::pipe::Client>,rx:mpsc::Receiver<Work>,tx:mpsc::SyncSender<Event>){
    let mut commands=VecDeque::new();let mut pictures=VecDeque::new();let mut pending:Option<(String,String,Instant,(String,String))>=None;
    let mut previous=Value::Null;let mut reconnect_requested=false;let mut manual_retry=false;let mut retry_at=Instant::now();let mut probe_notice=String::new();let mut reader=None;let mut reader_failed=false;
    let game=legacy::music::game_root().ok();let mut snapshot=offline_snapshot(game.as_deref());let mut catalog=game.as_deref().map(offline_catalog::load).unwrap_or(Value::Null);let mut saved_revision=0;let mut saved_shape=(0usize,0usize);let mut saved_at=Instant::now()-Duration::from_secs(3);
    if client.is_some(){let _=tx.send(Event::Connected);}
    let (index_tx,index_rx)=mpsc::channel();if let Some(game)=game.clone(){let missing=catalog.is_null();if missing || offline_catalog::needs_prefetch(&game){std::thread::spawn(move||{if missing{let _=legacy::icons::build_index(&game);let _=index_tx.send(());}let _=legacy::icons::background_prefetch(&game);let _=index_tx.send(());});}}
    loop {
        if client.is_none()&&Instant::now()>=retry_at {
            let result=game.as_deref().ok_or_else(||std::io::Error::new(std::io::ErrorKind::NotFound,"未配置游戏目录；资源库与导出仍可使用")).and_then(legacy::pipe::Client::connect_game);
            retry_at=Instant::now()+Duration::from_secs(2);
            match result {Ok(value)=>{client=Some(value);commands.push_front(json!({"version":1,"action":"reload"}));manual_retry=false;probe_notice.clear();previous=Value::Null;let _=tx.send(Event::Connected);},
                Err(error)=>{let notice=error.to_string();if notice!=probe_notice&&tx.try_send(Event::Probe(notice.clone())).is_ok(){probe_notice=notice.clone();}if manual_retry{manual_retry=false;let _=tx.send(Event::RetryFailed(notice));}}
            }
        }
        if index_rx.try_recv().is_ok(){if let Some(game)=game.as_deref(){catalog=offline_catalog::load(game);saved_revision=0;let _=tx.send(Event::IconsUpdated);}}
        let first=if commands.len()>16 {Err(mpsc::RecvTimeoutError::Timeout)}else{rx.recv_timeout(if pending.is_some(){Duration::from_millis(80)}else if !commands.is_empty()||!pictures.is_empty(){Duration::from_millis(5)}else{Duration::from_millis(50)})};
        let mut incoming=Vec::new();match first {Ok(w)=>incoming.push(w),Err(mpsc::RecvTimeoutError::Disconnected)=>break,Err(_)=>{}}
        if commands.len()<=16 {incoming.extend(rx.try_iter().take(64));}
        for w in incoming {match w {
            Work::Reconnect=>{reconnect_requested=true;},
            Work::ReadCatalog=>{
                let result = game.as_deref().ok_or_else(|| "未配置游戏安装目录。".to_owned()).and_then(|game| {
                    catalog = offline_catalog::load(game);
                    if catalog.is_null() {
                        legacy::icons::build_index(game)?;
                        catalog = offline_catalog::load(game);
                    }
                    let count = |key| catalog[key].as_array().map_or(0, Vec::len);
                    if count("bodies") == 0 || count("heads") == 0 {
                        return Err("游戏安装目录中没有可用的身体和头部目录。".into());
                    }
                    reader = None;
                    reader_failed = false;
                    Ok(format!("已读取本地目录 · 身体 {} · 头部 {}", count("bodies"), count("heads")))
                });
                let _=tx.send(Event::IconsUpdated);
                let _=tx.send(Event::CatalogRead(result));
            },
            Work::SaveNotes(path,notes)=>{let result=scene_tree::save_notes(&path,&notes).map_err(|e|e.to_string());let _=tx.send(Event::NotesSaved(result));},
            Work::Command(v)=>{if (v["action"]=="move"||v["action"]=="set_yaw")&&commands.back().is_some_and(|p:&Value|p["action"]==v["action"]&&p["instance"]==v["instance"]){commands.pop_back();}commands.push_back(v);},
            Work::Picture(k,v)=>{if pictures.len()<64{pictures.push_back((k,v));}else{let _=tx.send(Event::Picture(k,None,None));}}
        }}
        if reconnect_requested&&commands.is_empty()&&pending.is_none(){reconnect_requested=false;manual_retry=true;client=None;probe_notice.clear();retry_at=Instant::now();snapshot=offline_snapshot(game.as_deref());previous=Value::Null;let _=tx.send(Event::Disconnected);continue;}
        if pending.as_ref().is_some_and(|p|p.2.elapsed()>Duration::from_secs(5)){if let Some(p)=pending.take(){let _=tx.send(Event::Picture(p.0,None,None));}}
        let request=if let Some(v)=commands.pop_front(){v}else if pending.is_some(){json!({"version":1,"action":"thumbnail_status"})}else if let Some((k,v))=pictures.pop_front(){
            let visible=offline_catalog::merge(&snapshot,&catalog);let names=legacy::icons::names(&visible,&v);
            if let Some((a,b))=legacy::icons::cached_pair(&names,&mut reader,&mut reader_failed){let _=tx.send(Event::Picture(k,a.map(|x|x.fit(192,192)),b.map(|x|x.fit(192,192))));continue;}
            if !flag(&snapshot,"admitted"){let mut a=None;let mut b=None;let mut status=String::new();legacy::icons::fill(&mut a,&names.0,&mut status,&mut reader,&mut reader_failed);legacy::icons::fill(&mut b,&names.1,&mut status,&mut reader,&mut reader_failed);let _=tx.send(Event::Picture(k,a,b));continue;}
            let wire=if v["action"]=="load_thumbnail"{s(&v,"preset_id").to_owned()}else{format!("part:{}:{}",s(&v,"kind"),s(&v,"part_id"))};pending=Some((k,wire,Instant::now(),names));v
        }else{json!({"version":1,"action":"snapshot"})};
        let Some(pipe)=client.as_mut() else {
            if matches!(request["action"].as_str(), Some("save_preset" | "delete_preset")) {
                let result = game.as_deref().ok_or_else(|| "未配置游戏安装目录。".to_owned())
                    .and_then(|game| offline_presets::update(game, &request));
                match result {
                    Ok(presets) => {
                        snapshot["presets"] = presets.into();
                        snapshot["writable"] = true.into();
                        snapshot["store_notice"] = if request["action"] == "delete_preset" {"预设已删除。"} else {"预设已保存。"}.into();
                    }
                    Err(error) => { let _ = tx.send(Event::PresetWriteFailed(error)); }
                }
            }
            let visible=offline_catalog::merge(&snapshot,&catalog);if visible!=previous&&tx.try_send(Event::Snapshot(visible.clone())).is_ok(){previous=visible;}continue;
        };
        let mut reply=match pipe.request(&request){Ok(v)=>v,Err(_)=>{client=None;pending=None;commands.clear();pictures.clear();snapshot=offline_snapshot(game.as_deref());previous=Value::Null;let _=tx.send(Event::Disconnected);continue;}};
        if let Some(v)=reply.get("snapshot"){snapshot=v.clone();let rev=snapshot["catalog_revision"].as_u64().unwrap_or(0);let shape=(snapshot["resources"].as_array().map_or(0,|v|v.len()),snapshot["models"].as_array().map_or(0,|v|v.len()));if rev>0&&(rev!=saved_revision||(shape!=saved_shape&&saved_at.elapsed()>=Duration::from_secs(3))){if let Some(game)=game.as_deref(){offline_catalog::save(game,&snapshot);catalog=offline_catalog::load(game);}saved_revision=rev;saved_shape=shape;saved_at=Instant::now();}}
        if matches!(request["action"].as_str(),Some("load_thumbnail"|"load_part_thumbnail"))&&reply["accepted"]!=true {
            if let Some(p)=pending.take(){let _=tx.send(Event::Picture(p.0,None,None));}
        }
        if let Some(t)=reply.get("thumbnail") {
            if pending.as_ref().is_some_and(|p|p.1==s(t,"id"))&&s(t,"status")!="读取中"&&s(t,"head_status")!="读取中" {
                if let Some(p)=pending.take(){
                    let mut a=legacy::thumbnail::decode(s(t,"data")).ok();let mut b=legacy::thumbnail::decode(s(t,"head_data")).ok();let mut status=String::new();
                    legacy::icons::fill(&mut a,&p.3.0,&mut status,&mut reader,&mut reader_failed);legacy::icons::fill(&mut b,&p.3.1,&mut status,&mut reader,&mut reader_failed);
                    let _=tx.send(Event::Picture(p.0,a.map(|x|x.fit(192,192)),b.map(|x|x.fit(192,192))));
                }
            }
        }
        if reply["accepted"]==false&&!matches!(request["action"].as_str(),Some("thumbnail_status"|"load_thumbnail"|"load_part_thumbnail")){snapshot["notice"]="操作未通过游戏插件校验，请刷新后重试。".into();}
        if let Some(map)=reply.as_object_mut(){map.remove("thumbnail");}
        let visible=offline_catalog::merge(&snapshot,&catalog);if visible!=previous {if tx.try_send(Event::Snapshot(visible.clone())).is_ok(){previous=visible;}}
    }
}

// The checkbox expresses the requested state directly, independent of any
// older snapshot still in flight. Locked resources never produce a command.
fn visibility_request(r:&Value,hidden:bool)->Option<Value>{
    if flag(r,"locked"){return None;}
    Some(json!({"action":"hide_resource","scene":r["scene"],"instance":r["instance"],"kind":r["kind"],"generation":r["generation"],"hidden":hidden}))
}
#[cfg(test)] mod visibility_tests {
    use super::*;
    #[test] fn checkbox_state_is_sent_explicitly_and_locked_nodes_are_rejected(){
        let mut r=json!({"scene":4,"instance":5,"kind":1,"generation":7,"hidden":false,"locked":false});
        assert_eq!(visibility_request(&r,true).unwrap(),json!({"action":"hide_resource","scene":4,"instance":5,"kind":1,"generation":7,"hidden":true}));
        assert_eq!(visibility_request(&r,false).unwrap()["hidden"],false);
        r["locked"]=true.into();assert!(visibility_request(&r,true).is_none());assert!(visibility_request(&r,false).is_none());
    }
}
#[derive(Default)]
struct State {
    snapshot:Value,tx:Option<mpsc::SyncSender<Work>>,connected:bool,connection_detail:String,character:String,preset:String,actor:String,resource:String,
    body:String,head:String,edit_id:String,raw:[Vec<String>;3],cache:HashMap<String,Pictures>,lru:VecDeque<String>,requested:HashSet<String>,
    tree_rows:Vec<scene_tree::TreeRow>,collapsed:HashSet<String>,notes:BTreeMap<String,String>,notes_path:Option<std::path::PathBuf>,notes_error:bool,yaw_dirty:bool,
    pages:[usize;3],catalog_scan:bool,game_configured:bool,reference:i64,stage_initialized:bool,confirm:Option<Value>,undo:Option<Value>,save:Option<(Value,Instant)>,message:String,pose_dirty:bool,reset:Option<(i64,u64)>,reset_yaw:Option<(i64,u64)>,
}
impl State {
    fn send(&mut self,mut v:Value){if !self.connected&&!matches!(v["action"].as_str(),Some("save_preset"|"delete_preset")){return;}v["version"]=1.into();if let Some(tx)=&self.tx {if tx.try_send(Work::Command(v)).is_err(){self.message="操作队列忙，请稍后重试。".into();}}else{self.message="游戏插件未连接。".into();}}
    fn request(&mut self,k:String,v:Value){if self.cache.contains_key(&k){self.lru.retain(|x|x!=&k);self.lru.push_back(k);return;}if self.requested.contains(&k){return;}if let Some(tx)=&self.tx{if tx.try_send(Work::Picture(k.clone(),v)).is_ok(){self.requested.insert(k);}}}
    fn picture(&mut self,k:String,a:Option<legacy::thumbnail::Bitmap>,b:Option<legacy::thumbnail::Bitmap>){
        self.requested.remove(&k);if !self.cache.contains_key(&k)&&self.cache.len()>=CACHE {if let Some(old)=self.lru.pop_front(){self.cache.remove(&old);}}
        self.lru.retain(|x|x!=&k);self.lru.push_back(k.clone());self.cache.insert(k,Pictures{body:image(a),head:image(b)});
    }
    fn entry(&mut self,v:&Value,kind:&str,selected:&str)->Entry{
        let id=s(v,"id");let k=if kind=="preset"{format!("preset:{id}")}else{format!("part:{kind}:{id}")};
        let request=if kind=="preset"{json!({"version":1,"action":"load_thumbnail","preset_id":id})}else{json!({"version":1,"action":"load_part_thumbnail","kind":kind,"part_id":id})};let k=image_key(&self.snapshot,&k,&request);self.request(k.clone(),request);
        let p=self.cache.get(&k).cloned().unwrap_or_default();let body=if kind=="head"{p.head.clone()}else{p.body.clone()};
        Entry{key:id.into(),label:s(v,"name").into(),detail:if kind=="preset"{if flag(v,"builtin"){"系统预设".into()}else{"自定义预设".into()}}else{s(v,"id").into()},selected:id==selected,body_ready:body.size().width>0,head_ready:p.head.size().width>0,body,head:p.head,..Default::default()}
    }
    fn part_rows(&self,ui:&Manager,kind:&str)->Vec<Value>{rows(&self.snapshot,kind).into_iter().filter(|v|ui.get_cross()||s(v,"character")==self.character).collect()}
    fn actor_value(&self)->Option<Value>{rows(&self.snapshot,"actors").into_iter().find(|v|key(v)==self.actor)}
    fn resource_value(&self)->Option<Value>{scene_tree::separated_resources(&rows(&self.snapshot,"scene_resources")).into_iter().find(|v|key(v)==self.resource)}
    fn position(&self,ui:&Manager,a:&Value){let p=&a["position"];let x=p[0].as_f64().unwrap_or(0.0);let y=p[1].as_f64().unwrap_or(0.0);let z=p[2].as_f64().unwrap_or(0.0);ui.set_x_value(format!("{x:.2}").into());ui.set_y_value(format!("{y:.2}").into());ui.set_z_value(format!("{z:.2}").into());ui.set_x_pos(x as f32);ui.set_y_pos(y as f32);ui.set_z_pos(z as f32);}
    fn render(&mut self,ui:&Manager){
        let admitted=self.connected&&flag(&self.snapshot,"admitted");let ready=admitted&&!flag(&self.snapshot,"busy")&&!flag(&self.snapshot,"playing")&&!flag(&self.snapshot,"faulted");
        ui.set_live_pause_available(flag(&self.snapshot,"live_pause_available"));ui.set_live_paused(flag(&self.snapshot,"live_paused"));ui.set_live_pause_owned(flag(&self.snapshot,"live_pause_owned"));ui.set_in_live(flag(&self.snapshot,"in_live"));ui.set_live_pause_status(s(&self.snapshot,"live_pause_status").into());
        ui.set_admitted(admitted);ui.set_ready(ready);ui.set_writable(flag(&self.snapshot,"writable"));ui.set_creation(self.connected&&flag(&self.snapshot,"creation_enabled"));ui.set_resource_enabled(flag(&self.snapshot,"resource_enabled"));ui.set_motion_present(flag(&self.snapshot,"motion_conflict")||flag(&self.snapshot,"motion_handoff_available")||flag(&self.snapshot,"motion_handoff_requested"));ui.set_resource_pending(flag(&self.snapshot,"resource_enable_pending"));ui.set_camera_active(flag(&self.snapshot,"free_camera_active"));
        let issue=flag(&self.snapshot,"faulted")||flag(&self.snapshot,"busy")||!flag(&self.snapshot,"runtime")||s(&self.snapshot,"prelive_catalog_status").contains("失败")||self.message.contains("未被接受")||self.message.contains("尚未完成")||self.catalog_scan;
        ui.set_connection_state(if !self.connected{0}else if issue{1}else{2});
        let catalog_failed=s(&self.snapshot,"prelive_catalog_status").contains("失败");ui.set_catalog_failed(self.connected&&catalog_failed);
        ui.set_connection_label(if !self.connected{self.connection_detail.clone().into()}else if catalog_failed{"已连接 · 目录异常".into()}else if issue{"已连接 · 等待就绪".into()}else{"游戏已连接".into()});
        let can_read_game=self.connected&&flag(&self.snapshot,"runtime")&&!flag(&self.snapshot,"busy");
        ui.set_can_scan_catalog(if self.connected {can_read_game} else {self.tx.is_some()&&self.game_configured&&!self.catalog_scan});
        ui.set_can_scan_resources(can_read_game);
        ui.set_scene_scan_status(s(&self.snapshot,"resource_status").into());
        ui.set_status(if !self.connected{self.connection_detail.clone().into()}else if flag(&self.snapshot,"offline_catalog"){if flag(&self.snapshot,"offline_provisional"){"正在浏览游戏安装目录中的人物图标".into()}else{"正在浏览本地人物目录；场景操作需进入拍摄或 Live".into()}}else{s(&self.snapshot,"status").into()});ui.set_notice(if self.message.is_empty(){["prelive_catalog_status","resource_catalog_notice","notice","resource_status","store_notice"].iter().map(|k|if *k=="prelive_catalog_status"{catalog_notice(s(&self.snapshot,k))}else{s(&self.snapshot,k).to_owned()}).filter(|x|!x.is_empty()).collect::<Vec<_>>().join(" · ").into()}else{self.message.clone().into()});
        if let Some((wanted,since))=&self.save {if rows(&self.snapshot,"presets").iter().any(|v|["id","name","character","body","head","bodyAsset","faceAsset","hairAsset"].iter().all(|k|s(v,k)==s(wanted,k))){self.preset=s(wanted,"id").into();ui.set_page(0);self.message="预设已保存。".into();self.save=None;}else if since.elapsed()>Duration::from_secs(8){self.save=None;self.message="未确认保存成功，请检查存储状态后重试。".into();}}
        let chars=rows(&self.snapshot,"characters");if !chars.iter().any(|v|s(v,"id")==self.character){self.character=chars.first().map(|v|s(v,"id").to_owned()).unwrap_or_default();}
        ui.set_character_count(chars.len().min(i32::MAX as usize) as i32);
        ui.set_character_name(chars.iter().find(|v| s(v,"id")==self.character).map(|v| s(v,"name")).unwrap_or_default().into());
        ui.set_scene_name(s(&self.snapshot,"active_scene_name").into());
        let counts=people_catalog::counts(&self.snapshot);let query=ui.get_search().to_lowercase();let presets:Vec<_>=rows(&self.snapshot,"presets").into_iter().filter(|v|s(v,"character")==self.character).collect();
        ui.set_characters(model(chars.iter().filter(|v|s(v,"name").to_lowercase().contains(&query)||s(v,"id").contains(&query)).map(|v|Entry{key:s(v,"id").into(),label:s(v,"name").into(),detail:{let empty=people_catalog::Counts::default();let c=counts.get(s(v,"id")).unwrap_or(&empty);format!("身体 {} · 头部组合 {}\n脸部 {} · 发型 {}",c.bodies,c.heads,c.faces,c.hairs).into()},body_count:counts.get(s(v,"id")).map_or(0,|c|c.bodies.min(i32::MAX as usize) as i32),head_count:counts.get(s(v,"id")).map_or(0,|c|c.heads.min(i32::MAX as usize) as i32),face_count:counts.get(s(v,"id")).map_or(0,|c|c.faces.min(i32::MAX as usize) as i32),hair_count:counts.get(s(v,"id")).map_or(0,|c|c.hairs.min(i32::MAX as usize) as i32),selected:s(v,"id")==self.character,..Default::default()}).collect()));
        if !presets.iter().any(|v|s(v,"id")==self.preset){self.preset=presets.first().map(|v|s(v,"id").to_owned()).unwrap_or_default();}
        ui.set_body_placeholder("暂无图片".into());ui.set_head_placeholder("暂无图片".into());let page=ui.get_page();let current=presets.iter().find(|v|s(v,"id")==self.preset).cloned();let mut pictures=Pictures::default();
        ui.set_can_undo(self.undo.as_ref().is_some_and(|p|!rows(&self.snapshot,"presets").iter().any(|v|s(v,"id")==s(p,"id")))&&flag(&self.snapshot,"writable"));
        ui.set_selection("".into());ui.set_detail("".into());ui.set_can_delete(false);ui.set_can_hide(false);ui.set_actor_owned(false);ui.set_pose_supported(false);
        if page==0 {
            let xs=self.paged(&presets,0);let selected=self.preset.clone();ui.set_presets(model(xs.iter().map(|v|self.entry(v,"preset",&selected)).collect()));
            ui.set_preset_pager(self.pager(presets.len(),0).into());
            if let Some(p)=current {let req=json!({"version":1,"action":"load_thumbnail","preset_id":self.preset});let k=image_key(&self.snapshot,&format!("preset:{}",self.preset),&req);self.request(k.clone(),req);pictures=self.cache.get(&k).cloned().unwrap_or_default();ui.set_selection(s(&p,"name").into());let detail=if !s(&p,"bodyAsset").is_empty(){format!("身体：{}\n脸部：{}\n头发：{}",s(&p,"bodyAsset"),s(&p,"faceAsset"),s(&p,"hairAsset"))}else if !s(&p,"factory").is_empty(){format!("人物模型：{}",s(&p,"name"))}else{format!("身体：{}\n头部：{}",s(&p,"body"),s(&p,"head"))};ui.set_detail(detail.into());ui.set_can_delete(!flag(&p,"builtin")&&flag(&self.snapshot,"writable"));}
        }
        let actors=rows(&self.snapshot,"actors");ui.set_references(strings(actors.iter().map(|v|format!("与 {} 同位置",s(v,"name"))).collect()));let ri=actors.iter().position(|a|a["instance"].as_i64()==Some(self.reference)).or(if actors.is_empty(){None}else{Some(0)});self.reference=ri.and_then(|i|actors[i]["instance"].as_i64()).unwrap_or(0);ui.set_reference_index(ri.map(|i|i as i32).unwrap_or(-1));if !self.stage_initialized&&self.snapshot["stage_choice"].is_number(){ui.set_stage_index(self.snapshot["stage_choice"].as_i64().unwrap_or(0) as i32);self.stage_initialized=true;}
        if page==4 {
            if ui.get_raw_mode(){self.raw_choices(ui);}
            let bodies=self.part_rows(ui,"bodies");let heads=self.part_rows(ui,"heads");
            if !ui.get_raw_mode(){let bs=self.paged(&bodies,1);let hs=self.paged(&heads,2);let b=self.body.clone();let h=self.head.clone();ui.set_bodies(model(bs.iter().map(|v|self.entry(v,"body",&b)).collect()));ui.set_heads(model(hs.iter().map(|v|self.entry(v,"head",&h)).collect()));}
            ui.set_body_pager(self.pager(bodies.len(),1).into());ui.set_head_pager(self.pager(heads.len(),2).into());
            let valid=if ui.get_raw_mode(){let indices=[ui.get_body_index(),ui.get_face_index(),ui.get_hair_index()];let mut values=Vec::new();for i in 0..3 {values.push(usize::try_from(indices[i]).ok().and_then(|x|self.raw[i].get(x)).cloned().unwrap_or_default());}ui.set_detail(format!("身体：{}\n\n脸部：{}\n\n头发：{}",values[0],values[1],values[2]).into());
                for kind in ["body","head"] {
                    let id=people_catalog::raw_part(&self.snapshot,kind,&values[0],&values[1],&values[2]);
                    let placeholder=if (kind=="body"&&values[0].is_empty())||(kind=="head"&&(values[1].is_empty()||values[2].is_empty())) {"请选择部件"} else if let Some(id)=id {
                        let req=json!({"version":1,"action":"load_part_thumbnail","kind":kind,"part_id":id});let k=image_key(&self.snapshot,&format!("part:{kind}:{id}"),&req);self.request(k.clone(),req);
                        if let Some(p)=self.cache.get(&k){if kind=="body"{pictures.body=p.body.clone();}else{pictures.head=p.head.clone();}}
                        if self.requested.contains(&k){"加载中…"}else{"暂无对应图片"}
                    }else{"暂无对应图片"};
                    if kind=="body"{ui.set_body_placeholder(placeholder.into());}else{ui.set_head_placeholder(placeholder.into());}
                }
                values.iter().all(|x|!x.is_empty())}else{
                let b=bodies.iter().find(|v|s(v,"id")==self.body);let h=heads.iter().find(|v|s(v,"id")==self.head);let bk=image_key(&self.snapshot,&format!("part:body:{}",self.body),&json!({"action":"load_part_thumbnail","kind":"body","part_id":self.body}));let hk=image_key(&self.snapshot,&format!("part:head:{}",self.head),&json!({"action":"load_part_thumbnail","kind":"head","part_id":self.head}));pictures.body=self.cache.get(&bk).map(|p|p.body.clone()).unwrap_or_default();pictures.head=self.cache.get(&hk).map(|p|p.head.clone()).unwrap_or_default();ui.set_detail(format!("身体：{}\n\n头部：{}",b.map(|v|s(v,"name")).unwrap_or("尚未选择"),h.map(|v|s(v,"name")).unwrap_or("尚未选择")).into());b.is_some()&&h.is_some()};ui.set_save_ready(self.tx.is_some()&&flag(&self.snapshot,"writable")&&valid&&self.save.is_none());
        }
        if (1..=2).contains(&page){let query=ui.get_resource_search().to_lowercase();
            let resources=rows(&self.snapshot,"scene_resources");
            ui.set_actor_count(actors.len().min(i32::MAX as usize) as i32);
            ui.set_actors(model(actors.iter().filter(|v|s(v,"name").to_lowercase().contains(&query)).map(|v| {
                let name=s(v,"name");
                let visibility=actor_visibility(&resources,v);
                Entry {
                    key:key(v).into(),
                    label:name.into(),
                    avatar:name.chars().last().unwrap_or('•').to_string().into(),
                    detail:if flag(v,"owned"){"本插件新增".into()}else{"场景原有人物".into()},
                    selected:key(v)==self.actor,
                    hidden:visibility.is_some_and(|resource|flag(resource,"hidden")),
                    hidden_known:visibility.is_some(),
                    ..Default::default()
                }
            }).collect()));
            if page == 2 {
                let scene_name=s(&self.snapshot,"active_scene_name");
                let filters=[ui.get_fixed_search(),ui.get_moving_search()];
                let filters=[filters[0].as_str(),filters[1].as_str()];
                let motion=&self.snapshot["motion_assets"];
                let workspace=if motion.is_object() {
                    ui.set_workspace_feedback(if self.message.is_empty() {"Motion 同源候选 · 时间轴道具只读".into()}else{self.message.clone().into()});
                    motion_assets::project(motion,&resources,scene_name,&self.resource,&self.collapsed,filters,&self.notes)
                }else{
                    ui.set_workspace_feedback(if self.message.is_empty()&&flag(&self.snapshot,"in_live")&&ui.get_motion_present(){
                        "当前显示 Actors 本地候选。打开 Loader 的 GakuActors 面板可同步 Motion。".into()
                    }else{self.message.clone().into()});
                    scene_workspace::project(&resources,scene_name,&self.resource,&self.collapsed,filters,&self.notes)
                };
                ui.set_fixed_count(format!("{} 个节点",workspace.fixed_count).into());
                ui.set_moving_count(format!("{} 组道具",workspace.moving_count).into());
                ui.set_fixed_assets(model(workspace.fixed));
                ui.set_moving_assets(model(workspace.moving));
                ui.set_asset_name(workspace.detail.name.into());
                ui.set_asset_tag(workspace.detail.tags.into());
                ui.set_asset_state(workspace.detail.state.into());
                ui.set_asset_path(workspace.detail.path.into());
                ui.set_note_writable(!self.notes_error&&self.notes_path.is_some()&&workspace.detail.note_writable);
                self.tree_rows=workspace.rows;
            }
            if page == 1 {ui.set_stage_markers(model(stage_markers(&actors,&self.actor)));}
            if page==1 {if let Some(a)=self.actor_value(){ui.set_selection(s(&a,"name").into());ui.set_actor_owned(flag(&a,"owned"));ui.set_pose_supported(flag(&a,"pose_supported"));ui.set_actor_body(s(&a,"bodyAsset").into());ui.set_actor_face(s(&a,"faceAsset").into());ui.set_actor_hair(s(&a,"hairAsset").into());let visibility=actor_visibility(&resources,&a);ui.set_actor_hidden(visibility.is_some_and(|resource|flag(resource,"hidden")));ui.set_can_hide(ready&&flag(&self.snapshot,"resource_enabled")&&visibility.is_some_and(|resource|!flag(resource,"locked")));if self.pose_dirty {let p=&a["position"];let target=[ui.get_x_value().parse::<f32>().unwrap_or(0.0),ui.get_y_value().parse::<f32>().unwrap_or(0.0),ui.get_z_value().parse::<f32>().unwrap_or(0.0)];if (0..3).all(|i|p[i].as_f64().is_some_and(|v|(v-target[i] as f64).abs()<0.02)){self.pose_dirty=false;}}if !self.pose_dirty{self.position(ui,&a);}if let Some((id,rev))=self.reset_yaw {if self.snapshot["pose_reset_yaw_actor"].as_i64()==Some(id)&&self.snapshot["pose_reset_yaw_revision"].as_u64().unwrap_or(0)>rev {if let Some(y)=self.snapshot["pose_reset_yaw"].as_f64(){let y=signed_yaw(y);ui.set_yaw_pos(y as f32);ui.set_yaw_value(format!("{y:.1}").into());self.yaw_dirty=true;}self.reset_yaw=None;}}if let Some(y)=a["yaw"].as_f64(){if self.yaw_dirty&&signed_yaw(y-ui.get_yaw_pos() as f64).abs()<0.25{self.yaw_dirty=false;}if !self.yaw_dirty{let y=signed_yaw(y);ui.set_yaw_pos(y as f32);ui.set_yaw_value(format!("{y:.1}").into());}}if let Some((id,rev))=self.reset{if self.snapshot["pose_reset_actor"].as_i64()==Some(id)&&self.snapshot["pose_reset_revision"].as_u64().unwrap_or(0)>rev {self.position(ui,&a);self.pose_dirty=false;self.reset=None;}}}}

        }
        ui.set_selected_body_ready(pictures.body.size().width>0);ui.set_selected_head_ready(pictures.head.size().width>0);ui.set_selected_body(pictures.body);ui.set_selected_head(pictures.head);
    }
    fn paged(&mut self,xs:&[Value],i:usize)->Vec<Value>{self.pages[i]=self.pages[i].min(xs.len().saturating_sub(1)/PAGE);xs.iter().skip(self.pages[i]*PAGE).take(PAGE).cloned().collect()}
    fn pager(&self,n:usize,i:usize)->String{format!("{} / {}",self.pages[i]+1,n.saturating_sub(1)/PAGE+1)}
    fn raw_choices(&mut self,ui:&Manager){
        // Keep selection by resource identity when asynchronous scans append
        // new entries; a changed sort order must not select a different model.
        let indices=[ui.get_body_index(),ui.get_face_index(),ui.get_hair_index()];
        let previous:Vec<Option<String>>=(0..3).map(|i|usize::try_from(indices[i]).ok().and_then(|n|self.raw[i].get(n)).cloned()).collect();
        let resources=rows(&self.snapshot,"resources");let bodies=rows(&self.snapshot,"bodies");let heads=rows(&self.snapshot,"heads");let models=rows(&self.snapshot,"models");
        for (i,kind) in ["body","face","hair"].into_iter().enumerate(){
            let mut values=Vec::<String>::new();
            let mut add=|row:&Value,field:&str|{let asset=s(row,field);if (ui.get_cross()||s(row,"character")==self.character)&&asset.starts_with("mdl_chr_")&&asset.ends_with(&format!("_{kind}"))&&asset.bytes().all(|b|b.is_ascii_lowercase()||b.is_ascii_digit()||b==b'_'||b==b'-'){values.push(asset.to_owned());}};
            for row in &resources{if s(row,"kind")==kind{add(row,"resource");}}
            match kind {"body"=>for row in &bodies{add(row,"resource");},"face"=>for row in &heads{add(row,"face_resource");},"hair"=>for row in &heads{add(row,"resource");},_=>{}}
            for row in &models{add(row,kind);}
            values.sort();values.dedup();
            if values!=self.raw[i]{
                let selected=previous[i].as_ref().and_then(|v|values.iter().position(|x|x==v)).map(|n|n as i32).unwrap_or(-1);
                self.raw[i]=values;
                match i{0=>{ui.set_raw_bodies(strings(self.raw[i].clone()));ui.set_body_index(selected);},1=>{ui.set_raw_faces(strings(self.raw[i].clone()));ui.set_face_index(selected);},_=>{ui.set_raw_hairs(strings(self.raw[i].clone()));ui.set_hair_index(selected);}}
            }
        }
    }
    fn act(&mut self,ui:&Manager,action:&str,arg:&str){
        self.message.clear();match action {
            "nav"=>{let page=arg.parse().unwrap_or(0);let page=if page==3{2}else{page};ui.set_page(page);self.pose_dirty=false;},
            "character"=>{self.character=arg.into();self.preset.clear();self.pages=[0;3];},"preset"=>self.preset=arg.into(),
            "preset-page"|"body-page"|"head-page"=>{let i=if action=="preset-page"{0}else if action=="body-page"{1}else{2};self.pages[i]=if arg=="-1"{self.pages[i].saturating_sub(1)}else{self.pages[i]+1};},
            "body"=>self.body=arg.into(),"head"=>self.head=arg.into(),"actor"=>{self.actor=arg.into();self.pose_dirty=false;self.yaw_dirty=false;ui.set_yaw_value("未读取".into());ui.set_yaw_pos(0.0);},"resource"=>{self.resource=arg.into();let note=self.tree_rows.iter().find(|r|r.key==self.resource).map(|r|r.note.clone()).unwrap_or_default();ui.set_resource_note(note.into());},
            "tree-toggle"=>{let group=matches!(arg,"fixed-structure"|"fixed-background"|"fixed-lighting");let collapse=if group{Some(arg.to_owned())}else{self.tree_rows.iter().find(|r|r.key==arg).map(|r|r.collapse_key.clone())};if let Some(k)=collapse{if !self.collapsed.remove(&k){self.collapsed.insert(k);}}},
            "save-note"=>{if !self.notes_error {if let (Some(path),Some(note_key),Some(tx))=(self.notes_path.clone(),self.tree_rows.iter().find(|r|r.key==self.resource).and_then(|r|r.note_key.clone()),self.tx.as_ref()){let note=ui.get_resource_note().trim().to_owned();if !scene_tree::valid_note(&note){self.message="备注过长或包含控制字符。".into();}else{let mut updated=self.notes.clone();if note.is_empty(){updated.remove(&note_key);}else{updated.insert(note_key,note);}if tx.try_send(Work::SaveNotes(path,updated.clone())).is_ok(){self.notes=updated;self.message="正在保存备注…".into();}else{self.message="操作队列忙，请稍后重试。".into();}}}}},
            "new"|"edit"=>{self.pages=[0;3];self.edit_id.clear();self.body.clear();self.head.clear();ui.set_preset_name("".into());ui.set_raw_mode(false);ui.set_cross(false);ui.set_body_index(-1);ui.set_face_index(-1);ui.set_hair_index(-1);self.raw_choices(ui);
                if action=="edit" {if let Some(p)=rows(&self.snapshot,"presets").into_iter().find(|v|s(v,"id")==self.preset){if !flag(&p,"builtin"){self.edit_id=s(&p,"id").into();}ui.set_preset_name(s(&p,"name").into());self.body=s(&p,"body").into();self.head=s(&p,"head").into();if !s(&p,"bodyAsset").is_empty(){ui.set_raw_mode(true);for (i,k) in ["bodyAsset","faceAsset","hairAsset"].iter().enumerate(){let selected=self.raw[i].iter().position(|v|v==s(&p,k)).map(|v|v as i32).unwrap_or(-1);match i{0=>ui.set_body_index(selected),1=>ui.set_face_index(selected),_=>ui.set_hair_index(selected)}}}}}ui.set_page(4);},
            "reconnect"=>{if let Some(tx)=&self.tx{if tx.try_send(Work::Reconnect).is_ok(){self.message="正在重新连接游戏插件…".into();}else{self.message="操作队列忙，请稍后重试。".into();}}},
            "scan"=>{
                if self.connected {
                    self.send(json!({"action":"read_pre_live_catalog"}));
                } else if !self.catalog_scan {
                    if self.tx.as_ref().is_some_and(|tx|tx.try_send(Work::ReadCatalog).is_ok()) {
                        self.catalog_scan=true;
                        self.message="正在读取本地人物和服装目录…".into();
                    } else { self.message="操作队列忙，请稍后重试。".into(); }
                }
            },"scan-scene"=>self.send(json!({"action":"scan_scene"})),"scan-resources"=>self.send(json!({"action":"scan_resources"})),
            "creation"=>self.send(json!({"action":"enable_creation","enabled":!flag(&self.snapshot,"creation_enabled")})),
            "resources"=>self.send(json!({"action":"enable_resources","enabled":!flag(&self.snapshot,"resource_enabled")})),
            "camera"=>self.send(json!({"action":"free_camera","enabled":!flag(&self.snapshot,"free_camera_active")})),
            "live-pause"=>self.send(json!({"action":"live_pause","enabled":!flag(&self.snapshot,"live_pause_owned")})),
            "reference"=>{self.reference=rows(&self.snapshot,"actors").get(ui.get_reference_index().max(0) as usize).and_then(|a|a["instance"].as_i64()).unwrap_or(0);},
            "zoom"=>{let i=if arg=="head"{ui.get_selected_head()}else{ui.get_selected_body()};if i.size().width>0{ui.set_zoom_image(i);ui.set_zoom_title(if arg=="head"{"头部模型系统预览".into()}else{"身体服装系统预览".into()});ui.set_zoom_open(true);}},
            "spawn"=>{if let Some(a)=rows(&self.snapshot,"actors").get(ui.get_reference_index().max(0) as usize){self.send(json!({"action":"spawn","preset_id":self.preset,"instance":a["instance"]}));}},
            "slider-move"=>{self.pose_dirty=true;let mut values=[ui.get_x_value().parse::<f32>().unwrap_or(0.0),ui.get_y_value().parse::<f32>().unwrap_or(0.0),ui.get_z_value().parse::<f32>().unwrap_or(0.0)];match arg{"x"=>values[0]=ui.get_x_pos(),"y"=>values[1]=ui.get_y_pos(),"z"=>values[2]=ui.get_z_pos(),_=>{}}ui.set_x_value(format!("{:.2}",values[0]).into());ui.set_y_value(format!("{:.2}",values[1]).into());ui.set_z_value(format!("{:.2}",values[2]).into());if let Some(a)=self.actor_value(){self.send(json!({"action":"move","instance":a["instance"],"position":values}));}},
            "slider-yaw"=>{self.yaw_dirty=true;let value=ui.get_yaw_pos();ui.set_yaw_value(format!("{value:.1}").into());if let Some(a)=self.actor_value(){self.send(json!({"action":"set_yaw","instance":a["instance"],"degrees":f64::from(value).rem_euclid(360.0)}));}},
            "slider-yaw-end"=>{},
            "reset-yaw"=>{if let Some(a)=self.actor_value(){self.yaw_dirty=true;self.reset_yaw=Some((a["instance"].as_i64().unwrap_or(0),self.snapshot["pose_reset_yaw_revision"].as_u64().unwrap_or(0)));self.send(json!({"action":"reset_yaw","instance":a["instance"]}));}},
            "move"=>{self.pose_dirty=true;if let Some(a)=self.actor_value(){let values=[ui.get_x_value(),ui.get_y_value(),ui.get_z_value()];let p:Option<Vec<f64>>=values.iter().map(|x|x.parse::<f64>().ok().filter(|n|n.is_finite()&&n.abs()<=10000.0)).collect();if let Some(p)=p{self.send(json!({"action":"move","instance":a["instance"],"position":p}));}}},
            "reset"=>{if let Some(a)=self.actor_value(){self.reset=Some((a["instance"].as_i64().unwrap_or(0),self.snapshot["pose_reset_revision"].as_u64().unwrap_or(0)));self.send(json!({"action":"reset_position","instance":a["instance"]}));}},
            "turn"|"yaw"=>{if let Some(a)=self.actor_value(){let value=if action=="yaw"{ui.get_yaw_value().to_string()}else{arg.into()};if let Ok(n)=value.parse::<f64>() {if n.is_finite()&&n.abs()<=360.0 {self.send(json!({"action":if action=="yaw"{"set_yaw"}else{"turn"},"instance":a["instance"],"degrees":n}));}else{self.message="请输入有效角度（0–360）。".into();}}}},
            "hide-row-on"|"hide-row-off"=>{if let Some(r)=self.tree_rows.iter().find(|r|r.key==arg).map(|r|r.value.clone()){self.set_hidden(r,action=="hide-row-on");}return;},
            "hide"=>{if let Some(r)=self.resource_value(){self.hide(r);}},
            "hide-actor"=>{if let Some(a)=self.actor_value(){if let Some(r)=rows(&self.snapshot,"scene_resources").into_iter().find(|r|r["kind"]==0&&r["owner"]==a["instance"]&&!flag(r,"locked")){self.hide(r);}}},
            "stage"=>self.send(json!({"action":"select_stage","choice":ui.get_stage_index()})),
            "choose-color"=>{let values=[ui.get_red_value(),ui.get_green_value(),ui.get_blue_value()];let current=values.map(|v|(v.parse::<f32>().unwrap_or(0.0).clamp(0.0,1.0)*255.0).round() as u8);if let Some(rgb)=color_dialog::choose(current){ui.set_red_value(format!("{:.4}",rgb[0] as f32/255.0).into());ui.set_green_value(format!("{:.4}",rgb[1] as f32/255.0).into());ui.set_blue_value(format!("{:.4}",rgb[2] as f32/255.0).into());ui.set_color_preview(slint::Color::from_rgb_u8(rgb[0],rgb[1],rgb[2]).into());}},
            "color-off"=>self.send(json!({"action":"camera_color","enabled":false,"color":[0,1,0,1]})),
            "color"=>{let rgb:Option<Vec<f64>>=[ui.get_red_value(),ui.get_green_value(),ui.get_blue_value()].iter().map(|x|x.parse::<f64>().ok().filter(|v|v.is_finite()&&(0.0..=1.0).contains(v))).collect();if let Some(mut rgb)=rgb{rgb.push(1.0);self.send(json!({"action":"camera_color","enabled":action=="color","color":rgb}));}else{self.message="RGB 请输入 0 到 1 的数值。".into();}},
            "delete"=>{if let Some(p)=rows(&self.snapshot,"presets").into_iter().find(|v|s(v,"id")==self.preset&&!flag(v,"builtin")){self.undo=Some(p.clone());self.confirm=Some(json!({"action":"delete_preset","preset_id":self.preset}));ui.set_confirmation(format!("删除预设“{}”？场景中已加入的人物不会被移除。",s(&p,"name")).into());}},
            "remove"=>{if let Some(a)=self.actor_value().filter(|a|flag(a,"owned")){self.confirm=Some(json!({"action":"remove","instance":a["instance"]}));ui.set_confirmation(format!("从场景移除“{}”？",s(&a,"name")).into());}},
            "cancel"=>{self.confirm=None;ui.set_confirmation("".into());},"confirm"=>{if let Some(v)=self.confirm.take(){self.send(v);}ui.set_confirmation("".into());},
            "retry"=>{self.cache.clear();self.lru.clear();self.requested.clear();},
            "save"=>self.save_preset(ui),
            "undo"=>{if let Some(mut p)=self.undo.clone(){p["builtin"]=false.into();self.send(json!({"action":"save_preset","preset":p}));self.save=Some((p,Instant::now()));}},
            "music"=>{if let Ok(exe)=std::env::current_exe(){use std::os::windows::process::CommandExt;if std::process::Command::new(exe).arg("--music-only").creation_flags(0x08000000).spawn().is_err(){self.message="乐库窗口无法启动。".into();}}},_=>{}
        }self.render(ui);
    }
    fn hide(&mut self,r:Value){let hidden=!flag(&r,"hidden");self.set_hidden(r,hidden);}
    fn set_hidden(&mut self,r:Value,hidden:bool){if let Some(request)=visibility_request(&r,hidden){self.send(request);}}
    fn save_preset(&mut self,ui:&Manager){if self.save.is_some()||!ui.get_save_ready(){return;}let id=if self.edit_id.is_empty(){format!("user-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos())}else{self.edit_id.clone()};let mut name=ui.get_preset_name().trim().to_owned();if name.is_empty(){let label=rows(&self.snapshot,"characters").into_iter().find(|v|s(v,"id")==self.character).map(|v|s(&v,"name").to_owned()).unwrap_or_else(||self.character.clone());name=format!("{} · 自定义组合",label);}if name.chars().any(char::is_control)||name.len()>160{self.message="预设名称过长或含控制字符。".into();return;}
        let mut p=json!({"id":id,"name":name,"character":self.character,"body":"","head":"","factory":"","bodyAsset":"","faceAsset":"","hairAsset":""});
        if ui.get_raw_mode(){for (i,k) in ["bodyAsset","faceAsset","hairAsset"].iter().enumerate(){let index=[ui.get_body_index(),ui.get_face_index(),ui.get_hair_index()][i];let Some(value)=usize::try_from(index).ok().and_then(|x|self.raw[i].get(x))else{return;};p[*k]=value.clone().into();}}
        else{p["body"]=self.body.clone().into();p["head"]=self.head.clone().into();}
        self.send(json!({"action":"save_preset","preset":p}));self.save=Some((p,Instant::now()));self.message="正在保存预设…".into();
    }
}

fn run()->Result<(),Box<dyn std::error::Error>>{
    // Only the software renderer is compiled and explicitly selected. Failure
    // is reported instead of silently falling back to a GPU backend.
    slint::BackendSelector::new().backend_name("winit".into()).renderer_name("software".into()).select()?;
    music_window::platform::configure_font_fallback();
    let ui=Manager::new()?;
    ui.set_desktop_version(env!("CARGO_PKG_VERSION").into());
    // Check the offline resource library in a separate process; network failure
    // and exporter work must never block the desktop or the game pipe.
    std::thread::spawn(|| {
        use std::os::windows::process::CommandExt;
        if let Some(script)=std::env::current_exe().ok().and_then(|p|p.parent().map(|p|p.join("Open-ResourceLibrary.ps1"))).filter(|p|p.is_file()) {
            let _=std::process::Command::new("powershell.exe").args(["-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File"]).arg(script).arg("-NoBrowser").creation_flags(0x08000000).output();
        }
    });
    let (tx,rx)=mpsc::sync_channel(64);let (events_tx,events_rx)=mpsc::sync_channel(8);
    let client=legacy::pipe::Client::from_args().ok();std::thread::spawn(move||worker(client,rx,events_tx));
    let notes_path=legacy::music::game_root().ok().map(|p|p.join("GakuLoader/Configs/gakuactors/scene-notes.json"));
    let (notes,notes_error)=match notes_path.as_ref().map(|p|scene_tree::load_notes(p)){Some(Ok(v))=>(v,false),Some(Err(_))=>(BTreeMap::new(),true),None=>(BTreeMap::new(),true)};
    let state=Rc::new(RefCell::new(State{snapshot:json!({"status":"等待游戏和 GakuActors 插件"}),tx:Some(tx),connected:false,connection_detail:"正在检测游戏".into(),character:String::new(),preset:String::new(),actor:String::new(),resource:String::new(),body:String::new(),head:String::new(),edit_id:String::new(),raw:Default::default(),cache:HashMap::new(),lru:VecDeque::new(),requested:HashSet::new(),tree_rows:Vec::new(),collapsed:HashSet::new(),notes,notes_path:notes_path.clone(),notes_error,yaw_dirty:false,pages:[0;3],catalog_scan:false,game_configured:notes_path.is_some(),reference:0,stage_initialized:false,confirm:None,undo:None,save:None,message:String::new(),pose_dirty:false,reset:None,reset_yaw:None}));
    let library=Rc::new(RefCell::new(resource_library::Library::new()));
    let weak=ui.as_weak();let lib=library.clone();ui.on_library_action(move|action,arg|{if let Some(ui)=weak.upgrade(){lib.borrow_mut().action(&ui,&action,&arg);}});
    let weak=ui.as_weak();let st=state.clone();ui.on_action(move|a,b|{if let Some(ui)=weak.upgrade(){st.borrow_mut().act(&ui,&a,&b);}});
    let weak=ui.as_weak();let st=state.clone();ui.on_changed(move|kind|{if let Some(ui)=weak.upgrade(){let mut st=st.borrow_mut();if kind=="cross"{st.body.clear();st.head.clear();st.pages=[0;3];st.raw_choices(&ui);}st.render(&ui);}});
    state.borrow_mut().render(&ui);
    ui.show()?;
    let mut startup_size_ready=apply_initial_window_size(ui.window());
    {let weak=ui.as_weak();let state=state.clone();ui.on_drag_position(move |axis,delta|{let Some(ui)=weak.upgrade()else{return;};if !delta.is_finite()||delta==0.0{return;}let mut st=state.borrow_mut();if axis.as_str()=="朝向" {let value=signed_yaw(ui.get_yaw_pos() as f64+delta as f64*0.1);ui.set_yaw_pos(value as f32);ui.set_yaw_value(format!("{value:.1}").into());st.yaw_dirty=true;if let Some(a)=st.actor_value(){st.send(json!({"action":"set_yaw","instance":a["instance"],"degrees":value.rem_euclid(360.0)}));}return;}let mut values=[ui.get_x_value().parse::<f64>().unwrap_or(0.0),ui.get_y_value().parse::<f64>().unwrap_or(0.0),ui.get_z_value().parse::<f64>().unwrap_or(0.0)];let i=match axis.as_str(){"X"=>0,"Y"=>1,"Z"=>2,_=>return};let current=match i{0=>ui.get_x_pos(),1=>ui.get_y_pos(),_=>ui.get_z_pos()};values[i]=(current as f64+delta as f64*0.01).clamp(-10000.0,10000.0);match i{0=>{ui.set_x_pos(values[i] as f32);ui.set_x_value(format!("{:.2}",values[i]).into());},1=>{ui.set_y_pos(values[i] as f32);ui.set_y_value(format!("{:.2}",values[i]).into());},_=>{ui.set_z_pos(values[i] as f32);ui.set_z_value(format!("{:.2}",values[i]).into());}};st.pose_dirty=true;if let Some(a)=st.actor_value(){st.send(json!({"action":"move","instance":a["instance"],"position":values}));}});}
    let timer=slint::Timer::default();let weak=ui.as_weak();let lib=library.clone();let mut title_bar_ready=false;timer.start(slint::TimerMode::Repeated,Duration::from_millis(160),move||{let Some(ui)=weak.upgrade()else{return;};lib.borrow_mut().poll(&ui);if !startup_size_ready{startup_size_ready=apply_initial_window_size(ui.window());}if !title_bar_ready{title_bar_ready=music_window::platform::sync_title_bar(ui.window(),2);}let mut st=state.borrow_mut();let mut dirty=false;for e in events_rx.try_iter().take(16){dirty=true;match e {Event::IconsUpdated=>{let empty:Vec<_>=st.cache.iter().filter(|(k,p)|if k.starts_with("part:body:"){p.body.size().width==0}else if k.starts_with("part:head:"){p.head.size().width==0}else{p.body.size().width==0||p.head.size().width==0}).map(|(k,_)|k.clone()).collect();for k in empty{st.cache.remove(&k);st.lru.retain(|x|x!=&k);}},Event::Snapshot(v)=>{if flag(&v,"runtime")&&st.message=="游戏连接已断开，正在等待重新连接。"{st.message.clear();}st.snapshot=v;},Event::Picture(k,a,b)=>st.picture(k,a,b),Event::NotesSaved(result)=>{st.message=match result{Ok(())=>"备注已保存。".into(),Err(e)=>format!("备注保存失败：{e}")};},Event::CatalogRead(result)=>{st.catalog_scan=false;st.message=result.unwrap_or_else(|e|format!("读取本地人物目录失败：{e}"));},Event::PresetWriteFailed(error)=>{st.save=None;st.message=error;},Event::Probe(notice)=>{if st.message=="游戏连接已断开，正在等待重新连接。"||st.message.ends_with("；窗口会继续自动检测。") {st.message=format!("{notice}；窗口会继续自动检测。");}st.connection_detail=notice;},Event::RetryFailed(notice)=>{st.connection_detail=notice.clone();st.message=format!("{notice}；窗口会继续自动检测。");},Event::Connected=>{st.connected=true;st.connection_detail.clear();st.message.clear();},Event::Disconnected=>{st.connected=false;st.connection_detail="正在检测游戏".into();st.requested.clear();st.snapshot=json!({"status":"游戏连接已断开，等待重新连接","runtime":false});st.message="游戏连接已断开，正在等待重新连接。".into();}}}if dirty||st.save.is_some()||st.catalog_scan{st.render(&ui);}});
    music_window::platform::sync_title_bar(ui.window(),2);
    ui.run()?;Ok(())
}
fn main(){
    if std::env::args().any(|a|a=="--music-only"){if let Err(e)=music_window::run(){eprintln!("{e}");}return;}
    let legacy_mode=std::env::args().skip(1).any(|a|a.starts_with("--model-icon")||a.starts_with("--music-")||a=="--self-test"||a=="--ipc-self-test");
    if legacy_mode{legacy::main();return;}
    if let Err(e)=run(){
        let message:Vec<u16>=format!("场景资源管理窗口无法启动。\n请确认安装文件完整，随后重新打开窗口。\n\n{e}").encode_utf16().chain(Some(0)).collect();
        let title:Vec<u16>="GakuActors".encode_utf16().chain(Some(0)).collect();
        #[link(name="user32")] extern "system" {fn MessageBoxW(owner:*mut std::ffi::c_void,text:*const u16,title:*const u16,flags:u32)->i32;}
        unsafe{MessageBoxW(std::ptr::null_mut(),message.as_ptr(),title.as_ptr(),0x10);}
        std::process::exit(1);
    }
}


#[cfg(test)] mod image_cache_tests { use super::*; #[test] fn key_tracks_icon_and_preset_but_not_catalog_revision(){let mut s=json!({"bodies":[{"id":"x","thumbnail":"icon"}],"catalog_revision":1});let r=json!({"action":"load_part_thumbnail","kind":"body","part_id":"x"});let k=image_key(&s,"part:body:x",&r);s["catalog_revision"]=2.into();assert_eq!(k,image_key(&s,"part:body:x",&r));s["bodies"][0]["thumbnail"]="new".into();assert_ne!(k,image_key(&s,"part:body:x",&r));} }
