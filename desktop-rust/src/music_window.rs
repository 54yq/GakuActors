use crate::legacy::{music,preview};
use serde_json::Value;
use slint::{ComponentHandle,ModelRc,VecModel};
use std::{collections::HashSet,cell::RefCell,rc::Rc,sync::{mpsc,Arc,atomic::{AtomicBool,Ordering}},time::Duration,path::PathBuf};

#[path="music_ui.rs"] mod ui;
#[path="music_platform.rs"] pub(crate) mod platform;
use ui::{MusicWindow,SongRow,GroupRow};

enum Event {Scan(Result<Vec<Value>,String>), Job(String,Result<(),String>), BulkProgress(String), BulkComplete(String), Play(String,PathBuf,Result<(),String>)}
struct State {songs:Vec<Value>,selected:String,playing_name:String,checked:HashSet<String>,multi_mode:bool,busy:bool,player:preview::Player,tx:mpsc::Sender<Event>,alive:Arc<AtomicBool>}
fn field<'a>(v:&'a Value,k:&str)->&'a str {v[k].as_str().unwrap_or("")}
fn flag(v:&Value,k:&str)->bool {v[k].as_bool().unwrap_or(false)}
fn artist(v:&Value)->&str {let a=field(v,"performer");if a.is_empty(){"未知演唱"}else{a}}
fn song_title(v:&Value)->&str {let t=field(v,"title");if t.is_empty(){field(v,"name").trim_end_matches(".awb")}else{t}}
fn variant(v:&Value)->&str {let name=field(v,"name").trim_end_matches(".awb");if name.ends_with("-inst"){"伴奏版"}else if field(v,"kind")=="live"{name.rsplit('_').next().unwrap_or("Live")}else{"游戏版"}}
#[derive(Clone,Copy)] enum BulkAction {Download,Compile,RemoveWave,RemoveRaw}
impl BulkAction {
 fn parse(s:&str)->Option<Self>{match s{"bulk-download"=>Some(Self::Download),"bulk-compile"=>Some(Self::Compile),"bulk-wave"=>Some(Self::RemoveWave),"bulk-raw"=>Some(Self::RemoveRaw),_=>None}}
 fn eligible(self,s:&Value)->bool{match self{Self::Download=>!flag(s,"cached"),Self::Compile=>flag(s,"cached")&&!flag(s,"compiled"),Self::RemoveWave=>flag(s,"compiled"),Self::RemoveRaw=>flag(s,"own_cached")}}
 fn label(self)->&'static str{match self{Self::Download=>"下载缓存",Self::Compile=>"编译音频",Self::RemoveWave=>"删除编译",Self::RemoveRaw=>"删除缓存"}}
 fn deleting(self)->bool{matches!(self,Self::RemoveWave|Self::RemoveRaw)}
 fn run(self,name:&str,digest:&str)->Result<(),String>{match self{Self::Download=>music::download(name),Self::Compile=>music::prepare(name,digest),Self::RemoveWave=>music::remove_preview(digest),Self::RemoveRaw=>music::remove_raw(digest)}}
}
fn bulk_jobs(songs:&[Value],checked:&HashSet<String>,action:BulkAction)->Vec<(String,String)>{let mut hashes=HashSet::new();songs.iter().filter(|s|checked.contains(field(s,"name"))&&action.eligible(s)).filter(|s|hashes.insert(field(s,"md5").to_owned())).map(|s|(field(s,"name").to_owned(),field(s,"md5").to_owned())).collect()}
fn options(songs:&[Value],by_artist:bool)->Vec<String>{let mut values:Vec<String>=songs.iter().map(|s|if by_artist{artist(s)}else{song_title(s)}.to_owned()).collect();values.sort();values.dedup();values.insert(0,if by_artist{"全部演唱者".into()}else{"全部歌曲".into()});values}
struct Filter {search:String,artist:String,title:String,kind:i32,status:i32}
impl Filter {fn matches(&self,s:&Value)->bool {
 (self.artist.is_empty()||artist(s)==self.artist)&&(self.title.is_empty()||song_title(s)==self.title)
 &&(self.kind==0||(self.kind==2)==(field(s,"kind")=="live"))
 &&match self.status{1=>flag(s,"cached"),2=>!flag(s,"cached"),3=>flag(s,"compiled"),4=>!flag(s,"compiled"),_=>true}
 &&(self.search.is_empty()||[field(s,"name"),song_title(s),artist(s)].iter().any(|x|x.to_lowercase().contains(&self.search)))
}}
fn confirm(text:&str)->bool {
    use std::iter::once;
    #[link(name="user32")] extern "system" {fn MessageBoxW(hwnd:*mut std::ffi::c_void,text:*const u16,title:*const u16,flags:u32)->i32;}
    let message:Vec<u16>=text.encode_utf16().chain(once(0)).collect();let title:Vec<u16>="乐库".encode_utf16().chain(once(0)).collect();
    unsafe{MessageBoxW(std::ptr::null_mut(),message.as_ptr(),title.as_ptr(),0x24)==6}
}
impl State {
 fn render_bulk(&self,ui:&MusicWindow){
    ui.set_multi_mode(self.multi_mode);ui.set_checked_count(self.checked.len() as i32);
    let selected:Vec<_>=self.songs.iter().filter(|s|self.checked.contains(field(s,"name"))).collect();
    ui.set_bulk_summary(format!("所选资源：已缓存 {} 项，已编译 {} 项。",selected.iter().filter(|s|flag(s,"cached")).count(),selected.iter().filter(|s|flag(s,"compiled")).count()).into());
    ui.set_can_bulk_download(selected.iter().any(|s|BulkAction::Download.eligible(s)));ui.set_can_bulk_compile(selected.iter().any(|s|BulkAction::Compile.eligible(s)));ui.set_can_bulk_wave(selected.iter().any(|s|BulkAction::RemoveWave.eligible(s)));ui.set_can_bulk_raw(selected.iter().any(|s|BulkAction::RemoveRaw.eligible(s)));
 }
 fn bulk(&mut self,ui:&MusicWindow,action:BulkAction){
    if self.busy||!self.multi_mode{return}let jobs=bulk_jobs(&self.songs,&self.checked,action);
    if jobs.is_empty(){ui.set_notice("所选版本中没有需要执行此操作的资源。".into());return}
    if action.deleting(){let eligible=self.songs.iter().filter(|s|self.checked.contains(field(s,"name"))&&action.eligible(s)).count();let preserved=if matches!(action,BulkAction::RemoveRaw){"只删除乐库下载的原始缓存；游戏缓存和编译音频会保留。"}else{"只删除编译音频；原始资源缓存会保留。"};if !confirm(&format!("所选 {} 个版本中，有 {eligible} 个需要{}，涉及 {} 个资源文件。\r\n\r\n{preserved}\r\n\r\n确认继续？",self.checked.len(),action.label(),jobs.len())){return}}
    self.busy=true;self.render(ui);let tx=self.tx.clone();let alive=self.alive.clone();
    std::thread::spawn(move||{let total=jobs.len();let mut done=0;let mut errors=Vec::new();for (name,digest) in jobs{if !alive.load(Ordering::Acquire){break}let _=tx.send(Event::BulkProgress(format!("{}：{}/{}",action.label(),done+errors.len()+1,total)));match action.run(&name,&digest){Ok(())=>done+=1,Err(e)=>errors.push(format!("{name}：{e}"))}}let mut result=format!("{}完成：成功 {done} 个资源，失败 {} 个。",action.label(),errors.len());if let Some(e)=errors.first(){result.push_str(&format!(" {e}"));}let _=tx.send(Event::BulkComplete(result));});
 }
 fn sync_selection(&mut self,ui:&MusicWindow){let visible=self.visible(ui);if !visible.contains(&self.selected){self.selected=visible.first().cloned().unwrap_or_default();}}
 fn filter(&self,ui:&MusicWindow)->Filter {let artists=options(&self.songs,true);let titles=options(&self.songs,false);Filter{search:ui.get_search().trim().to_lowercase(),artist:if ui.get_artist_index()>0{artists.get(ui.get_artist_index() as usize).cloned().unwrap_or_default()}else{String::new()},title:if ui.get_title_index()>0{titles.get(ui.get_title_index() as usize).cloned().unwrap_or_default()}else{String::new()},kind:ui.get_kind_index(),status:ui.get_status_index()}}
 fn visible(&self,ui:&MusicWindow)->Vec<String>{let f=self.filter(ui);self.songs.iter().filter(|s|f.matches(s)).map(|s|field(s,"name").to_owned()).collect()}
 fn render(&self,ui:&MusicWindow){
    let filter=self.filter(ui);let artists=options(&self.songs,true);let titles=options(&self.songs,false);
    ui.set_artist_options(ModelRc::new(VecModel::from(artists.iter().map(|v|v.as_str().into()).collect::<Vec<slint::SharedString>>())));ui.set_title_options(ModelRc::new(VecModel::from(titles.iter().map(|v|v.as_str().into()).collect::<Vec<slint::SharedString>>())));
    let mode=ui.get_group_mode()==0;let values=if mode{&artists}else{&titles};
    let groups=values.iter().skip(1).map(|v|GroupRow{key:v.as_str().into(),label:v.as_str().into(),count:self.songs.iter().filter(|s|if mode{artist(s)==v}else{song_title(s)==v}).count() as i32,selected:if mode{filter.artist==*v}else{filter.title==*v}}).collect::<Vec<_>>();ui.set_groups(ModelRc::new(VecModel::from(groups)));
    let list:Vec<SongRow>=self.songs.iter().filter(|s|filter.matches(s)).map(|s|{
        let name=field(s,"name");let title=field(s,"title");
        SongRow{name:name.into(),title:if title.is_empty(){name.trim_end_matches(".awb").into()}else{title.into()},subtitle:artist(s).into(),variant:variant(s).into(),checked:self.checked.contains(name),cached:if flag(s,"cached"){"已缓存".into()}else{"未缓存".into()},compiled:if flag(s,"compiled"){"已编译".into()}else{"未编译".into()},selected:name==self.selected}
    }).collect();
    ui.set_result_label(format!("{} 个版本",list.len()).into());ui.set_visible_count(list.len() as i32);ui.set_visible_checked(list.iter().filter(|r|r.checked).count() as i32);ui.set_scope_title(if !filter.artist.is_empty(){filter.artist.clone().into()}else if !filter.title.is_empty(){filter.title.clone().into()}else{"全部歌曲".into()});ui.set_songs(ModelRc::new(VecModel::from(list)));ui.set_busy(self.busy);
    if let Some(s)=self.songs.iter().find(|s|field(s,"name")==self.playing_name){ui.set_now_title(song_title(s).into());ui.set_now_artist(format!("{} · {}",artist(s),if self.player.paused{"已暂停"}else if self.player.open{"正在播放"}else{"已停止"}).into());}
    self.render_bulk(ui);
    let song=self.songs.iter().find(|s|field(s,"name")==self.selected);
    ui.set_has_song(song.is_some());
    if let Some(s)=song {let title=field(s,"title");ui.set_detail_title(if title.is_empty(){"未匹配曲名".into()}else{title.into()});ui.set_detail_subtitle(format!("{}  /  {}",artist(s),if field(s,"kind")=="live"{"Live"}else{"通用"}).into());ui.set_resource(field(s,"name").into());
        let cached=flag(s,"cached");let own=flag(s,"own_cached");let compiled=flag(s,"compiled");ui.set_cached(cached);ui.set_own_cached(own);ui.set_compiled(compiled);
        ui.set_cache_label(if cached {if own {"已缓存 · 乐库".into()}else{"已缓存 · 游戏管理".into()}}else{"未缓存".into()});
        ui.set_compile_label(if compiled{"已编译 · WAV".into()}else{"未编译".into()});
    } else {ui.set_cached(false);ui.set_own_cached(false);ui.set_compiled(false);ui.set_detail_title("选择一首歌曲".into());ui.set_detail_subtitle("".into());ui.set_resource("".into());ui.set_cache_label("未缓存".into());ui.set_compile_label("未编译".into());}
    ui.set_playing(self.player.open);ui.set_paused(self.player.paused);ui.set_selected_current(self.player.open && self.playing_name==self.selected);
 }
 fn scan(&mut self,ui:&MusicWindow){if self.busy{return}self.busy=true;ui.set_notice("正在读取游戏歌曲清单…".into());self.render(ui);let tx=self.tx.clone();std::thread::spawn(move||{let _=tx.send(Event::Scan(music::scan()));});}
 fn action(&mut self,ui:&MusicWindow,action:&str,arg:&str){
    if let Some(op)=BulkAction::parse(action){self.bulk(ui,op);return}
    if action=="multi-toggle"{if !self.busy{self.multi_mode=!self.multi_mode;self.checked.clear();self.render(ui);}return}
    if action=="clear-checks"{if !self.busy{self.checked.clear();self.render(ui);}return}
    if action=="check"{if self.multi_mode&&!self.busy&&self.songs.iter().any(|s|field(s,"name")==arg){if !self.checked.remove(arg){self.checked.insert(arg.into());}self.render(ui);}return}
    if action=="check-visible"{if self.multi_mode&&!self.busy{for name in self.visible(ui){if arg=="1"{self.checked.insert(name);}else{self.checked.remove(&name);}}self.render(ui);}return}
    if action=="clear-filter"{ui.set_title_index(0);ui.set_kind_index(0);ui.set_status_index(0);self.sync_selection(ui);self.render(ui);return}
    if action=="filter"{self.sync_selection(ui);self.render(ui);return}
    if action=="reset"{ui.set_search("".into());ui.set_artist_index(0);ui.set_title_index(0);ui.set_kind_index(0);ui.set_status_index(0);self.sync_selection(ui);self.render(ui);return}
    if action=="group-mode"{ui.set_group_mode(arg.parse().unwrap_or(0));self.render(ui);return}
    if action=="group"{let mode=ui.get_group_mode()==0;let values=options(&self.songs,mode);let index=values.iter().position(|x|x==arg).unwrap_or(0) as i32;if mode{ui.set_artist_index(index);ui.set_title_index(0);}else{ui.set_title_index(index);ui.set_artist_index(0);}self.sync_selection(ui);self.render(ui);return}
    if action=="select"{self.selected=arg.into();self.render(ui);return}if action=="scan"{self.scan(ui);return}
    if action=="pause"{if let Err(e)=self.player.toggle_pause(){ui.set_notice(e.into());}self.render(ui);return}
    if action=="stop"{ui.set_notice(match self.player.stop(){Ok(())=>"播放已停止。".into(),Err(e)=>e.into()});self.render(ui);return}
    if self.busy{return}let Some(song)=self.songs.iter().find(|s|field(s,"name")==self.selected) else{return};
    let name=self.selected.clone();let digest=field(song,"md5").to_owned();
    match action {
      "delete-cache"|"delete-compiled"=>{let own=flag(song,"own_cached");let compiled=flag(song,"compiled");if (action=="delete-cache"&&!own)||(action=="delete-compiled"&&!compiled){return}let msg=if action=="delete-cache"{"删除乐库下载的原始资源？已编译的音频会保留。"}else{"删除已编译音频？原始缓存会保留。"};if !confirm(msg){return}let result=if action=="delete-cache"{music::remove_raw(&digest)}else{music::remove_preview(&digest)};ui.set_notice(match result{Ok(())=>"删除完成。".into(),Err(e)=>e.into()});for song in &mut self.songs{music::refresh(song);}self.render(ui);},
      "download"|"compile"=>{if action=="compile"&&!flag(song,"cached"){ui.set_notice("请先下载缓存。".into());return}self.busy=true;ui.set_notice(if action=="download"{"正在下载缓存…".into()}else{"正在编译音频…".into()});self.render(ui);let tx=self.tx.clone();let act=action.to_owned();std::thread::spawn(move||{let r=if act=="download"{music::download(&name)}else{music::prepare(&name,&digest)};let _=tx.send(Event::Job(name,r));});},
      "export"=>{let Some(path)=(unsafe{crate::legacy::choose_audio_path(std::ptr::null_mut(),&name)})else{return};if path.exists(){ui.set_notice("目标文件已存在，请选择其他文件名。".into());return}self.busy=true;ui.set_notice("正在准备导出…".into());self.render(ui);let tx=self.tx.clone();std::thread::spawn(move||{let r=music::prepare_preview(&name,&digest,&path);let _=tx.send(Event::Job(name,r));});},
      "play"=>{if self.player.open && self.playing_name==name && self.player.paused {let result=self.player.toggle_pause();if let Err(e)=result{ui.set_notice(e.into());}self.render(ui);return}if let Err(e)=self.player.stop(){ui.set_notice(e.into());return}let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();let path=std::env::temp_dir().join(format!("gakuactors-preview-{}-{stamp}.wav",std::process::id()));self.busy=true;ui.set_notice("正在准备播放；未缓存时会先下载…".into());self.render(ui);let tx=self.tx.clone();let alive=self.alive.clone();std::thread::spawn(move||{let r=music::prepare_preview(&name,&digest,&path);if !alive.load(Ordering::Acquire){let _=std::fs::remove_file(&path);return}if tx.send(Event::Play(name,path.clone(),r)).is_err(){let _=std::fs::remove_file(path);}});},
      _=>{}
    }
 }
}

pub fn run()->Result<(),slint::PlatformError>{
 platform::configure_font_fallback();
 let ui=MusicWindow::new()?;let (tx,rx)=mpsc::channel();let state=Rc::new(RefCell::new(State{songs:Vec::new(),selected:String::new(),playing_name:String::new(),checked:HashSet::new(),multi_mode:false,busy:false,player:preview::Player::new(),tx,alive:Arc::new(AtomicBool::new(true))}));
 ui.set_volume(state.borrow().player.volume as f32);state.borrow_mut().scan(&ui);
 let weak=ui.as_weak();let st=state.clone();ui.on_action(move|a,b|{if let Some(ui)=weak.upgrade(){st.borrow_mut().action(&ui,&a,&b);}});
 let weak=ui.as_weak();let st=state.clone();ui.on_search_changed(move||{if let Some(ui)=weak.upgrade(){let mut st=st.borrow_mut();st.sync_selection(&ui);st.render(&ui);}});
 let weak=ui.as_weak();let st=state.clone();ui.on_seek(move|value|{if let Some(ui)=weak.upgrade(){let mut st=st.borrow_mut();let at=(value.clamp(0.0,1000.0)*st.player.length as f32/1000.0) as u32;if let Err(e)=st.player.seek(at){ui.set_notice(e.into());}}});
 let weak=ui.as_weak();let st=state.clone();ui.on_volume_changed(move|value|{if let Some(ui)=weak.upgrade(){let mut st=st.borrow_mut();let value=value.round().clamp(0.0,100.0) as u32;if let Err(e)=st.player.set_volume(value){ui.set_notice(e.into());}}});
 let weak=ui.as_weak();let st=state.clone();ui.on_volume_save(move||{if let Some(ui)=weak.upgrade(){if let Err(e)=preview::save_volume(st.borrow().player.volume){ui.set_notice(e.into());}}});
 let timer=slint::Timer::default();let weak=ui.as_weak();let st=state.clone();let mut title_bar_ready=false;timer.start(slint::TimerMode::Repeated,Duration::from_millis(250),move||{let Some(ui)=weak.upgrade()else{return};if !title_bar_ready{title_bar_ready=platform::sync_title_bar(ui.window(),2);}let mut st=st.borrow_mut();let mut changed=false;for event in rx.try_iter(){if let Event::BulkProgress(message)=event{ui.set_notice(message.into());continue}st.busy=false;changed=true;match event{
    Event::Scan(Ok(songs))=>{ui.set_notice(format!("检索完成：{} 个版本。",songs.len()).into());st.songs=songs;let names:HashSet<String>=st.songs.iter().map(|s|field(s,"name").to_owned()).collect();st.checked.retain(|n|names.contains(n));st.sync_selection(&ui);},Event::Scan(Err(e))=>ui.set_notice(e.into()),
    Event::Job(name,r)=>ui.set_notice(match r{Ok(())=>format!("操作完成：{name}").into(),Err(e)=>e.into()}),
    Event::BulkComplete(message)=>ui.set_notice(message.into()),Event::BulkProgress(_)=>{},
    Event::Play(name,path,r)=>match r{Ok(())=>{ui.set_notice(match st.player.open_and_play(&path){Ok(())=>{st.playing_name=name.clone();format!("正在播放：{name}").into()},Err(e)=>{let _=std::fs::remove_file(&path);e.into()}});},Err(e)=>{let _=std::fs::remove_file(&path);ui.set_notice(e.into());}},
 }for song in &mut st.songs {music::refresh(song);}}
 if !st.busy&&st.player.finished(){let _=st.player.stop();ui.set_notice("本曲播放完毕。".into());st.render(&ui);}
 if changed{st.render(&ui)}let pos=st.player.position().unwrap_or(0);let len=st.player.length;let time=|v:u32|format!("{:02}:{:02}",v/60000,(v/1000)%60);ui.set_current_time(time(pos).into());ui.set_total_time(time(len).into());if !ui.get_seeking(){ui.set_seek_value(if len>0{pos as f32*1000.0/len as f32}else{0.0});}});
 ui.show()?;platform::sync_title_bar(ui.window(),2);
 // Observe without consuming events: a click outside search must also perform
 // its original action, including on otherwise non-focusable list rows.
 {
   use slint::winit_030::{WinitWindowAccessor,EventResult,winit::event::{WindowEvent,ElementState,TouchPhase}};
   let weak=ui.as_weak();let mut cursor=None;
   ui.window().on_winit_window_event(move|window,event|{
     let point=match event {
       WindowEvent::CursorMoved{position,..}=>{cursor=Some(*position);None},
       WindowEvent::MouseInput{state:ElementState::Pressed,..}=>cursor,
       WindowEvent::Touch(touch) if touch.phase==TouchPhase::Started=>Some(touch.location),
       WindowEvent::Focused(false)=>{if let Some(ui)=weak.upgrade(){ui.invoke_dismiss_search();}None},
       _=>None,
     };
     if let (Some(point),Some(ui))=(point,weak.upgrade()){let p=point.to_logical::<f32>(window.scale_factor() as f64);ui.invoke_pointer_down(p.x,p.y);}
     EventResult::Propagate
   });
 }
 let result=ui.run();state.borrow().alive.store(false,Ordering::Release);result
}

#[cfg(test)]
pub(crate) fn visual_fixture() -> MusicWindow {
    let ui = MusicWindow::new().unwrap();
    ui.set_scope_title("全部歌曲".into());
    ui.set_result_label("3 首".into());
    ui.set_groups(Rc::new(VecModel::from(vec![
        GroupRow { key: "all".into(), label: "全部歌曲".into(), count: 3, selected: true },
        GroupRow { key: "fktn".into(), label: "藤田琴音".into(), count: 2, selected: false },
    ])).into());
    ui.set_songs(Rc::new(VecModel::from(vec![
        SongRow { name: "demo-a".into(), title: "初".into(), subtitle: "藤田琴音".into(), variant: "Live".into(),
            cached: "已缓存".into(), selected: true, ..Default::default() },
        SongRow { name: "demo-b".into(), title: "光景".into(), subtitle: "篠澤広".into(), ..Default::default() },
        SongRow { name: "demo-c".into(), title: "世界一可愛い私".into(), subtitle: "藤田琴音".into(), ..Default::default() },
    ])).into());
    ui.set_detail_title("初".into());
    ui.set_detail_subtitle("藤田琴音 · Live".into());
    ui.set_has_song(true);
    ui.set_cached(true);
    ui.set_now_title("初".into());
    ui.set_now_artist("藤田琴音".into());
    ui
}

#[cfg(test)] mod layout_tests {
 use super::*;
 use slint::platform::{Platform,PlatformError,WindowAdapter,software_renderer::{MinimalSoftwareWindow,RepaintBufferType}};
 use slint::{PhysicalSize,Rgb8Pixel,Model};
 struct TestPlatform {window:Rc<MinimalSoftwareWindow>}
 slint::slint! {
   #[edition="1.19"]
   export component FontFallbackProbe inherits Window {
     in property <string> family:"Microsoft YaHei UI";
     default-font-family:root.family; background:#101219;
     in property <string> sample:"∀";
     Text {text:root.sample;font-size:32px;color:#eff1f7;}
   }
 }
 impl Platform for TestPlatform {fn create_window_adapter(&self)->Result<Rc<dyn WindowAdapter>,PlatformError>{Ok(self.window.clone())}}
 fn fixture()->Vec<Value>{vec![serde_json::json!({"name":"track-a","title":"初","performer":"冰渡香名江","kind":"general","cached":true,"compiled":true,"own_cached":true}),serde_json::json!({"name":"track-b","title":"初","performer":"冰渡香名江","kind":"live","cached":true,"compiled":false}),serde_json::json!({"name":"track-c","title":"初","performer":"藤田琴音","kind":"general","cached":false,"compiled":false}),serde_json::json!({"name":"track-d","title":"光景","performer":"冰渡香名江","kind":"general","cached":true,"compiled":false})]}
 #[test] fn filters_intersect_artist_title_version_and_state(){let data=fixture();let mut f=Filter{search:String::new(),artist:"冰渡香名江".into(),title:"初".into(),kind:0,status:0};assert_eq!(data.iter().filter(|s|f.matches(s)).count(),2);f.kind=2;assert_eq!(data.iter().filter(|s|f.matches(s)).count(),1);f.status=3;assert_eq!(data.iter().filter(|s|f.matches(s)).count(),0);f.kind=0;assert_eq!(data.iter().filter(|s|f.matches(s)).count(),1);f.search="光景".into();assert_eq!(data.iter().filter(|s|f.matches(s)).count(),0);assert_eq!(options(&data,true).len(),3);assert_eq!(options(&data,false).len(),3);assert_eq!(artist(&serde_json::json!({"performer":""})),"未知演唱");}
 #[test] fn batch_scope_uses_checked_resources_and_preserves_game_cache(){
   let songs=vec![serde_json::json!({"name":"a","md5":"same","cached":true,"compiled":true,"own_cached":true}),serde_json::json!({"name":"alias","md5":"same","cached":true,"compiled":true,"own_cached":true}),serde_json::json!({"name":"game","md5":"game","cached":true,"compiled":false,"own_cached":false,"game_cached":true}),serde_json::json!({"name":"remote","md5":"remote","cached":false,"compiled":false})];
   let checked:HashSet<String>=["a","alias","game","remote"].into_iter().map(String::from).collect();
   assert_eq!(bulk_jobs(&songs,&checked,BulkAction::RemoveRaw),vec![("a".into(),"same".into())]);
   assert_eq!(bulk_jobs(&songs,&checked,BulkAction::RemoveWave).len(),1);
   assert_eq!(bulk_jobs(&songs,&checked,BulkAction::Compile),vec![("game".into(),"game".into())]);
   assert_eq!(bulk_jobs(&songs,&checked,BulkAction::Download),vec![("remote".into(),"remote".into())]);
   assert!(bulk_jobs(&songs,&HashSet::new(),BulkAction::RemoveRaw).is_empty());
 }
 #[test] fn header_and_player_expand_at_supported_sizes(){
   platform::configure_font_fallback();
   let window=MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
   slint::platform::set_platform(Box::new(TestPlatform{window:window.clone()})).unwrap();
   // Exercise the actual software renderer. YaHei has no U+2200; its fallback
   // must draw the same glyph as an explicit Segoe UI Symbol font request.
   let probe=FontFallbackProbe::new().unwrap();probe.show().unwrap();window.set_size(PhysicalSize::new(200,60));
   let glyph_shape=|pixels:&[Rgb8Pixel]|{let ink:Vec<(usize,usize)>=pixels.iter().enumerate().filter(|(_,p)|p.r>128&&p.g>128&&p.b>128).map(|(i,_)|(i%200,i/200)).collect();assert!(ink.len()>50,"U+2200 must render through the symbol fallback");let left=ink.iter().map(|(x,_)|*x).min().unwrap();let top=ink.iter().map(|(_,y)|*y).min().unwrap();ink.into_iter().map(|(x,y)|(x-left,y-top)).collect::<Vec<_>>()};
   let mut pixels=vec![Rgb8Pixel::default();200*60];window.draw_if_needed(|renderer|{renderer.render(&mut pixels,200);});let fallback=glyph_shape(&pixels);
   probe.set_family("Segoe UI Symbol".into());slint::platform::update_timers_and_animations();window.draw_if_needed(|renderer|{renderer.render(&mut pixels,200);});assert_eq!(fallback,glyph_shape(&pixels));
   probe.set_family("Microsoft YaHei UI".into());probe.set_sample("∀ﾟ".into());slint::platform::update_timers_and_animations();window.draw_if_needed(|renderer|{renderer.render(&mut pixels,200);});let mixed=glyph_shape(&pixels);
   probe.set_family("Yu Gothic Medium".into());slint::platform::update_timers_and_animations();window.draw_if_needed(|renderer|{renderer.render(&mut pixels,200);});assert_eq!(mixed,glyph_shape(&pixels),"mixed symbol/kana cluster must select a font covering both characters");probe.hide().unwrap();drop(probe);
   let ui=MusicWindow::new().unwrap();let (tx,_rx)=mpsc::channel();let mut data=fixture();for i in 0..40 {data.push(serde_json::json!({"name":format!("scroll-test-{i}"),"title":"滚动列表中的歌曲版本","performer":format!("演唱者姓名 {i:02}"),"kind":"general","cached":true,"compiled":false}));}let mut st=State{songs:data,selected:"track-a".into(),playing_name:"track-a".into(),checked:HashSet::new(),multi_mode:false,busy:false,player:preview::Player::new(),tx,alive:Arc::new(AtomicBool::new(true))};st.render(&ui);ui.show().unwrap();
   assert!(!ui.get_multi_mode());assert_eq!(ui.get_checked_count(),0);
   st.action(&ui,"check","track-a");assert!(st.checked.is_empty());
   ui.set_filter_open(true);st.action(&ui,"multi-toggle","");assert!(st.checked.is_empty());
   st.action(&ui,"check","track-a");st.action(&ui,"check","track-b");assert_eq!(st.checked.len(),2);
   st.action(&ui,"clear-checks","");assert!(ui.get_multi_mode());assert!(ui.get_filter_open());assert!(st.checked.is_empty());
   ui.set_kind_index(2);st.action(&ui,"filter","");st.action(&ui,"check-visible","1");assert_eq!(st.checked,HashSet::from(["track-b".to_string()]));
   st.action(&ui,"clear-filter","");assert!(ui.get_filter_open());assert_eq!(ui.get_kind_index(),0);assert!(st.checked.contains("track-b"));
   st.action(&ui,"multi-toggle","");assert!(!ui.get_multi_mode());assert!(st.checked.is_empty());assert!(ui.get_filter_open());ui.set_filter_open(false);
   let actions=Rc::new(RefCell::new(Vec::<String>::new()));let recorded=actions.clone();ui.on_action(move|a,_|recorded.borrow_mut().push(a.to_string()));
   ui.set_resource("sud_music_general_hkne-001-hkne_game-inst.awb".into());
   for theme in [2] {slint::platform::update_timers_and_animations();
   for (w,h) in [(1040,660),(1280,800),(1920,1080)] {
     window.set_size(PhysicalSize::new(w,h));let mut pixels=vec![Rgb8Pixel::default();(w*h) as usize];
     window.draw_if_needed(|renderer|{renderer.render(&mut pixels,w as usize);});
     if w==1280 {if let Some(path)=std::env::var_os("GAKU_MUSIC_LAYOUT_SNAPSHOT") {let snapshot=std::path::PathBuf::from(path).with_file_name(format!("approved-theme-{theme}.png"));let file=std::fs::File::create(snapshot).unwrap();let mut encoder=png::Encoder::new(file,w,h);encoder.set_color(png::ColorType::Rgb);encoder.set_depth(png::BitDepth::Eight);let mut writer=encoder.write_header().unwrap();let mut bytes=Vec::with_capacity(pixels.len()*3);for p in &pixels{bytes.extend_from_slice(&[p.r,p.g,p.b]);}writer.write_image_data(&bytes).unwrap();}}
     assert!((ui.get_layout_search_center()-w as f32/2.0).abs()<1.0,"search must be centered in the window");assert!((ui.get_layout_left_height()-ui.get_layout_list_height()).abs()<1.0,"library cards must have equal height");
     assert!(ui.get_layout_progress_width()>=120.0,"progress must remain usable");
     assert!((ui.get_layout_play_center()-ui.get_layout_progress_center()).abs()<1.0,"play control and seek track must share a center line");
     assert!(ui.get_layout_footer_bottom()<=h as f32-20.0,"player must fit above status at minimum size");
     if w>=1280 {assert!(ui.get_layout_progress_width()>600.0,"progress should use available width");}
   }}
   use slint::platform::{WindowEvent,PointerEventButton};use slint::LogicalPosition;
   let click=|x:f32,y:f32|{ui.invoke_pointer_down(x,y);let position=LogicalPosition::new(x,y);ui.window().dispatch_event(WindowEvent::PointerPressed{position,button:PointerEventButton::Left});ui.window().dispatch_event(WindowEvent::PointerReleased{position,button:PointerEventButton::Left});};
   ui.set_search("初".into());click(960.0,34.0);assert!(ui.get_search_focused());
   click(980.0,34.0);assert!(ui.get_search_focused(),"clicking inside search must retain text editing");
   click(600.0,130.0);assert!(!ui.get_search_focused(),"clicking blank content must release search focus");assert_eq!(ui.get_search().as_str(),"初");
   click(960.0,34.0);assert!(ui.get_search_focused());let before=actions.borrow().len();click(400.0,280.0);assert!(!ui.get_search_focused());assert_eq!(actions.borrow().len(),before+1,"the same outside click must still select the row");assert_eq!(actions.borrow().last().unwrap(),"select");
   click(960.0,34.0);ui.window().dispatch_event(WindowEvent::KeyPressed{text:slint::platform::Key::Escape.into()});assert!(!ui.get_search_focused());assert_eq!(ui.get_search().as_str(),"初");
   click(960.0,34.0);ui.window().dispatch_event(WindowEvent::KeyPressed{text:"\n".into()});assert!(!ui.get_search_focused());assert_eq!(ui.get_search().as_str(),"初");ui.set_search("".into());
   click(1920.0-55.0,34.0);assert!(actions.borrow().iter().any(|a|a=="scan"),"custom controls must deliver pointer clicks");
   ui.set_playing(true);let seeked=Rc::new(RefCell::new(0.0));let value=seeked.clone();ui.on_seek(move|v|*value.borrow_mut()=v);
   click(12.0+444.0+ui.get_layout_progress_width()*0.7,1080.0-112.0+41.0);assert!(*seeked.borrow()>600.0&&*seeked.borrow()<800.0,"the themed seek track must remain interactive");
   ui.set_playing(false);ui.set_filter_open(true);st.action(&ui,"multi-toggle","");st.action(&ui,"check","track-a");st.action(&ui,"check","track-b");assert_eq!(ui.get_checked_count(),2);assert!(ui.get_multi_mode());
   ui.set_resource("sud_music_general_hkne-001-hkne_game-inst.awb".into());window.set_size(PhysicalSize::new(1280,800));slint::platform::update_timers_and_animations();let mut pixels=vec![Rgb8Pixel::default();1280*800];window.draw_if_needed(|renderer|{renderer.render(&mut pixels,1280);});
   if let Some(path)=std::env::var_os("GAKU_MUSIC_LAYOUT_SNAPSHOT"){let file=std::fs::File::create(std::path::PathBuf::from(path).with_file_name("approved-multi.png")).unwrap();let mut encoder=png::Encoder::new(file,1280,800);encoder.set_color(png::ColorType::Rgb);encoder.set_depth(png::BitDepth::Eight);let mut writer=encoder.write_header().unwrap();let bytes:Vec<u8>=pixels.iter().flat_map(|p|[p.r,p.g,p.b]).collect();writer.write_image_data(&bytes).unwrap();}
   window.set_size(PhysicalSize::new(1280,800));
   let original="奇迹正是现在(ﾟ∀ﾟ)！";st.action(&ui,"multi-toggle","");ui.set_filter_open(false);st.songs[0]["title"]=original.into();st.playing_name="track-a".into();st.selected="track-a".into();st.render(&ui);
   st.action(&ui,"group","冰渡香名江");assert!(ui.get_groups().iter().any(|g|g.selected&&g.key=="冰渡香名江"));
   assert_eq!(ui.get_detail_title().as_str(),original);assert_eq!(ui.get_now_title().as_str(),original);
   for theme in [2] {slint::platform::update_timers_and_animations();let mut pixels=vec![Rgb8Pixel::default();1280*800];window.draw_if_needed(|renderer|{renderer.render(&mut pixels,1280);});
     if let Some(path)=std::env::var_os("GAKU_MUSIC_LAYOUT_SNAPSHOT"){let file=std::fs::File::create(std::path::PathBuf::from(path).with_file_name(format!("font-fallback-theme-{theme}.png"))).unwrap();let mut encoder=png::Encoder::new(file,1280,800);encoder.set_color(png::ColorType::Rgb);encoder.set_depth(png::BitDepth::Eight);let mut writer=encoder.write_header().unwrap();let bytes:Vec<u8>=pixels.iter().flat_map(|p|[p.r,p.g,p.b]).collect();writer.write_image_data(&bytes).unwrap();}
   }
 }
}
