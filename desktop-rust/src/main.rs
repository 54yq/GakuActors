#![windows_subsystem = "windows"]
//! Native Windows scene manager connected to the in-game plugin.
use std::ffi::c_void;
use std::iter::once;
use std::os::windows::ffi::OsStrExt;
use std::ptr::{null, null_mut};
use std::cell::RefCell;
use std::sync::{mpsc,Arc,Mutex,OnceLock};
use std::sync::atomic::{AtomicU16,Ordering};
use std::time::Duration;
use serde_json::{json,Value};
mod pipe;
mod thumbnail;
mod music;
mod preview;

type Handle = *mut c_void;
type WndProc = unsafe extern "system" fn(Handle, u32, usize, isize) -> isize;
#[repr(C)]
struct WndClass {
    style: u32, proc: Option<WndProc>, class_extra: i32, window_extra: i32,
    instance: Handle, icon: Handle, cursor: Handle, background: Handle,
    menu_name: *const u16, class_name: *const u16,
}
#[repr(C)]
struct Msg { hwnd: Handle, message: u32, wparam: usize, lparam: isize, time: u32, x: i32, y: i32, private: u32 }
#[repr(C)]
#[derive(Clone,Copy)]
struct Rect {left:i32,top:i32,right:i32,bottom:i32}
#[repr(C)]
struct Point {x:i32,y:i32}
#[repr(C)]
struct MinMaxInfo {reserved:Point,max_size:Point,max_position:Point,min_track:Point,max_track:Point}
#[repr(C)]
struct PaintStruct {dc:Handle,erase:i32,paint:Rect,restore:i32,inc_update:i32,reserved:[u8;32]}
#[repr(C)]
struct DrawItem {ctl_type:u32,ctl_id:u32,item_id:u32,action:u32,state:u32,hwnd:Handle,dc:Handle,rect:Rect,data:usize}
#[repr(C)]
struct BitmapHeader {size:u32,width:i32,height:i32,planes:u16,bits:u16,compression:u32,image_size:u32,xppm:i32,yppm:i32,used:u32,important:u32}
#[repr(C)]
struct BitmapInfo {header:BitmapHeader,colors:[u32;1]}
#[repr(C)]
struct ChooseColor {size:u32,owner:Handle,instance:Handle,rgb:u32,custom:*mut u32,flags:u32,data:isize,hook:Handle,template:*const u16}
#[repr(C)]
struct InitCommonControls {size:u32,classes:u32}
#[repr(C)]
struct OpenFileName {size:u32,owner:Handle,instance:Handle,filter:*const u16,custom_filter:*mut u16,max_custom:u32,filter_index:u32,file:*mut u16,max_file:u32,file_title:*mut u16,max_file_title:u32,initial_dir:*const u16,title:*const u16,flags:u32,file_offset:u16,file_extension:u16,default_ext:*const u16,custom_data:isize,hook:Handle,template:*const u16,reserved:*mut c_void,reserved_value:u32,flags_ex:u32}

#[link(name = "user32")]
extern "system" {
    fn RegisterClassW(value: *const WndClass) -> u16;
    fn CreateWindowExW(ex: u32, class: *const u16, title: *const u16, style: u32,
        x: i32, y: i32, width: i32, height: i32, parent: Handle, menu: Handle,
        instance: Handle, param: *mut c_void) -> Handle;
    fn DefWindowProcW(hwnd: Handle, msg: u32, wp: usize, lp: isize) -> isize;
    fn ShowWindow(hwnd: Handle, cmd: i32) -> i32;
    fn MoveWindow(hwnd:Handle,x:i32,y:i32,w:i32,h:i32,repaint:i32)->i32;
    fn GetClientRect(hwnd:Handle,rect:*mut Rect)->i32;
    fn EnableWindow(hwnd: Handle, enabled: i32) -> i32;
    fn GetDlgItem(hwnd: Handle, id: i32) -> Handle;
    fn GetDlgCtrlID(hwnd:Handle)->i32;
    fn GetFocus()->Handle;
    fn UpdateWindow(hwnd: Handle) -> i32;
    fn GetMessageW(msg: *mut Msg, hwnd: Handle, min: u32, max: u32) -> i32;
    fn TranslateMessage(msg: *const Msg) -> i32;
    fn DispatchMessageW(msg: *const Msg) -> isize;
    fn PostQuitMessage(code: i32);
    fn LoadCursorW(instance: Handle, name: *const u16) -> Handle;
    fn SendMessageW(hwnd: Handle, msg: u32, wp: usize, lp: isize) -> isize;
    fn PostMessageW(hwnd: Handle, msg: u32, wp: usize, lp: isize) -> i32;
    fn SetWindowTextW(hwnd: Handle, text: *const u16) -> i32;
    fn GetWindowTextW(hwnd: Handle, text: *mut u16, count: i32) -> i32;
    fn MessageBoxW(hwnd:Handle,text:*const u16,caption:*const u16,flags:u32)->i32;
    fn SetProcessDPIAware()->i32;
    fn GetSystemMetrics(index:i32)->i32;
    fn BeginPaint(hwnd:Handle,paint:*mut PaintStruct)->Handle;
    fn EndPaint(hwnd:Handle,paint:*const PaintStruct)->i32;
    fn FillRect(dc:Handle,rect:*const Rect,brush:Handle)->i32;
    fn InvalidateRect(hwnd:Handle,rect:*const Rect,erase:i32)->i32;
    fn DrawTextW(dc:Handle,text:*const u16,count:i32,rect:*mut Rect,format:u32)->i32;
    fn SetTimer(hwnd:Handle,id:usize,ms:u32,callback:*mut c_void)->usize;
    fn KillTimer(hwnd:Handle,id:usize)->i32;
}
#[link(name="comctl32")]
extern "system" { fn InitCommonControlsEx(value:*const InitCommonControls)->i32; }
#[link(name = "kernel32")]
extern "system" { fn GetModuleHandleW(name: *const u16) -> Handle; }
#[link(name = "comdlg32")]
extern "system" { fn ChooseColorW(value:*mut ChooseColor)->i32; fn GetSaveFileNameW(value:*mut OpenFileName)->i32; }
#[link(name = "gdi32")]
extern "system" {
    fn GetStockObject(index: i32) -> Handle;
    fn CreateSolidBrush(color:u32)->Handle;
    fn CreatePen(style:i32,width:i32,color:u32)->Handle;
    fn SelectObject(dc:Handle,object:Handle)->Handle;
    fn SetTextColor(dc:Handle,color:u32)->u32;
    fn SetBkMode(dc:Handle,mode:i32)->i32;
    fn RoundRect(dc:Handle,left:i32,top:i32,right:i32,bottom:i32,width:i32,height:i32)->i32;
    fn CreateDIBSection(dc:Handle,info:*const BitmapInfo,usage:u32,bits:*mut *mut c_void,section:Handle,offset:u32)->Handle;
    fn DeleteObject(object:Handle)->i32;
    fn CreateFontW(height:i32,width:i32,esc:i32,orientation:i32,weight:i32,italic:u32,underline:u32,strike:u32,charset:u32,out:u32,clip:u32,quality:u32,pitch:u32,name:*const u16)->Handle;
}

fn wide(s: &str) -> Vec<u16> { std::ffi::OsStr::new(s).encode_wide().chain(once(0)).collect() }
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn music_panels_fit_supported_window_sizes() {
        for (w,h) in [(984,610),(1440,860),(1920,1040)] {
            let (list,detail,player)=music_panels(w,h);
            assert!(list.2>300 && list.3>150);
            assert!(detail.0>=list.0+list.2 && detail.0+detail.2<=w);
            assert!(player.1>=list.1+list.3 && player.1+player.3<h);
        }
    }
    #[test]
    fn native_layout_and_utf16() {
        assert_eq!(std::mem::size_of::<WndClass>(), 72);
        assert_eq!(std::mem::size_of::<Msg>(), 48);
        let value=wide("香名江");
        assert_eq!(value.last(),Some(&0));
        assert_eq!(String::from_utf16(&value[..value.len()-1]).unwrap(),"香名江");
    }
}
const WS_OVERLAPPEDWINDOW: u32 = 0x00cf0000;
const WS_CHILD: u32 = 0x40000000;
const WS_VISIBLE: u32 = 0x10000000;
const WS_BORDER: u32 = 0x00800000;
const WM_DESTROY: u32 = 0x0002;
const WM_SETFONT: u32 = 0x0030;
const WM_COMMAND: u32 = 0x0111;
const WM_HSCROLL:u32=0x0114;
const WM_TIMER:u32=0x0113;
const WM_SNAPSHOT: u32 = 0x8001;
const LB_ADDSTRING:u32=0x0180;
const LB_RESETCONTENT:u32=0x0184;
const LB_GETCURSEL:u32=0x0188;
const LB_SETCURSEL:u32=0x0186;
const CB_ADDSTRING:u32=0x0143;
const CB_RESETCONTENT:u32=0x014b;
const CB_GETCURSEL:u32=0x0147;
const CB_SETCURSEL:u32=0x014e;
const IDC_ARROW: usize = 32512;
const DEFAULT_GUI_FONT: i32 = 17;

static SENDER:OnceLock<mpsc::Sender<Value>>=OnceLock::new();
static LATEST:OnceLock<Arc<Mutex<Value>>>=OnceLock::new();
struct ThumbResult {id:String,status:String,bitmap:Option<thumbnail::Bitmap>,head_status:String,head_bitmap:Option<thumbnail::Bitmap>}
static THUMB:OnceLock<Arc<Mutex<Option<ThumbResult>>>>=OnceLock::new();
static SCALE:OnceLock<f64>=OnceLock::new();
static FONT:OnceLock<usize>=OnceLock::new();
static HEADING_FONT:OnceLock<usize>=OnceLock::new();
static MUSIC_TITLE_FONT:OnceLock<usize>=OnceLock::new();
static MUSIC_ROW_FONT:OnceLock<usize>=OnceLock::new();
static MUSIC_SUB_FONT:OnceLock<usize>=OnceLock::new();
static WHITE_BRUSH:OnceLock<usize>=OnceLock::new();
static BACK_BRUSH:OnceLock<usize>=OnceLock::new();
static MUSIC_ONLY:OnceLock<bool>=OnceLock::new();
enum MusicResult { Scan(Result<Vec<Value>,String>), Export(String,Result<(),String>), Preview(String,std::path::PathBuf,Result<(),String>) }
static MUSIC_RESULT:OnceLock<Mutex<Option<MusicResult>>>=OnceLock::new();
static NAV_SELECTED:AtomicU16=AtomicU16::new(106);
thread_local! { static APP:RefCell<Option<App>>=const {RefCell::new(None)}; }
#[derive(Clone,Copy,PartialEq)]
enum View { People,Actors,Scene,Props,Music }
struct App {
    hwnd:Handle,status:Handle,left:Handle,right:Handle,view:View,snapshot:Value,
    characters:Vec<String>,presets:Vec<Value>,resources:Vec<Value>,selected_character:String,
    left_rows:Vec<String>,right_rows:Vec<String>,list_view:Option<View>,resource_filter:String,
    selected_preset:String,selected_resource:Option<(i64,i64)>,
    actors:Vec<i64>,actor_labels:Vec<String>,actor:Handle,selected_actor:i64,selected_reference:i64,parts:[Handle;3],part_items:[Vec<String>;3],
    name:Handle,position:[Handle;3],reset_wait_revision:Option<u64>,reset_wait_actor:i64,stage:Handle,picture:Handle,picture_status:Handle,head_picture:Handle,head_status:Handle,edit_id:String,cross_character:bool,editing:bool,stage_choice:usize,stage_initialized:bool,
    songs:Vec<Value>, music_visible:Vec<usize>, music_rows:Vec<String>, music_filter:String, music_selected:String, music_busy:bool, music_notice:String,
    preview:preview::Player, preview_pending:Option<std::path::PathBuf>, preview_alive:Arc<std::sync::atomic::AtomicBool>, preview_seek_drag:bool,volume_dirty:bool,
}
impl Drop for App {
    fn drop(&mut self){self.preview_alive.store(false,Ordering::Release);if self.volume_dirty{let _=preview::save_volume(self.preview.volume);}if let Some(path)=self.preview_pending.take(){let _=std::fs::remove_file(path);}}
}
fn send(v:Value) { if let Some(tx)=SENDER.get(){let _=tx.send(v);} }
fn text_value<'a>(v:&'a Value,key:&str)->&'a str {v.get(key).and_then(Value::as_str).unwrap_or("")}
unsafe fn text(hwnd:Handle,value:&str) {SetWindowTextW(hwnd,wide(value).as_ptr());}
unsafe fn list_add(hwnd:Handle,value:&str) {let w=wide(value);SendMessageW(hwnd,LB_ADDSTRING,0,w.as_ptr() as isize);}
unsafe fn sync_resource_list(hwnd:Handle,rows:Vec<String>,cache:&mut Vec<String>,wanted:Option<usize>,reset_top:bool) {
    if rows!=*cache||reset_top {
        let top=if reset_top {0}else{SendMessageW(hwnd,0x018e,0,0).max(0) as usize};
        SendMessageW(hwnd,0x000b,0,0);SendMessageW(hwnd,LB_RESETCONTENT,0,0);
        for row in &rows {list_add(hwnd,row);}
        if let Some(i)=wanted {SendMessageW(hwnd,LB_SETCURSEL,i,0);}
        if !rows.is_empty(){SendMessageW(hwnd,0x0197,top.min(rows.len()-1),0);}
        SendMessageW(hwnd,0x000b,1,0);InvalidateRect(hwnd,null(),1);*cache=rows;
    }
    if selected(hwnd)!=wanted {SendMessageW(hwnd,LB_SETCURSEL,wanted.unwrap_or(usize::MAX),0);}
}
unsafe fn selected(hwnd:Handle)->Option<usize> {let n=SendMessageW(hwnd,LB_GETCURSEL,0,0);if n<0{None}else{Some(n as usize)}}
unsafe fn combo_selected(hwnd:Handle)->Option<usize> {let n=SendMessageW(hwnd,CB_GETCURSEL,0,0);if n<0{None}else{Some(n as usize)}}
unsafe fn combo_add(hwnd:Handle,value:&str) {let w=wide(value);SendMessageW(hwnd,CB_ADDSTRING,0,w.as_ptr() as isize);}
unsafe fn get_text(hwnd:Handle)->String {let mut data=[0u16;512];let n=GetWindowTextW(hwnd,data.as_mut_ptr(),data.len() as i32);String::from_utf16_lossy(&data[..n.max(0) as usize])}
unsafe fn choose_audio_path(owner:Handle,name:&str)->Option<std::path::PathBuf> {
    let default=name.trim_end_matches(".awb").to_owned()+".wav";
    let mut filename=vec![0u16;32768];let init=wide(&default);if init.len()>filename.len(){return None;}filename[..init.len()].copy_from_slice(&init);
    let filter:Vec<u16>="WAV 音频\0*.wav\0\0".encode_utf16().collect();let ext=wide("wav");
    let mut ofn:OpenFileName=std::mem::zeroed();ofn.size=std::mem::size_of::<OpenFileName>() as u32;ofn.owner=owner;ofn.filter=filter.as_ptr();ofn.filter_index=1;ofn.file=filename.as_mut_ptr();ofn.max_file=filename.len() as u32;ofn.default_ext=ext.as_ptr();ofn.flags=0x00000800|0x00000008;
    if GetSaveFileNameW(&mut ofn)==0{return None;}
    let len=filename.iter().position(|x|*x==0).unwrap_or(filename.len());Some(std::path::PathBuf::from(String::from_utf16_lossy(&filename[..len])))
}
impl App {
    unsafe fn set_picture(picture:Handle,status:Handle,label:&str,bitmap:Option<thumbnail::Bitmap>) {
        let previous=SendMessageW(picture,0x0172,0,0) as Handle;
        if !previous.is_null(){DeleteObject(previous);}
        text(status,label);
        if let Some(image)=bitmap {
            let scale=*SCALE.get().unwrap_or(&1.0);let image=image.fit((135.0*scale).max(1.0) as u32,(145.0*scale).max(1.0) as u32);let mut bits=null_mut();
            let info=BitmapInfo{header:BitmapHeader{size:40,width:image.width as i32,height:-(image.height as i32),planes:1,bits:32,compression:0,image_size:0,xppm:0,yppm:0,used:0,important:0},colors:[0]};
            let bitmap=CreateDIBSection(null_mut(),&info,0,&mut bits,null_mut(),0);
            if bitmap.is_null()||bits.is_null(){if !bitmap.is_null(){DeleteObject(bitmap);}text(status,"缩略图显示失败");return;}
            std::ptr::copy_nonoverlapping(image.bgra.as_ptr(),bits as *mut u8,image.bgra.len());
            let replaced=SendMessageW(picture,0x0172,0,bitmap as isize) as Handle;
            if !replaced.is_null(){DeleteObject(replaced);}
        }
    }
    unsafe fn thumbnail(&mut self,result:ThumbResult) {
        if result.id!=self.selected_preset{return;}
        Self::set_picture(self.picture,self.picture_status,&format!("身体服装：{}",result.status),result.bitmap);
        Self::set_picture(self.head_picture,self.head_status,&format!("头部组合：{}",result.head_status),result.head_bitmap);
    }
    unsafe fn render(&mut self) {
        let admitted=self.snapshot.get("admitted").and_then(Value::as_bool).unwrap_or(false);
        if let Some(revision)=self.reset_wait_revision {
            if self.selected_actor==self.reset_wait_actor && self.snapshot["pose_reset_actor"].as_i64()==Some(self.reset_wait_actor)
                && self.snapshot["pose_reset_revision"].as_u64().unwrap_or(0)>revision {
                for i in 0..3 {if let Some(value)=self.snapshot["pose_reset_position"][i].as_f64(){text(self.position[i],&format!("{value:.2}"));}}
                self.reset_wait_revision=None;
            }
        }
        let connected=SENDER.get().is_some() && admitted;
        let ready=connected && !self.snapshot["busy"].as_bool().unwrap_or(false) && !self.snapshot["playing"].as_bool().unwrap_or(false) && !self.snapshot["faulted"].as_bool().unwrap_or(false);
        let writable=self.snapshot["writable"].as_bool().unwrap_or(false);
        let no_conflict=!self.snapshot["motion_conflict"].as_bool().unwrap_or(false);
        let people=self.view==View::People;
        let scene=self.view==View::Scene;
        let actors_view=self.view==View::Actors;
        let music_view=self.view==View::Music;
        let only=*MUSIC_ONLY.get().unwrap_or(&false);
        for id in [106,126,107,108] {ShowWindow(GetDlgItem(self.hwnd,id),if only {0}else{5});}
        ShowWindow(GetDlgItem(self.hwnd,132),0);
        if only {ShowWindow(GetDlgItem(self.hwnd,140),0);}
        for id in [102,105] {ShowWindow(GetDlgItem(self.hwnd,id),if music_view {0}else{5});}
        for id in [133,134,135,136,137,138,139,221,323,335,336] {ShowWindow(GetDlgItem(self.hwnd,id),if music_view {5}else{0});}
        for id in [101,119,118,111,220,321,322] {
            ShowWindow(GetDlgItem(self.hwnd,id),if people {5}else{0});
        }
        ShowWindow(GetDlgItem(self.hwnd,127),0);
        for id in [103] {ShowWindow(GetDlgItem(self.hwnd,id),if people||actors_view {5}else{0});}
        for id in [104,205,301] {ShowWindow(GetDlgItem(self.hwnd,id),if people && !self.editing {5}else{0});}
        for id in [112,113,114,122,123,124,125,216,316,302,303,304,305] {
            ShowWindow(GetDlgItem(self.hwnd,id),if actors_view {5}else{0});
        }
        for id in [110,117,121,210,310] {
            ShowWindow(GetDlgItem(self.hwnd,id),if people && self.editing {5}else{0});
        }
        for id in [211,212,213,311,312,313] {ShowWindow(GetDlgItem(self.hwnd,id),if people&&self.editing {5}else{0});}
        for id in [115,116,209,120,314] {ShowWindow(GetDlgItem(self.hwnd,id),if scene {5}else{0});}
        for id in [320] {ShowWindow(GetDlgItem(self.hwnd,id),if !people {5}else{0});}
        ShowWindow(GetDlgItem(self.hwnd,109),if scene||self.view==View::Props {5}else{0});
        ShowWindow(self.right,if people {5}else{0});
        ShowWindow(self.picture,if people {5}else{0});ShowWindow(self.picture_status,if people {5}else{0});
        ShowWindow(self.head_picture,if people {5}else{0});ShowWindow(self.head_status,if people {5}else{0});
        let nav=match self.view {View::People=>106,View::Actors=>126,View::Scene=>107,View::Props=>108,View::Music=>132};
        if NAV_SELECTED.swap(nav,Ordering::Relaxed)!=nav {for id in [106,126,107,108,132] {InvalidateRect(GetDlgItem(self.hwnd,id),null(),1);}}
        text(GetDlgItem(self.hwnd,330),match self.view {View::People=>"人物与预设",View::Actors=>"场景人物",View::Scene=>"场景",View::Props=>"道具",View::Music=>"乐库"});
        text(GetDlgItem(self.hwnd,331),match self.view {
            View::People=>if connected {"先选择人物，再选择或编辑预设；需要时可加入场景"}else{"进入拍摄或 Live 场景后，读取人物目录并选择预设"},
            View::Actors=>"选择场景中的人物，查看状态并调整显隐或位置",
            View::Scene=>"先扫描场景，再选择节点；下方可设置背景与舞台",
            View::Props=>"先扫描场景，再选择道具并调整显隐",
            View::Music=>"探索游戏音乐，试听喜欢的版本并导出",
        });
        text(GetDlgItem(self.hwnd,332),if people {"1   选择人物"}else if actors_view {"当前场景人物"}else if music_view {"歌曲列表"}else if scene {"1   选择场景节点"}else{"1   选择道具"});
        text(GetDlgItem(self.hwnd,333),if people {"2   选择预设"}else if actors_view {"2   人物详情"}else if music_view {"歌曲详情"}else{"2   查看并操作"});
        text(GetDlgItem(self.hwnd,334),if people {if self.editing {"编辑人物预设"}else{"3   加入与定位"}}else if actors_view {"3   调整人物"}else if scene {"背景与摄影舞台"}else if music_view {"试听播放"}else{"道具操作"});
        EnableWindow(GetDlgItem(self.hwnd,133),(!self.music_busy) as i32);
        let exportable=self.songs.iter().any(|s|text_value(s,"name")==self.music_selected&&!self.music_selected.is_empty());
        EnableWindow(GetDlgItem(self.hwnd,134),(exportable&&!self.music_busy) as i32);
        EnableWindow(GetDlgItem(self.hwnd,135),(exportable&&!self.music_busy) as i32);
        EnableWindow(GetDlgItem(self.hwnd,136),self.preview.open as i32);
        EnableWindow(GetDlgItem(self.hwnd,137),self.preview.open as i32);
        EnableWindow(GetDlgItem(self.hwnd,138),self.preview.open as i32);
        text(GetDlgItem(self.hwnd,136),if self.preview.paused {"继续"}else{"暂停"});
        text(GetDlgItem(self.hwnd,336),&format!("试听音量：{}%",self.preview.volume));
        text(GetDlgItem(self.hwnd,117),if self.cross_character {"跨人物部件：开"}else{"跨人物部件：关"});
        let presets=self.snapshot["presets"].as_array();
        let has_preset=presets.is_some_and(|ps|ps.iter().any(|p|text_value(p,"id")==self.selected_preset&&!self.selected_preset.is_empty()));
        let is_custom=presets.is_some_and(|ps|ps.iter().any(|p|text_value(p,"id")==self.selected_preset&&!self.selected_preset.is_empty()&&!p["builtin"].as_bool().unwrap_or(false)));
        let actor_owned=self.snapshot["actors"].as_array().is_some_and(|xs|xs.iter().any(|a|a["instance"].as_i64()==Some(self.selected_actor)&&a["owned"].as_bool().unwrap_or(false)));
        let actor_pose_supported=self.snapshot["actors"].as_array().is_some_and(|xs|xs.iter().any(|a|a["instance"].as_i64()==Some(self.selected_actor)&&a["pose_supported"].as_bool().unwrap_or(false)));
        let actor_resource=self.snapshot["scene_resources"].as_array().and_then(|rs|rs.iter().find(|r|r["kind"]==0&&r["instance"].as_i64()==Some(self.selected_actor)));
        text(GetDlgItem(self.hwnd,112),if actor_resource.is_some_and(|r|r["hidden"].as_bool().unwrap_or(false)){"显示人物"}else{"隐藏人物"});
        let selected_unlocked=self.resources.iter().find(|r|Some((r["scene"].as_i64().unwrap_or(0),r["instance"].as_i64().unwrap_or(0)))==self.selected_resource).is_some_and(|r|!r["locked"].as_bool().unwrap_or(false));
        let resources_on=self.snapshot["resource_enabled"].as_bool().unwrap_or(false);
        let creation_on=self.snapshot["creation_enabled"].as_bool().unwrap_or(false);
        let handoff_available=self.snapshot["motion_handoff_available"].as_bool().unwrap_or(false);
        let handoff_requested=self.snapshot["motion_handoff_requested"].as_bool().unwrap_or(false);
        let handoff_released=self.snapshot["motion_handoff_released"].as_bool().unwrap_or(false);
        let resource_pending=self.snapshot["resource_enable_pending"].as_bool().unwrap_or(false);
        let free_camera=self.snapshot["free_camera_active"].as_bool().unwrap_or(false);
        text(GetDlgItem(self.hwnd,140),if free_camera {"关闭自由视角"}else{"开启自由视角"});
        EnableWindow(GetDlgItem(self.hwnd,140),((connected&&!self.snapshot["busy"].as_bool().unwrap_or(false))||free_camera) as i32);
        text(GetDlgItem(self.hwnd,127),if handoff_released {"交还 Motion 资源"}else if handoff_requested {"等待 Motion 释放"}else{"让 Motion 释放资源"});
        for (id,enabled) in [(101,connected),(102,connected),(103,ready),(105,connected&&(no_conflict||handoff_available||resource_pending||resources_on)),(127,connected&&handoff_available&&(!handoff_requested||handoff_released)),
            (104,ready&&creation_on&&has_preset&&self.selected_reference!=0&&!self.editing),
            (111,connected&&writable&&is_custom&&!self.editing),(118,has_preset&&!self.editing),
            (119,connected&&!self.selected_character.is_empty()&&!self.editing),
            (110,connected&&writable&&self.editing),(112,connected&&resources_on&&no_conflict&&actor_resource.is_some_and(|r|!r["locked"].as_bool().unwrap_or(false))),
            (113,ready&&actor_owned),(114,ready&&actor_pose_supported),
            (122,ready&&actor_pose_supported),(123,ready&&actor_pose_supported),(124,ready&&actor_pose_supported),(125,ready&&actor_pose_supported),
            (109,connected&&resources_on&&no_conflict&&selected_unlocked),
            (115,connected&&resources_on&&no_conflict),(116,connected&&resources_on&&no_conflict),(120,connected)] {
            EnableWindow(GetDlgItem(self.hwnd,id),enabled as i32);
        }
        text(GetDlgItem(self.hwnd,103),if creation_on {"关闭人物加载"}else{"启用人物加载"});
        text(GetDlgItem(self.hwnd,105),if resource_pending {"取消等待"}else if resources_on {"关闭资源控制"}else{"启用资源控制"});
        let status=text_value(&self.snapshot,"status");
        let notice=text_value(&self.snapshot,"notice");
        let store_notice=text_value(&self.snapshot,"store_notice");
        let catalog_notice=text_value(&self.snapshot,"catalog_notice");
        let resource_status=text_value(&self.snapshot,"resource_status");
        let stage_status=text_value(&self.snapshot,"stage_status");
        let camera_status=text_value(&self.snapshot,"free_camera_status");
        if music_view { text(self.status,&self.music_notice); }
        else {
            let contextual=if people {catalog_notice}else if scene {stage_status}else{resource_status};
            let primary=if !notice.is_empty(){notice}else if !admitted {"等待进入拍摄 / Live 场景"}else if !contextual.is_empty(){contextual}else{status};
            let secondary=if people {store_notice}else{camera_status};
            let message=if !secondary.is_empty()&&secondary!=primary {format!("{}   ·   {}",primary,secondary)}else{primary.to_owned()};
            text(self.status,&message);
        }
        if self.selected_character.is_empty(){if let Some(first)=self.snapshot["characters"].as_array().and_then(|xs|xs.first()){self.selected_character=text_value(first,"id").to_owned();}}
        let selected_parts:Vec<String>=(0..3).map(|i|combo_selected(self.parts[i]).and_then(|n|self.part_items[i].get(n)).cloned().unwrap_or_default()).collect();
        let mut next_actors=Vec::new();let mut next_actor_labels=Vec::new();
        if let Some(actors)=self.snapshot["actors"].as_array() {for actor in actors {
            let id=actor["instance"].as_i64().unwrap_or(0);if id!=0{next_actors.push(id);next_actor_labels.push(format!("{}{}",text_value(actor,"name"),if actor["owned"].as_bool().unwrap_or(false){"  · 已加入"}else{""}));}
        }}
        if self.actors!=next_actors||self.actor_labels!=next_actor_labels {
            self.actors=next_actors;self.actor_labels=next_actor_labels;
            SendMessageW(self.actor,CB_RESETCONTENT,0,0);
            for label in &self.actor_labels {combo_add(self.actor,label);}
            if let Some(index)=self.actors.iter().position(|x|*x==self.selected_reference){SendMessageW(self.actor,CB_SETCURSEL,index,0);}
        }
        if !self.actors.contains(&self.selected_actor){self.selected_actor=0;}
        if !self.actors.contains(&self.selected_reference){self.selected_reference=0;}
        let mut next_parts:[Vec<String>;3]=std::array::from_fn(|_|Vec::new());
        if let Some(parts)=self.snapshot["resources"].as_array() {for part in parts {
            if !self.cross_character&&text_value(part,"character")!=self.selected_character{continue;}
            let i=match text_value(part,"kind") {"body"=>0,"face"=>1,"hair"=>2,_=>continue};
            next_parts[i].push(text_value(part,"resource").to_owned());
        }}
        for i in 0..3 {if self.part_items[i]!=next_parts[i] {
            self.part_items[i]=std::mem::take(&mut next_parts[i]);SendMessageW(self.parts[i],CB_RESETCONTENT,0,0);
            for name in &self.part_items[i]{combo_add(self.parts[i],name);}
            if let Some(index)=self.part_items[i].iter().position(|x|x==&selected_parts[i]){SendMessageW(self.parts[i],CB_SETCURSEL,index,0);}
        }}
        if admitted&&!self.stage_initialized {self.stage_choice=self.snapshot["stage_choice"].as_u64().unwrap_or(0).min(3) as usize;self.stage_initialized=true;}
        SendMessageW(self.stage,CB_SETCURSEL,self.stage_choice,0);
        if !music_view {self.music_rows.clear();}
        let mut left_rows=Vec::new();let mut right_rows=Vec::new();
        self.characters.clear();self.presets.clear();self.resources.clear();
        match self.view {
            View::People=>{
                if let Some(chars)=self.snapshot.get("characters").and_then(Value::as_array) {
                    let filter=get_text(GetDlgItem(self.hwnd,220)).trim().to_lowercase();
                    for c in chars {let id=text_value(c,"id");let name=text_value(c,"name");if !filter.is_empty()&&!name.to_lowercase().contains(&filter)&&!id.to_lowercase().contains(&filter){continue;}self.characters.push(id.to_string());left_rows.push(name.to_owned());}
                }
                if self.selected_character.is_empty(){if let Some(c)=self.characters.first(){self.selected_character=c.clone();}}
                if let Some(presets)=self.snapshot.get("presets").and_then(Value::as_array) {
                    for p in presets.iter().filter(|p|text_value(p,"character")==self.selected_character) {
                        self.presets.push(p.clone());right_rows.push(text_value(p,"name").to_owned());
                    }
                }
            }
            View::Actors=>{
                if let Some(items)=self.snapshot["actors"].as_array(){for actor in items {
                    if actor["instance"].as_i64().unwrap_or(0)==0 {continue;}
                    let name=text_value(actor,"name");let owned=actor["owned"].as_bool().unwrap_or(false);
                    list_add(self.left,&format!("{}  · {}",name,if owned {"本插件新增"}else{"游戏人物"}));
                }}
                let info=self.snapshot["actors"].as_array().and_then(|xs|xs.iter().find(|a|a["instance"].as_i64()==Some(self.selected_actor))).map(|actor|{
                    let owned=actor["owned"].as_bool().unwrap_or(false);
                    let hidden=self.snapshot["scene_resources"].as_array().and_then(|rs|rs.iter().find(|r|r["kind"]==0&&r["instance"].as_i64()==Some(self.selected_actor))).map(|r|if r["hidden"].as_bool().unwrap_or(false){"已隐藏"}else{"显示中"}).unwrap_or("未扫描，请点击扫描场景资源");
                    let part=|key|{let s=text_value(actor,key);if s.is_empty(){"未读取到"}else{s}};
                    format!("人物：{}    类型：{}    模型显隐：{}\r\n\r\n身体：{}\r\n脸部：{}\r\n头发：{}\r\n\r\n{}",text_value(actor,"name"),if owned {"本插件新增"}else{"游戏人物"},hidden,part("bodyAsset"),part("faceAsset"),part("hairAsset"),if owned {"可以设置位置、朝向或移除。"}else if actor["pose_supported"].as_bool().unwrap_or(false){"可调整位置和朝向；移除仅用于本插件新增人物。"}else{"人物身份未确认；请重新扫描后再调整位置。"})
                }).unwrap_or_else(||"选择左侧人物查看详情。\r\n\r\n若列表为空，请扫描场景资源。".into());
                text(GetDlgItem(self.hwnd,320),&info);
            }
            View::Scene|View::Props=>{
                if let Some(resources)=self.snapshot.get("scene_resources").and_then(Value::as_array) {
                    for r in resources {
                        let kind=r.get("kind").and_then(Value::as_i64).unwrap_or(-1);
                        if (self.view==View::Props&&kind!=1)||(self.view==View::Scene&&kind<2){continue;}
                        let locked=r.get("locked").and_then(Value::as_bool).unwrap_or(false);
                        let hidden=r.get("hidden").and_then(Value::as_bool).unwrap_or(false);
                        let depth=r.get("depth").and_then(Value::as_u64).unwrap_or(0).min(10) as usize;
                        let line=format!("{}{} {}{}","  ".repeat(depth),if hidden {"[隐藏]"} else {"[显示]"},text_value(r,"name"),if locked {" [锁定]"} else {""});
                        self.resources.push(r.clone());left_rows.push(line);
                    }
                }
                let description=self.selected_resource.and_then(|(scene,instance)|self.resources.iter().find(|r|r["scene"].as_i64()==Some(scene)&&r["instance"].as_i64()==Some(instance))).map(|r|format!("名称：{}\r\n\r\n状态：{}\r\n\r\n{}",text_value(r,"name"),if r["hidden"].as_bool().unwrap_or(false){"已隐藏"}else{"显示中"},if r["locked"].as_bool().unwrap_or(false){"此节点受保护，不能修改。"}else{"点击下方按钮切换显隐。"})).unwrap_or_else(||"选择左侧资源以查看详情。\r\n\r\n请先扫描资源并启用资源控制。".into());
                text(GetDlgItem(self.hwnd,320),&description);
            }
            View::Music=>{
                self.music_visible.clear();
                let mut rows=Vec::new();
                let filter=get_text(GetDlgItem(self.hwnd,221)).trim().to_lowercase();
                for (i,song) in self.songs.iter().enumerate() {
                    let name=text_value(song,"name");
                    let title=text_value(song,"title");
                    let performer=text_value(song,"performer");
                    if !filter.is_empty()&&!name.to_lowercase().contains(&filter)&&!title.to_lowercase().contains(&filter)&&!performer.to_lowercase().contains(&filter){continue;}
                    self.music_visible.push(i);
                    let display=if title.is_empty(){name.trim_end_matches(".awb")}else{title};
                    let kind=if text_value(song,"kind")=="live" {"Live"}else{"通用"};
                    let artist=if performer.is_empty(){"未知演唱"}else{performer};
                    let cached=if song["cached"].as_bool()==Some(true){"已缓存"}else{"可下载"};
                    let variant=name.trim_start_matches("sud_music_general_").trim_start_matches("sud_music_live_").trim_end_matches(".awb");
                    if only {rows.push(format!("{}\t{}  ·  {}  ·  {}\t{}",display,artist,kind,variant,cached));}
                    else {rows.push(format!("{}  {}  {}",display,artist,name));}
                }
                if rows!=self.music_rows {
                    let top=if filter==self.music_filter {SendMessageW(self.left,0x018e,0,0).max(0) as usize}else{0};
                    SendMessageW(self.left,0x000b,0,0);
                    SendMessageW(self.left,LB_RESETCONTENT,0,0);
                    for row in &rows {list_add(self.left,row);}
                    if let Some(index)=self.music_visible.iter().position(|i|text_value(&self.songs[*i],"name")==self.music_selected){SendMessageW(self.left,LB_SETCURSEL,index,0);}
                    if !rows.is_empty(){SendMessageW(self.left,0x0197,top.min(rows.len()-1),0);}
                    SendMessageW(self.left,0x000b,1,0);InvalidateRect(self.left,null(),1);
                    self.music_rows=rows;
                }
                self.music_filter=filter;
                let wanted=self.music_visible.iter().position(|i|text_value(&self.songs[*i],"name")==self.music_selected);
                if selected(self.left)!=wanted {SendMessageW(self.left,LB_SETCURSEL,wanted.unwrap_or(usize::MAX),0);}
                let desc=self.songs.iter().find(|s|text_value(s,"name")==self.music_selected).map(|s|{
                    let title=text_value(s,"title");let artist=text_value(s,"performer");let name=text_value(s,"name");
                    let wrapped=name.chars().collect::<Vec<_>>().chunks(35).map(|c|c.iter().collect::<String>()).collect::<Vec<_>>().join("\r\n");
                    format!("{}\r\n\r\n演唱  {}\r\n版本  {}\r\n状态  {}\r\n\r\n资源名称\r\n{}\r\n\r\n未缓存的歌曲会在试听或导出时下载。",if title.is_empty(){"未匹配曲名"}else{title},if artist.is_empty(){"未知"}else{artist},if text_value(s,"kind")=="live"{"Live"}else{"通用"},if s["cached"].as_bool()==Some(true){"已缓存"}else{"可下载"},wrapped)
                }).unwrap_or_else(||"选择一首歌曲，查看版本信息并开始试听。".into());
                text(GetDlgItem(self.hwnd,320),&desc);
            }
        }
        if !music_view {
            let filter=if people {get_text(GetDlgItem(self.hwnd,220)).trim().to_lowercase()}else{String::new()};
            let changed_view=self.list_view!=Some(self.view);
            let selected_left=match self.view {
                View::People=>self.characters.iter().position(|x|x==&self.selected_character),
                View::Actors=>self.actors.iter().position(|x|*x==self.selected_actor),
                View::Scene|View::Props=>self.selected_resource.and_then(|(scene,instance)|self.resources.iter().position(|r|r["scene"].as_i64()==Some(scene)&&r["instance"].as_i64()==Some(instance))),
                View::Music=>None,
            };
            let selected_right=if people {self.presets.iter().position(|x|text_value(x,"id")==self.selected_preset)}else{None};
            sync_resource_list(self.left,left_rows,&mut self.left_rows,selected_left,changed_view||filter!=self.resource_filter);
            sync_resource_list(self.right,right_rows,&mut self.right_rows,selected_right,changed_view);
            self.list_view=Some(self.view);self.resource_filter=filter;
        }
    }
    unsafe fn command(&mut self,id:u16,notification:u16) {
        if matches!(id,101..=105|109..=116|140|209)&&SENDER.get().is_none(){text(self.status,"尚未连接游戏插件，操作未提交。");return;}
        match id {
            140=>send(json!({"version":1,"action":"free_camera","enabled":!self.snapshot["free_camera_active"].as_bool().unwrap_or(false)})),
            101=>send(json!({"version":1,"action":"scan"})),
            102=>send(json!({"version":1,"action":"scan_resources"})),
            103=>send(json!({"version":1,"action":"enable_creation","enabled":!self.snapshot["creation_enabled"].as_bool().unwrap_or(false)})),
            104=>{
                let instance=self.selected_reference;
                if !self.selected_preset.is_empty()&&instance!=0 {send(json!({"version":1,"action":"spawn","preset_id":self.selected_preset,"instance":instance}));}
            }
            105=>send(json!({"version":1,"action":"enable_resources","enabled":!self.snapshot["resource_enabled"].as_bool().unwrap_or(false)&&!self.snapshot["resource_enable_pending"].as_bool().unwrap_or(false)})),
            106=>{self.view=View::People;self.selected_resource=None;self.render();},
            127=>send(json!({"version":1,"action":"set_visual_handoff","enabled":!self.snapshot["motion_handoff_requested"].as_bool().unwrap_or(false)})),
            126=>{self.view=View::Actors;self.selected_resource=None;self.render();},
            107=>{self.view=View::Scene;self.selected_resource=None;self.render();},
            108=>{self.view=View::Props;self.selected_resource=None;self.render();},
            132=>{self.view=View::Music;self.render();},
            133=>{
                if !self.music_busy {self.music_busy=true;self.music_notice="正在读取游戏歌曲清单…".into();self.render();
                    let hwnd=self.hwnd as usize;std::thread::spawn(move||{
                        let result=music::scan();if let Some(slot)=MUSIC_RESULT.get(){if let Ok(mut s)=slot.lock(){*s=Some(MusicResult::Scan(result));}}
                        unsafe {PostMessageW(hwnd as Handle,WM_SNAPSHOT,0,0);}
                    });
                }
            },
            134=>{
                if !self.music_busy && self.songs.iter().any(|s|text_value(s,"name")==self.music_selected&&!self.music_selected.is_empty()) {
                    if let Some(path)=choose_audio_path(self.hwnd,&self.music_selected) {
                        if path.exists(){self.music_notice="目标文件已存在，请选择其他文件名。".into();self.render();return;}
                        let name=self.music_selected.clone();self.music_busy=true;self.music_notice="正在准备音频；未缓存时会先下载…".into();self.render();let hwnd=self.hwnd as usize;
                        std::thread::spawn(move||{let result=music::export(&name,&path);if let Some(slot)=MUSIC_RESULT.get(){if let Ok(mut s)=slot.lock(){*s=Some(MusicResult::Export(name,result));}}unsafe {PostMessageW(hwnd as Handle,WM_SNAPSHOT,0,0);}});
                    }
                }
            },
            135=>{
                if !self.music_busy&&self.songs.iter().any(|s|text_value(s,"name")==self.music_selected&&!self.music_selected.is_empty()) {
                    if let Err(e)=self.preview.stop(){self.music_notice=e;self.render();return;}
                    let name=self.music_selected.clone();let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
                    let path=std::env::temp_dir().join(format!("gakuactors-preview-{}-{stamp}.wav",std::process::id()));
                    self.preview_pending=Some(path.clone());self.music_busy=true;self.music_notice="正在准备试听；未缓存时会先下载…".into();self.render();
                    let hwnd=self.hwnd as usize;let alive=self.preview_alive.clone();
                    std::thread::spawn(move||{let result=music::export(&name,&path);
                        if !alive.load(Ordering::Acquire){let _=std::fs::remove_file(&path);return;}
                        if let Some(slot)=MUSIC_RESULT.get(){if let Ok(mut s)=slot.lock(){*s=Some(MusicResult::Preview(name,path.clone(),result));}}
                        if unsafe{PostMessageW(hwnd as Handle,WM_SNAPSHOT,0,0)}==0{let _=std::fs::remove_file(path);}
                    });
                }
            },
            136=>{if let Err(e)=self.preview.toggle_pause(){self.music_notice=e;}self.render();},
            137=>{if let Err(e)=self.preview.stop(){self.music_notice=e;}else{self.music_notice="试听已停止。".into();}self.render();},
            109=>{
                if let Some((scene,instance))=self.selected_resource {
                    if let Some(r)=self.resources.iter().find(|r|r["scene"].as_i64()==Some(scene)&&r["instance"].as_i64()==Some(instance)) {
                        send(json!({"version":1,"action":"hide_resource","scene":r["scene"],"instance":r["instance"],"kind":r["kind"],"generation":r["generation"],"hidden":!r["hidden"].as_bool().unwrap_or(false)}));
                    }
                }
            }
            110=>{
                let values:Vec<String>=(0..3).map(|i|combo_selected(self.parts[i]).and_then(|n|self.part_items[i].get(n)).cloned().unwrap_or_default()).collect();
                let name=get_text(self.name);
                if !name.is_empty()&&!self.selected_character.is_empty()&&values.iter().all(|x|!x.is_empty()) {
                    let id=if self.edit_id.is_empty(){format!("user-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos())}else{self.edit_id.clone()};
                    send(json!({"version":1,"action":"save_preset","preset":{"id":id,"name":name,"character":self.selected_character,"body":"","head":"","factory":"","bodyAsset":values[0],"faceAsset":values[1],"hairAsset":values[2]}}));
                    self.editing=false;self.render();
                } else {text(self.status,"请填写预设名称，并选择身体、脸部和头发后再保存。");}
            }
            112=>{
                if let Some(r)=self.snapshot["scene_resources"].as_array().and_then(|rs|rs.iter().find(|r|r["kind"]==0&&r["instance"].as_i64()==Some(self.selected_actor))) {
                    send(json!({"version":1,"action":"hide_resource","scene":r["scene"],"instance":r["instance"],"kind":r["kind"],"generation":r["generation"],"hidden":!r["hidden"].as_bool().unwrap_or(false)}));
                }
            }
            113=>if self.selected_actor!=0{send(json!({"version":1,"action":"remove","instance":self.selected_actor}));},
            114=>{
                let values:Vec<Option<f64>>=self.position.iter().map(|p|get_text(*p).parse::<f64>().ok()).collect();
                if self.selected_actor!=0&&values.iter().all(|v|v.is_some_and(|n|n.is_finite()&&n.abs()<=10000.0)){send(json!({"version":1,"action":"move","instance":self.selected_actor,"position":[values[0],values[1],values[2]]}));}
                else {text(self.status,"请填写有效的 X、Y、Z 世界坐标（范围 ±10000）。");}
            }
            116=>send(json!({"version":1,"action":"camera_color","enabled":false,"color":[0,1,0,1]})),
            117=>{self.cross_character=!self.cross_character;self.render();},
            118=>if let Some(p)=self.presets.iter().find(|p|text_value(&p,"id")==self.selected_preset).cloned() {
                self.editing=true;
                self.edit_id=if p["builtin"].as_bool().unwrap_or(false){String::new()}else{text_value(&p,"id").to_owned()};
                text(self.name,text_value(&p,"name"));
                let mut values=[text_value(&p,"bodyAsset").to_owned(),text_value(&p,"faceAsset").to_owned(),text_value(&p,"hairAsset").to_owned()];
                if values.iter().any(String::is_empty) {
                    if let Some(m)=self.snapshot["models"].as_array().and_then(|ms|ms.iter().find(|m|text_value(m,"factory")==self.selected_preset||(!text_value(&p,"factory").is_empty()&&text_value(m,"factory")==text_value(&p,"factory")))) {
                        values=[text_value(m,"body").to_owned(),text_value(m,"face").to_owned(),text_value(m,"hair").to_owned()];
                    }
                }
                for i in 0..3 {if let Some(index)=self.part_items[i].iter().position(|x|x==&values[i]){SendMessageW(self.parts[i],CB_SETCURSEL,index,0);}}
                self.render();
            },
            119=>{self.edit_id.clear();self.editing=true;text(self.name,"");for part in self.parts {SendMessageW(part,CB_SETCURSEL,usize::MAX,0);}self.render();},
            121=>{self.editing=false;self.render();},
            120=>send(json!({"version":1,"action":"select_stage","choice":self.stage_choice})),
            122|123=>if self.selected_actor!=0 {send(json!({"version":1,"action":"turn","instance":self.selected_actor,"degrees":if id==122 {-15} else {15}}));},
            124=>if self.selected_actor!=0 {self.reset_wait_actor=self.selected_actor;self.reset_wait_revision=Some(self.snapshot["pose_reset_revision"].as_u64().unwrap_or(0));send(json!({"version":1,"action":"reset_position","instance":self.selected_actor}));},
            125=>if let Ok(degrees)=get_text(GetDlgItem(self.hwnd,216)).parse::<f64>() {if degrees.is_finite()&&(0.0..=360.0).contains(&degrees) {send(json!({"version":1,"action":"set_yaw","instance":self.selected_actor,"degrees":degrees}));} else {text(self.status,"请输入 0 到 360 度的朝向。");}} else {text(self.status,"请输入 0 到 360 度的朝向。");},
            205 if notification==1=>if let Some(i)=combo_selected(self.actor){if let Some(id)=self.actors.get(i){self.selected_reference=*id;}self.render();},
            303..=305 if notification==0x0300 && self.position.contains(&GetFocus())=>{
                let valid_actor=self.snapshot["actors"].as_array().is_some_and(|xs|xs.iter().any(|a|a["instance"].as_i64()==Some(self.selected_actor)&&a["pose_supported"].as_bool().unwrap_or(false)));
                let ready=self.snapshot["admitted"].as_bool().unwrap_or(false)&&!self.snapshot["playing"].as_bool().unwrap_or(false)&&!self.snapshot["busy"].as_bool().unwrap_or(false)&&!self.snapshot["faulted"].as_bool().unwrap_or(false);
                let values:Vec<Option<f64>>=self.position.iter().map(|p|get_text(*p).parse::<f64>().ok()).collect();
                if valid_actor&&ready&&values.iter().all(|v|v.is_some_and(|n|n.is_finite()&&n.abs()<=10000.0)) {
                    self.reset_wait_revision=None;send(json!({"version":1,"action":"move","instance":self.selected_actor,"position":[values[0],values[1],values[2]]}));
                }
            },
            220 if notification==0x0300=>{self.render();},
            221 if notification==0x0300=>{self.render();},
            209 if notification==1=>if let Some(i)=combo_selected(self.stage){self.stage_choice=i;},
            201 if notification==1=>{
                if let Some(i)=selected(self.left) {
                    if self.view==View::People {if let Some(c)=self.characters.get(i){self.selected_character=c.clone();self.selected_preset.clear();self.edit_id.clear();self.editing=false;self.thumbnail(ThumbResult{id:String::new(),status:"选择预设".into(),bitmap:None,head_status:"选择预设".into(),head_bitmap:None});self.render();}}
                    else if self.view==View::Actors {if let Some(id)=self.actors.get(i){if self.selected_actor!=*id{self.reset_wait_revision=None;}self.selected_actor=*id;
                        if let Some(actor)=self.snapshot["actors"].as_array().and_then(|xs|xs.iter().find(|a|a["instance"].as_i64()==Some(self.selected_actor))) {
                            for i in 0..3 {if let Some(value)=actor["position"][i].as_f64(){text(self.position[i],&format!("{value:.2}"));}}
                        }self.render();}}
                    else if self.view==View::Music {if let Some(index)=self.music_visible.get(i){self.music_selected=text_value(&self.songs[*index],"name").to_owned();self.render();}}
                    else if let Some(r)=self.resources.get(i){self.selected_resource=Some((r["scene"].as_i64().unwrap_or(0),r["instance"].as_i64().unwrap_or(0)));self.render();}
                }
            }
            202 if notification==1=>if let Some(i)=selected(self.right){if let Some(p)=self.presets.get(i){
                self.selected_preset=text_value(p,"id").to_owned();
                self.thumbnail(ThumbResult{id:self.selected_preset.clone(),status:"读取中…".into(),bitmap:None,head_status:"读取中…".into(),head_bitmap:None});
                send(json!({"version":1,"action":"load_thumbnail","preset_id":self.selected_preset}));
                self.render();
            }},
            _=>{}
        }
    }
}
fn start_worker(hwnd:Handle,mut client:pipe::Client) {
    let (tx,rx)=mpsc::channel::<Value>();let _=SENDER.set(tx);
    let latest=Arc::new(Mutex::new(Value::Null));let _=LATEST.set(latest.clone());
    let thumbs=Arc::new(Mutex::new(None));let _=THUMB.set(thumbs.clone());let target=hwnd as usize;
    std::thread::spawn(move||{let mut pending:Option<String>=None;let mut held:Option<Value>=None;loop {
        let mut request=if let Some(value)=held.take(){value}else{match rx.recv_timeout(Duration::from_millis(750)){
            Ok(v)=>v,
            Err(mpsc::RecvTimeoutError::Timeout)=>if pending.is_some(){json!({"version":1,"action":"thumbnail_status"})}else{json!({"version":1,"action":"snapshot"})},
            Err(_)=>break}};
        if request["action"]=="move" {while let Ok(next)=rx.try_recv(){
            if next["action"]=="move"&&next["instance"]==request["instance"] {request=next;}else{held=Some(next);break;}
        }}
        if request["action"]=="load_thumbnail"{pending=request["preset_id"].as_str().map(str::to_owned);}
        match client.request(&request) {
            Ok(mut reply)=>{
                if request["action"]=="load_thumbnail"&&reply["accepted"]!=true {
                    if let Some(id)=pending.take(){if let Ok(mut v)=thumbs.lock(){*v=Some(ThumbResult{id,status:"请求未被接受".into(),bitmap:None,head_status:"请求未被接受".into(),head_bitmap:None});}}
                }
                if let Some(t)=reply.get("thumbnail").and_then(Value::as_object) {
                    let id=t.get("id").and_then(Value::as_str).unwrap_or("").to_owned();
                    let mut status=t.get("status").and_then(Value::as_str).unwrap_or("").to_owned();
                    let data=t.get("data").and_then(Value::as_str).unwrap_or("");
                    if pending.as_deref()==Some(id.as_str())&&!id.is_empty()&&status!="读取中" {
                        let decoded=if data.is_empty(){None}else{match thumbnail::decode(data){Ok(image)=>Some(image),Err(_)=>{status="缩略图解码失败".into();None}}};
                        let mut head_status=t.get("head_status").and_then(Value::as_str).unwrap_or("头部图未返回").to_owned();
                        let head_data=t.get("head_data").and_then(Value::as_str).unwrap_or("");
                        let head_decoded=if head_data.is_empty(){None}else{match thumbnail::decode(head_data){Ok(image)=>Some(image),Err(_)=>{head_status="头部图片解码失败".into();None}}};
                        if let Ok(mut v)=thumbs.lock(){*v=Some(ThumbResult{id,status,bitmap:decoded,head_status,head_bitmap:head_decoded});}
                        pending=None;
                    }
                }
                if let Some(map)=reply.as_object_mut(){map.remove("thumbnail");}
                if reply["accepted"]==false {
                    if let Some(snapshot)=reply.get_mut("snapshot").and_then(Value::as_object_mut){snapshot.insert("notice".into(),Value::String("操作未通过校验或队列已满".into()));}
                }
                if let Ok(mut state)=latest.lock(){*state=reply;}unsafe{PostMessageW(target as Handle,WM_SNAPSHOT,0,0);}
            },
            Err(_)=>{if let Ok(mut state)=latest.lock(){*state=json!({"snapshot":{"status":"游戏插件连接已断开"}});}unsafe{PostMessageW(target as Handle,WM_SNAPSHOT,0,0);}break;}
        }
    }});
}

fn rgb(r:u8,g:u8,b:u8)->u32 {(r as u32)|((g as u32)<<8)|((b as u32)<<16)}
unsafe fn rounded(dc:Handle,rect:Rect,color:u32,border:u32,radius:i32) {
    let brush=CreateSolidBrush(color);let pen=CreatePen(0,1,border);
    let old_brush=SelectObject(dc,brush);let old_pen=SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
    SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);
}
// Positions are in logical units; the fixed font scale stays readable while the
// list and detail panes grow with the client area.
fn music_panels(w:i32,h:i32)->((i32,i32,i32,i32),(i32,i32,i32,i32),(i32,i32,i32,i32)) {
    let right_w=(w*35/100).clamp(310,560);
    let left_w=w-96-right_w;
    let player_y=h-210;
    ((32,135,left_w,player_y-155),(48+left_w,135,right_w,player_y-155),(32,player_y,w-64,160))
}
unsafe fn music_client_size(hwnd:Handle)->(i32,i32) {
    let mut r=Rect{left:0,top:0,right:0,bottom:0};GetClientRect(hwnd,&mut r);
    let scale=*SCALE.get().unwrap_or(&1.0);
    ((r.right as f64/scale).round() as i32,(r.bottom as f64/scale).round() as i32)
}
unsafe fn layout_music(hwnd:Handle) {
    if GetDlgItem(hwnd,201).is_null(){return;}
    let (w,h)=music_client_size(hwnd);if w<1||h<1{return;}
    let (left,right,player)=music_panels(w,h);let scale=*SCALE.get().unwrap_or(&1.0);
    let p=|n:i32|(n as f64*scale).round() as i32;
    let seek_x=420;let export_x=w-218;let volume_x=(w-498).max(620);let seek_w=(volume_x-seek_x-30).max(100);
    for (id,x,y,cw,ch) in [(330,48,32,w-270,45),(331,48,80,w-270,28),(133,w-210,48,150,42),
        (332,56,154,left.2-48,30),(323,56,190,left.2-48,26),(221,56,219,left.2-48,35),
        (201,56,265,left.2-48,(left.3-154).max(30)),
        (333,right.0+30,154,right.2-60,30),(320,right.0+30,206,right.2-60,(right.3-95).max(30)),
        (334,56,player.1+19,300,28),(135,56,player.1+68,100,42),(136,168,player.1+68,100,42),(137,280,player.1+68,100,42),
        (335,seek_x,player.1+19,seek_w,28),(138,seek_x,player.1+60,seek_w,38),
        (336,volume_x,player.1+19,export_x-volume_x-20,28),(139,volume_x,player.1+60,export_x-volume_x-40,38),
        (134,export_x,player.1+68,174,42),(315,48,h-35,w-96,30)] {
        let child=GetDlgItem(hwnd,id);if !child.is_null(){MoveWindow(child,p(x),p(y),p(cw.max(1)),p(ch.max(1)),1);}
    }
}
unsafe fn paint_window(hwnd:Handle) {
    let scale=*SCALE.get().unwrap_or(&1.0);let p=|n:i32|(n as f64*scale).round() as i32;
    let rect=|x,y,w,h|Rect{left:p(x),top:p(y),right:p(x+w),bottom:p(y+h)};
    let mut paint:PaintStruct=std::mem::zeroed();let dc=BeginPaint(hwnd,&mut paint);
    if !dc.is_null() {
        let mut client=Rect{left:0,top:0,right:0,bottom:0};GetClientRect(hwnd,&mut client);
        FillRect(dc,&client,*BACK_BRUSH.get().unwrap_or(&0) as Handle);
        if *MUSIC_ONLY.get().unwrap_or(&false) {
            let (w,h)=music_client_size(hwnd);let (left,right,player)=music_panels(w,h);
            for r in [rect(left.0,left.1,left.2,left.3),rect(right.0,right.1,right.2,right.3),rect(player.0,player.1,player.2,player.3)] {rounded(dc,r,rgb(255,255,255),rgb(226,229,237),p(18));}
            EndPaint(hwnd,&paint);return;
        }
        let sidebar=CreateSolidBrush(rgb(23,34,57));FillRect(dc,&rect(0,0,210,900),sidebar);DeleteObject(sidebar);
        for r in [rect(230,150,350,390),rect(596,150,806,390),rect(230,558,1172,236)] {rounded(dc,r,rgb(255,255,255),rgb(220,227,238),p(13));}
        SetBkMode(dc,1);SetTextColor(dc,rgb(255,255,255));
        if let Some(font)=HEADING_FONT.get(){SelectObject(dc,*font as Handle);}
        let mut label=rect(25,35,160,32);DrawTextW(dc,wide("GAKUACTORS").as_ptr(),-1,&mut label,0x24);
        if let Some(font)=FONT.get(){SelectObject(dc,*font as Handle);}
        let only=*MUSIC_ONLY.get().unwrap_or(&false);
        SetTextColor(dc,rgb(195,207,224));let mut caption=rect(25,73,165,30);DrawTextW(dc,wide(if only {"游戏乐库"}else{"场景资源管理"}).as_ptr(),-1,&mut caption,0x24);
        let mut footer=rect(25,802,170,38);DrawTextW(dc,wide(if only {"检索与导出"}else{"拍摄 / Live 场景使用"}).as_ptr(),-1,&mut footer,0x24);
    }
    EndPaint(hwnd,&paint);
}
unsafe fn draw_button(item:&DrawItem) {
    let id=item.ctl_id as u16;let disabled=item.state&4!=0;let pressed=item.state&1!=0;
    let nav=(106..=108).contains(&id)||id==126||id==132;let selected=nav&&NAV_SELECTED.load(Ordering::Relaxed)==id;
    let music=*MUSIC_ONLY.get().unwrap_or(&false);
    let primary=matches!(id,104|110|125)||(music&&matches!(id,134|135));
    let (bg,border,fg)=if disabled {(rgb(239,242,247),rgb(225,230,237),rgb(157,168,183))}
        else if nav {if selected {(rgb(49,67,96),rgb(49,67,96),rgb(255,255,255))}else{(rgb(23,34,57),rgb(23,34,57),rgb(185,199,219))}}
        else if primary {if music {(rgb(89,76,181),rgb(89,76,181),rgb(255,255,255))}else{(rgb(83,109,221),rgb(83,109,221),rgb(255,255,255))}}
        else if pressed {(rgb(232,237,247),rgb(199,210,225),rgb(45,60,85))}
        else {(rgb(255,255,255),rgb(212,221,233),rgb(53,70,96))};
    let scale=*SCALE.get().unwrap_or(&1.0);rounded(item.dc,item.rect,bg,border,(8.0*scale).max(3.0) as i32);
    SetBkMode(item.dc,1);SetTextColor(item.dc,fg);if let Some(font)=FONT.get(){SelectObject(item.dc,*font as Handle);}
    let mut data=[0u16;256];let length=GetWindowTextW(item.hwnd,data.as_mut_ptr(),data.len() as i32);
    let mut rect=item.rect;DrawTextW(item.dc,data.as_ptr(),length,&mut rect,0x0001|0x0004|0x0020|0x8000);
}
unsafe fn draw_music_row(item:&DrawItem) {
    if item.item_id==u32::MAX {return;}
    let len=SendMessageW(item.hwnd,0x018a,item.item_id as usize,0);
    if !(0..=4096).contains(&len) {return;}
    let mut data=vec![0u16;len as usize+1];
    if SendMessageW(item.hwnd,0x0189,item.item_id as usize,data.as_mut_ptr() as isize)<0 {return;}
    let value=String::from_utf16_lossy(&data[..len as usize]);
    let mut parts=value.split('\t');let title=parts.next().unwrap_or("");let subtitle=parts.next().unwrap_or("");let state=parts.next().unwrap_or("");
    let scale=*SCALE.get().unwrap_or(&1.0);let p=|n:i32|(n as f64*scale).round() as i32;
    let selected=item.state&1!=0;
    let brush=CreateSolidBrush(if selected {rgb(242,240,253)}else{rgb(255,255,255)});
    FillRect(item.dc,&item.rect,brush);DeleteObject(brush);
    SetBkMode(item.dc,1);
    if let Some(font)=MUSIC_ROW_FONT.get(){SelectObject(item.dc,*font as Handle);}
    SetTextColor(item.dc,if selected {rgb(83,69,169)}else{rgb(37,41,58)});
    let mut r=Rect{left:item.rect.left+p(16),top:item.rect.top+p(6),right:item.rect.right-p(110),bottom:item.rect.top+p(31)};
    DrawTextW(item.dc,wide(title).as_ptr(),-1,&mut r,0x0020|0x0004|0x8000|0x0800);
    if let Some(font)=MUSIC_SUB_FONT.get(){SelectObject(item.dc,*font as Handle);}
    SetTextColor(item.dc,rgb(111,117,135));
    let mut sub=Rect{left:item.rect.left+p(16),top:item.rect.top+p(31),right:item.rect.right-p(110),bottom:item.rect.bottom-p(5)};
    DrawTextW(item.dc,wide(subtitle).as_ptr(),-1,&mut sub,0x0020|0x0004|0x8000|0x0800);
    SetTextColor(item.dc,if state=="已缓存" {rgb(80,132,111)}else{rgb(137,124,104)});
    let mut tag=Rect{left:item.rect.right-p(100),top:item.rect.top,right:item.rect.right-p(16),bottom:item.rect.bottom};
    DrawTextW(item.dc,wide(state).as_ptr(),-1,&mut tag,0x0002|0x0020|0x0004|0x0800);
}
unsafe fn draw_resource_row(item:&DrawItem) {
    if item.item_id==u32::MAX{return;}
    let len=SendMessageW(item.hwnd,0x018a,item.item_id as usize,0);
    if !(0..=4096).contains(&len){return;}
    let mut value=vec![0u16;len as usize+1];
    if SendMessageW(item.hwnd,0x0189,item.item_id as usize,value.as_mut_ptr() as isize)<0{return;}
    let active=item.state&1!=0;let bg=CreateSolidBrush(if active {rgb(232,237,252)}else{rgb(255,255,255)});
    FillRect(item.dc,&item.rect,bg);DeleteObject(bg);SetBkMode(item.dc,1);
    SetTextColor(item.dc,if active {rgb(54,68,150)}else{rgb(38,49,70)});
    if let Some(font)=FONT.get(){SelectObject(item.dc,*font as Handle);}
    let p=(12.0 * *SCALE.get().unwrap_or(&1.0)).round() as i32;
    let mut r=item.rect;r.left+=p;r.right-=p;
    DrawTextW(item.dc,value.as_ptr(),len as i32,&mut r,0x0020|0x0004|0x8000|0x0800);
}
unsafe extern "system" fn wndproc(hwnd: Handle, msg: u32, wp: usize, lp: isize) -> isize {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| wndproc_inner(hwnd,msg,wp,lp)))
        .unwrap_or_else(|_| DefWindowProcW(hwnd,msg,wp,lp))
}
unsafe fn wndproc_inner(hwnd: Handle, msg: u32, wp: usize, lp: isize) -> isize {
    match msg {
        0x0005=>{if *MUSIC_ONLY.get().unwrap_or(&false)&&wp!=1 {layout_music(hwnd);InvalidateRect(hwnd,null(),1);}0},
        0x0024=>{if *MUSIC_ONLY.get().unwrap_or(&false)&&lp!=0 {
            let info=&mut *(lp as *mut MinMaxInfo);let scale=*SCALE.get().unwrap_or(&1.0);
            info.min_track.x=(1000.0*scale).round() as i32;info.min_track.y=(650.0*scale).round() as i32;0
        }else{DefWindowProcW(hwnd,msg,wp,lp)}},
        0x0014=>1,
        0x000f=>{paint_window(hwnd);0},
        0x002b=>{if lp!=0 {let item=&*(lp as *const DrawItem);if item.ctl_type==2 {if *MUSIC_ONLY.get().unwrap_or(&false)&&item.ctl_id==201 {draw_music_row(item);}else{draw_resource_row(item);}}else{draw_button(item);}1}else{0}},
        0x0138=>{
            let dc=wp as Handle;let id=GetDlgCtrlID(lp as Handle);
            let music=*MUSIC_ONLY.get().unwrap_or(&false);
            SetBkMode(dc,1);SetTextColor(dc,if matches!(id,331|315){rgb(91,104,126)}else{rgb(37,48,71)});
            if music&&id==330 {if let Some(font)=MUSIC_TITLE_FONT.get(){SelectObject(dc,*font as Handle);}}
            else if matches!(id,330|332|333|334) {if let Some(font)=HEADING_FONT.get(){SelectObject(dc,*font as Handle);}}
            if matches!(id,330|331|315) {*BACK_BRUSH.get().unwrap_or(&0) as isize}else{*WHITE_BRUSH.get().unwrap_or(&0) as isize}
        },
        WM_DESTROY => {
            KillTimer(hwnd,1);
            APP.with(|cell|if let Some(app)=cell.try_borrow_mut().ok().and_then(|mut app|app.take()){
                for picture in [app.picture,app.head_picture] {let old=SendMessageW(picture,0x0172,0,0) as Handle;if !old.is_null(){DeleteObject(old);}}
            });
            if let Some(font)=FONT.get(){DeleteObject(*font as Handle);}
            if let Some(font)=HEADING_FONT.get(){DeleteObject(*font as Handle);}
            for font in [MUSIC_TITLE_FONT.get(),MUSIC_ROW_FONT.get(),MUSIC_SUB_FONT.get()].into_iter().flatten(){DeleteObject(*font as Handle);}
            if let Some(brush)=WHITE_BRUSH.get(){DeleteObject(*brush as Handle);}
            if let Some(brush)=BACK_BRUSH.get(){DeleteObject(*brush as Handle);}
            PostQuitMessage(0); 0
        }
        WM_COMMAND => {
            let id=(wp&0xffff) as u16;
            if id==111 {
                let preset=APP.with(|cell|cell.try_borrow().ok().and_then(|app|app.as_ref().and_then(|a|a.presets.iter().find(|p|text_value(p,"id")==a.selected_preset&&!p["builtin"].as_bool().unwrap_or(false)).map(|_|a.selected_preset.clone()))));
                if let Some(preset)=preset {if MessageBoxW(hwnd,wide("删除选中的自定义预设？场景人物不会被移除。").as_ptr(),wide("删除预设").as_ptr(),0x34)==6 {send(json!({"version":1,"action":"delete_preset","preset_id":preset}));}}
            } else if id==115 {
                let mut custom=[0u32;16];let mut color=ChooseColor{size:std::mem::size_of::<ChooseColor>() as u32,owner:hwnd,instance:null_mut(),rgb:0x0000ff00,custom:custom.as_mut_ptr(),flags:3,data:0,hook:null_mut(),template:null()};
                if ChooseColorW(&mut color)!=0 {let rgb=color.rgb;send(json!({"version":1,"action":"camera_color","enabled":true,"color":[(rgb&255) as f64/255.0,((rgb>>8)&255) as f64/255.0,((rgb>>16)&255) as f64/255.0,1]}));}
            } else if id==113 {
                let actor=APP.with(|cell|cell.try_borrow().ok().and_then(|app|app.as_ref().map(|a|a.selected_actor)).unwrap_or(0));
                if actor!=0&&MessageBoxW(hwnd,wide("移除当前选择的人物？").as_ptr(),wide("移除人物").as_ptr(),0x34)==6 {send(json!({"version":1,"action":"remove","instance":actor}));}
            } else {APP.with(|cell|if let Ok(mut app)=cell.try_borrow_mut(){if let Some(app)=app.as_mut(){app.command(id,((wp>>16)&0xffff) as u16)}});}
            0
        }
        WM_HSCROLL=>{
            let id=if lp!=0{GetDlgCtrlID(lp as Handle)}else{0};let code=(wp&0xffff) as u16;
            APP.with(|cell|if let Ok(mut guard)=cell.try_borrow_mut(){if let Some(app)=guard.as_mut(){
                if id==139 {
                    let value=SendMessageW(lp as Handle,0x0400,0,0).clamp(0,100) as u32;
                    let changed=value!=app.preview.volume;
                    match app.preview.set_volume(value) {
                        Ok(())=>{text(GetDlgItem(hwnd,336),&format!("试听音量：{}%",app.preview.volume));
                            app.volume_dirty|=changed;
                            if code!=5&&app.volume_dirty {match preview::save_volume(app.preview.volume){Ok(())=>app.volume_dirty=false,Err(e)=>{app.music_notice=e;text(app.status,&app.music_notice);}}}},
                        Err(e)=>{app.music_notice=e;text(app.status,&app.music_notice);SendMessageW(lp as Handle,0x0405,1,app.preview.volume as isize);},
                    }
                } else if id==138&&app.preview.open {
                    app.preview_seek_drag=code==5;
                    if code!=5 {let pos=SendMessageW(lp as Handle,0x0400,0,0).clamp(0,1000) as u64;
                        let at=(pos*app.preview.length as u64/1000) as u32;
                        if let Err(e)=app.preview.seek(at){app.music_notice=e;text(app.status,&app.music_notice);}}
                }
            }});0
        }
        WM_TIMER=>{
            if wp==1 {APP.with(|cell|if let Ok(mut guard)=cell.try_borrow_mut(){if let Some(app)=guard.as_mut(){
                if app.view==View::Music {
                    let pos=app.preview.position().unwrap_or(0);let len=app.preview.length;
                    let display=|ms:u32|format!("{:02}:{:02}",ms/60000,(ms/1000)%60);
                    text(GetDlgItem(hwnd,335),&format!("试听进度  {} / {}",display(pos),display(len)));
                    if !app.preview_seek_drag {let tick=if len>0{(pos as u64*1000/len as u64).min(1000)}else{0};SendMessageW(GetDlgItem(hwnd,138),0x0405,1,tick as isize);}
                }
            }});}0
        }
        WM_SNAPSHOT => {
            APP.with(|cell|if let Ok(mut guard)=cell.try_borrow_mut(){if let Some(app)=guard.as_mut(){
                let update=LATEST.get().and_then(|data|data.lock().ok().map(|v|v.clone()));
                if let Some(reply)=update {if let Some(snapshot)=reply.get("snapshot"){if *snapshot!=app.snapshot {app.snapshot=snapshot.clone();app.render();}}}
                if let Some(thumb)=THUMB.get().and_then(|value|value.lock().ok().and_then(|mut v|v.take())) {app.thumbnail(thumb);}
                if let Some(result)=MUSIC_RESULT.get().and_then(|value|value.lock().ok().and_then(|mut v|v.take())) {
                    app.music_busy=false;
                    match result {
                        MusicResult::Scan(Ok(songs))=>{let cached=songs.iter().filter(|s|s["cached"].as_bool()==Some(true)).count();app.music_notice=format!("检索完成：{} 个版本，{} 个已缓存。",songs.len(),cached);app.songs=songs;app.music_selected.clear();},
                        MusicResult::Scan(Err(e))=>app.music_notice=e,
                        MusicResult::Export(name,Ok(()))=>{app.music_notice="歌曲已导出。".into();if let Some(song)=app.songs.iter_mut().find(|s|text_value(s,"name")==name){song["cached"]=Value::Bool(true);}},
                        MusicResult::Export(_,Err(e))=>app.music_notice=e,
                        MusicResult::Preview(name,path,result)=>{
                            app.preview_pending=None;
                            match result {
                                Ok(()) if app.music_selected==name=>{
                                    if let Some(song)=app.songs.iter_mut().find(|s|text_value(s,"name")==name){song["cached"]=Value::Bool(true);}
                                    match app.preview.open_and_play(&path){Ok(())=>app.music_notice=format!("正在试听：{name}"),Err(e)=>{let _=std::fs::remove_file(&path);app.music_notice=e;}}
                                },
                                Ok(())=>{let _=std::fs::remove_file(&path);app.music_notice="选择已改变，请重新点击试听。".into();},
                                Err(e)=>{let _=std::fs::remove_file(&path);app.music_notice=e;},
                            }
                        },
                    }
                    app.render();
                }
            }});
            0
        }
        _ => DefWindowProcW(hwnd, msg, wp, lp),
    }
}
unsafe fn control(parent: Handle, instance: Handle, class: &str, label: &str,
    x: i32, y: i32, w: i32, h: i32, style: u32,id:u16) -> Handle {
    let is_button=class=="BUTTON";
    let style=if is_button {style|0x000b|0x00010000}else if class=="EDIT"||class=="COMBOBOX"||class=="LISTBOX"||class=="msctls_trackbar32" {style|0x00010000}else{style};
    let class = wide(class); let label = wide(label);
    let scale=*SCALE.get().unwrap_or(&1.0);let px=|v:i32| (v as f64*scale).round() as i32;
    let child = CreateWindowExW(0, class.as_ptr(), label.as_ptr(), WS_CHILD|WS_VISIBLE|style,
        px(x), px(y), px(w), px(h), parent, id as usize as Handle, instance, null_mut());
    let font=FONT.get().copied().unwrap_or_else(||GetStockObject(DEFAULT_GUI_FONT) as usize);
    if !child.is_null() { SendMessageW(child, WM_SETFONT, font, 1); }
    child
}
fn main() {
    let args:Vec<_>=std::env::args_os().skip(1).collect();
    let music_only=args.iter().any(|x|x=="--music-only");let _=MUSIC_ONLY.set(music_only);
    if args.iter().any(|x|x=="--music-volume-get") {println!("{}",preview::load_volume());return;}
    if let Some(parts)=args.windows(2).find(|p|p[0]=="--music-volume-set") {
        let value=parts[1].to_string_lossy().parse::<u32>();
        if value.ok().filter(|v|*v<=100).is_none_or(|v|preview::save_volume(v).is_err()){std::process::exit(1);}return;
    }
    if let Some(parts)=args.windows(3).find(|p|p[0]=="--music-json") {
        let result=music::scan_at(std::path::Path::new(&parts[1])).and_then(|songs|serde_json::to_vec(&songs).map_err(|e|e.to_string())).and_then(|bytes|std::fs::write(&parts[2],bytes).map_err(|e|e.to_string()));
        if result.is_err(){std::process::exit(1);}return;
    }
    if let Some(parts)=args.windows(3).find(|p|p[0]=="--music-export-dialog") {
        let name=parts[2].to_string_lossy();
        unsafe {if let Some(path)=choose_audio_path(null_mut(),&name) {
            let result=music::export_at(std::path::Path::new(&parts[1]),&name,&path);
            let message=match result {Ok(())=>"歌曲已导出。".to_string(),Err(e)=>format!("导出失败：{e}")};
            MessageBoxW(null_mut(),wide(&message).as_ptr(),wide("乐库导出").as_ptr(),0);
        }}
        return;
    }
    if let Some(parts)=args.windows(4).find(|p|p[0]=="--music-export") {
        let name=parts[2].to_string_lossy();
        if let Err(e)=music::export_at(std::path::Path::new(&parts[1]),&name,std::path::Path::new(&parts[3])) {eprintln!("{e}");std::process::exit(1);}
        return;
    }
    if let Some(path)=std::env::args_os().skip(1).collect::<Vec<_>>().windows(2).find(|pair|pair[0]=="--music-scan").map(|pair|std::path::PathBuf::from(&pair[1])) {
        match music::scan_at(&path) {Ok(songs)=>println!("songs={} cached={}",songs.len(),songs.iter().filter(|s|s["cached"].as_bool()==Some(true)).count()),Err(e)=>{eprintln!("{e}");std::process::exit(1);}}
        return;
    }
    if std::env::args().any(|x| x == "--ipc-self-test") {
        let result=(|| -> std::io::Result<()> {
            let mut client=pipe::Client::from_args()?;
            for action in ["snapshot","scan_resources"] {
                let response=client.request(&json!({"version":1,"action":action}))?;
                if response["version"]!=1||response["accepted"]!=true {return Err(std::io::Error::other("unexpected response"));}
            }
            Ok(())
        })();
        if result.is_err(){std::process::exit(1);}return;
    }
    if std::env::args().any(|x| x == "--self-test") {
        assert_eq!(std::mem::size_of::<WndClass>(), 72);
        assert_eq!(std::mem::size_of::<Msg>(), 48);
        return;
    }
    unsafe {
        SetProcessDPIAware();
        let common=InitCommonControls{size:std::mem::size_of::<InitCommonControls>() as u32,classes:0x4};
        InitCommonControlsEx(&common);
        let sw=GetSystemMetrics(0).max(640);let sh=GetSystemMetrics(1).max(480);
        let scale=((sw-80) as f64/1440.0).min((sh-100) as f64/900.0).min(1.0).max(0.5);
        let _=SCALE.set(scale);
        let face=wide("Microsoft YaHei UI");
        let font=CreateFontW(-((17.0*scale).round() as i32),0,0,0,if music_only {400}else{500},0,0,0,1,0,0,5,0,face.as_ptr());
        if !font.is_null(){let _=FONT.set(font as usize);}
        let heading=CreateFontW(-(((if music_only {19.0}else{20.0})*scale).round() as i32),0,0,0,600,0,0,0,1,0,0,5,0,face.as_ptr());
        if !heading.is_null(){let _=HEADING_FONT.set(heading as usize);}
        if music_only {
            for (slot,size,weight) in [(&MUSIC_TITLE_FONT,28.0,600),(&MUSIC_ROW_FONT,17.0,600),(&MUSIC_SUB_FONT,14.0,400)] {
                let f=CreateFontW(-((size*scale).round() as i32),0,0,0,weight,0,0,0,1,0,0,5,0,face.as_ptr());
                if !f.is_null(){let _=slot.set(f as usize);}
            }
        }
        let _=WHITE_BRUSH.set(CreateSolidBrush(rgb(255,255,255)) as usize);
        let _=BACK_BRUSH.set(CreateSolidBrush(rgb(240,244,249)) as usize);
        let instance = GetModuleHandleW(null());
        let class_name = wide("GakuActors.NativeWindow.Prototype");
        let class = WndClass { style: 0, proc: Some(wndproc), class_extra: 0, window_extra: 0,
            instance, icon: null_mut(), cursor: LoadCursorW(null_mut(), IDC_ARROW as *const u16),
            background: 6_usize as Handle, menu_name: null(), class_name: class_name.as_ptr() };
        if RegisterClassW(&class) == 0 { return; }
        let title = wide(if music_only {"GakuActors · 乐库"} else {"GakuActors · 场景资源管理（测试版）"});
        let w=(1440.0*scale).round() as i32;let h=(900.0*scale).round() as i32;
        let hwnd = CreateWindowExW(0, class_name.as_ptr(), title.as_ptr(), if music_only {WS_OVERLAPPEDWINDOW|0x02000000}else{WS_OVERLAPPEDWINDOW & !0x00050000},
            (sw-w)/2, (sh-h)/2, w, h, null_mut(), null_mut(), instance, null_mut());
        if hwnd.is_null() { return; }
        control(hwnd,instance,"BUTTON","人物与预设",20,135,170,44,0,106);
        control(hwnd,instance,"BUTTON","场景人物",20,187,170,44,0,126);
        control(hwnd,instance,"BUTTON","场景",20,239,170,44,0,107);
        control(hwnd,instance,"BUTTON","道具",20,291,170,44,0,108);
        control(hwnd,instance,"BUTTON","乐库",20,343,170,44,0,132);
        control(hwnd,instance,"STATIC","人物管理",240,25,500,32,0,330);
        control(hwnd,instance,"STATIC","选择人物和预设，加入场景后调整站位",240,59,750,24,0,331);
        control(hwnd,instance,"BUTTON","读取人物目录",240,97,150,36,0,101);
        control(hwnd,instance,"BUTTON","启用人物加载",400,97,150,36,0,103);
        control(hwnd,instance,"BUTTON","扫描场景资源",560,97,150,36,0,102);
        control(hwnd,instance,"BUTTON","启用资源控制",720,97,150,36,0,105);
        control(hwnd,instance,"BUTTON","让 Motion 释放资源",880,97,210,36,0,127);
        control(hwnd,instance,"BUTTON","开启自由视角",880,97,210,36,0,140);
        control(hwnd,instance,"BUTTON","检索乐库",240,97,150,36,0,133);
        control(hwnd,instance,"STATIC","",250,170,240,30,0,332);
        control(hwnd,instance,"STATIC","搜索人物",250,210,90,25,0,321);
        control(hwnd,instance,"EDIT","",250,235,260,29,WS_BORDER|0x80,220);
        control(hwnd,instance,"STATIC","搜索资源名",250,210,120,25,0,323);
        control(hwnd,instance,"EDIT","",250,235,260,29,WS_BORDER|0x80,221);
        let left=control(hwnd,instance,"LISTBOX","",250,275,260,240,0x0010|0x0040|0x0100|0x00200000|1,201);
        control(hwnd,instance,"STATIC","",570,170,300,30,0,333);
        control(hwnd,instance,"BUTTON","新建预设",1000,165,120,34,0,119);
        control(hwnd,instance,"BUTTON","编辑预设",1130,165,120,34,0,118);
        control(hwnd,instance,"BUTTON","删除预设",1260,165,120,34,0,111);
        control(hwnd,instance,"STATIC","选中预设查看身体与头部图片",570,211,500,23,0,322);
        let right=control(hwnd,instance,"LISTBOX","",570,243,470,270,0x0010|0x0040|0x0100|0x00200000|1,202);
        let picture=control(hwnd,instance,"STATIC","",1060,243,135,145,0x0e,0);
        let picture_status=control(hwnd,instance,"STATIC","身体服装：请选择预设",1055,400,150,90,0,0);
        let head_picture=control(hwnd,instance,"STATIC","",1220,243,135,145,0x0e,0);
        let head_status=control(hwnd,instance,"STATIC","头部组合：请选择预设",1215,400,150,90,0,0);
        control(hwnd,instance,"STATIC","选择左侧资源以查看详情。",570,228,760,200,0,320);
        control(hwnd,instance,"BUTTON","切换节点显隐",570,445,155,36,0,109);
        control(hwnd,instance,"STATIC","",250,578,500,30,0,334);
        control(hwnd,instance,"STATIC","参考人物",250,625,110,25,0,301);
        let actor=control(hwnd,instance,"COMBOBOX","",360,621,235,240,3|0x00200000,205);
        control(hwnd,instance,"BUTTON","加入场景",605,619,112,36,0,104);
        control(hwnd,instance,"BUTTON","试听",250,624,115,36,0,135);
        control(hwnd,instance,"BUTTON","暂停",375,624,115,36,0,136);
        control(hwnd,instance,"BUTTON","停止",500,624,115,36,0,137);
        control(hwnd,instance,"BUTTON","导出选中歌曲",650,624,155,36,0,134);
        control(hwnd,instance,"STATIC","试听进度  00:00 / 00:00",250,674,300,25,0,335);
        let seek=control(hwnd,instance,"msctls_trackbar32","",250,702,620,35,0x10,138);
        SendMessageW(seek,0x0406,1,(1000isize)<<16);
        control(hwnd,instance,"STATIC","试听音量：100%",920,674,220,25,0,336);
        let volume=control(hwnd,instance,"msctls_trackbar32","",920,702,280,35,0x10,139);
        SendMessageW(volume,0x0406,1,(100isize)<<16);
        control(hwnd,instance,"BUTTON","人物显隐",250,675,115,36,0,112);
        control(hwnd,instance,"BUTTON","移除人物",375,675,115,36,0,113);
        control(hwnd,instance,"STATIC","世界位置 X / Y / Z",760,622,185,25,0,302);
        let px=control(hwnd,instance,"EDIT","",940,620,72,29,WS_BORDER|0x80,303);
        let py=control(hwnd,instance,"EDIT","",1018,620,72,29,WS_BORDER|0x80,304);
        let pz=control(hwnd,instance,"EDIT","",1096,620,72,29,WS_BORDER|0x80,305);
        control(hwnd,instance,"BUTTON","设置位置",1180,617,92,36,0,114);
        control(hwnd,instance,"BUTTON","重置位置",1280,617,100,36,0,124);
        control(hwnd,instance,"STATIC","朝向（0–360°）",760,679,168,25,0,316);
        control(hwnd,instance,"EDIT","0",940,676,72,29,WS_BORDER|0x80,216);
        control(hwnd,instance,"BUTTON","设置朝向",1018,673,100,36,0,125);
        control(hwnd,instance,"BUTTON","左转 15°",1128,673,115,36,0,122);
        control(hwnd,instance,"BUTTON","右转 15°",1250,673,115,36,0,123);
        control(hwnd,instance,"STATIC","预设名称",250,625,100,25,0,310);
        let name=control(hwnd,instance,"EDIT","",360,621,250,29,WS_BORDER|0x80,210);
        control(hwnd,instance,"STATIC","身体",660,625,70,25,0,311);
        let body=control(hwnd,instance,"COMBOBOX","",730,621,630,230,3|0x00200000,211);
        control(hwnd,instance,"STATIC","脸部",660,670,70,25,0,312);
        let face=control(hwnd,instance,"COMBOBOX","",730,666,630,230,3|0x00200000,212);
        control(hwnd,instance,"STATIC","头发",660,715,70,25,0,313);
        let hair=control(hwnd,instance,"COMBOBOX","",730,711,630,230,3|0x00200000,213);
        control(hwnd,instance,"BUTTON","跨人物部件：关",250,674,155,36,0,117);
        control(hwnd,instance,"BUTTON","保存预设",250,730,115,36,0,110);
        control(hwnd,instance,"BUTTON","取消编辑",375,730,115,36,0,121);
        control(hwnd,instance,"BUTTON","背景颜色",250,624,135,36,0,115);
        control(hwnd,instance,"BUTTON","恢复相机",395,624,135,36,0,116);
        control(hwnd,instance,"STATIC","下次摄影舞台",250,683,150,25,0,314);
        let stage=control(hwnd,instance,"COMBOBOX","",400,679,330,220,3|0x00200000,209);
        for label in ["游戏原生摄影场景","通用 Live 舞台（实验）","初星舞台（实验）","中庭舞台（日中）"] {combo_add(stage,label);}
        control(hwnd,instance,"BUTTON","应用舞台",745,676,125,36,0,120);
        let status=control(hwnd,instance,"STATIC","尚未连接游戏插件。",240,808,1150,35,0,315);
        if !music_only {
            let p=|n:i32|(n as f64*scale).round() as i32;
            for (id,x,y,w,h) in [(220,250,235,310,32),(201,250,278,310,237),(333,620,170,300,30),
                (322,620,211,420,25),(202,620,243,415,270),(320,620,228,740,200),(109,620,445,155,36)] {
                MoveWindow(GetDlgItem(hwnd,id),p(x),p(y),p(w),p(h),1);
            }
            for list in [left,right] {SendMessageW(list,0x01a0,0,p(34) as isize);}
            text(GetDlgItem(hwnd,321),"搜索人物");
        }
        if music_only {
            SendMessageW(left,0x01a0,0,(62.0*scale).round() as isize);
            layout_music(hwnd);
            text(GetDlgItem(hwnd,323),"搜索歌曲、演唱者或资源名");
            text(GetDlgItem(hwnd,134),"导出歌曲");
        }
        let _=MUSIC_RESULT.set(Mutex::new(None));
        let player=preview::Player::new();SendMessageW(volume,0x0405,1,player.volume as isize);
        APP.with(|cell|*cell.borrow_mut()=Some(App{hwnd,status,left,right,view:if music_only {View::Music}else{View::People},snapshot:Value::Null,characters:Vec::new(),presets:Vec::new(),resources:Vec::new(),left_rows:Vec::new(),right_rows:Vec::new(),list_view:None,resource_filter:String::new(),selected_character:String::new(),selected_preset:String::new(),selected_resource:None,actors:Vec::new(),actor_labels:Vec::new(),actor,selected_actor:0,selected_reference:0,parts:[body,face,hair],part_items:std::array::from_fn(|_|Vec::new()),name,position:[px,py,pz],reset_wait_revision:None,reset_wait_actor:0,stage,picture,picture_status,head_picture,head_status,edit_id:String::new(),cross_character:false,editing:false,stage_choice:0,stage_initialized:false,songs:Vec::new(),music_visible:Vec::new(),music_rows:Vec::new(),music_filter:String::new(),music_selected:String::new(),music_busy:false,music_notice:"正在读取游戏歌曲。".into(),preview:player,preview_pending:None,preview_alive:Arc::new(std::sync::atomic::AtomicBool::new(true)),preview_seek_drag:false,volume_dirty:false}));
        APP.with(|cell|if let Some(app)=cell.borrow_mut().as_mut(){app.render();});
        if music_only {APP.with(|cell|if let Some(app)=cell.borrow_mut().as_mut(){app.command(133,0);});}
        if let Ok(client)=pipe::Client::from_args(){start_worker(hwnd,client);}
        ShowWindow(hwnd, 1); UpdateWindow(hwnd);
        if music_only {SetTimer(hwnd,1,250,null_mut());}
        let mut msg: Msg = std::mem::zeroed();
        loop { let result=GetMessageW(&mut msg,null_mut(),0,0); if result<=0 { break; }
            TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
}
