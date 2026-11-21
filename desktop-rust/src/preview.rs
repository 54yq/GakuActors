//! Independent music-library preview. Never changes the system/game volume.
use std::os::windows::ffi::OsStrExt;
use std::path::{Path,PathBuf};
use std::iter::once;

#[link(name="winmm")]
extern "system" {
    fn mciSendStringW(command:*const u16,output:*mut u16,capacity:u32,callback:*mut std::ffi::c_void)->u32;
    fn mciGetErrorStringW(error:u32,text:*mut u16,capacity:u32)->i32;
}
#[link(name="kernel32")]
extern "system" {fn MoveFileExW(from:*const u16,to:*const u16,flags:u32)->i32;}
fn wide(s:&std::ffi::OsStr)->Vec<u16>{s.encode_wide().chain(once(0)).collect()}
fn send(command:&str)->Result<String,String> {
    let c:Vec<u16>=command.encode_utf16().chain(once(0)).collect();let mut out=[0u16;256];
    let rc=unsafe{mciSendStringW(c.as_ptr(),out.as_mut_ptr(),out.len() as u32,std::ptr::null_mut())};
    if rc!=0 {let mut msg=[0u16;256];unsafe{mciGetErrorStringW(rc,msg.as_mut_ptr(),msg.len() as u32)};let len=msg.iter().position(|c|*c==0).unwrap_or(msg.len());return Err(format!("音频播放失败（{rc}）：{}",String::from_utf16_lossy(&msg[..len])));}
    let len=out.iter().position(|c|*c==0).unwrap_or(out.len());Ok(String::from_utf16_lossy(&out[..len]))
}
fn config_path()->Result<PathBuf,String>{Ok(super::music::game_root()?.join("GakuLoader/Configs/gakuactors/music-player.json"))}
pub fn load_volume()->u32 {
    let Ok(path)=config_path() else{return 100};
    std::fs::read(path).ok().and_then(|v|serde_json::from_slice::<serde_json::Value>(&v).ok())
        .and_then(|v|v.get("preview_volume").and_then(|n|n.as_u64())).filter(|n|*n<=100).unwrap_or(100) as u32
}
pub fn save_volume(volume:u32)->Result<(),String> {
    if volume>100{return Err("音量超出范围".into());}
    save_setting("preview_volume",volume.into())
}
fn save_setting(key:&str,value:serde_json::Value)->Result<(),String>{
    let path=config_path()?;let parent=path.parent().ok_or("配置目录无效")?;
    std::fs::create_dir_all(parent).map_err(|e|format!("无法保存音量：{e}"))?;
    let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
    let temp=parent.join(format!("music-player-{}-{stamp}.tmp",std::process::id()));
    let mut data=match std::fs::read(&path){Ok(bytes)=>serde_json::from_slice::<serde_json::Value>(&bytes).map_err(|_|"音量配置文件格式无效，未覆盖。".to_string())?,Err(e) if e.kind()==std::io::ErrorKind::NotFound=>serde_json::json!({}),Err(e)=>return Err(format!("无法读取音量配置：{e}"))};
    let object=data.as_object_mut().ok_or("播放器配置文件格式无效，未覆盖。")?;object.insert(key.into(),value);
    let bytes=serde_json::to_vec(&data).map_err(|e|e.to_string())?;
    std::fs::write(&temp,bytes).map_err(|e|format!("无法保存音量：{e}"))?;
    if unsafe{MoveFileExW(wide(temp.as_os_str()).as_ptr(),wide(path.as_os_str()).as_ptr(),0x1|0x8)}==0 {
        let e=std::io::Error::last_os_error();let _=std::fs::remove_file(&temp);return Err(format!("无法保存音量：{e}"));
    }
    Ok(())
}
pub struct Player {pub open:bool,pub paused:bool,pub volume:u32,pub length:u32,pub path:Option<PathBuf>}
impl Player {
    pub fn new()->Self{Self{open:false,paused:false,volume:load_volume(),length:0,path:None}}
    pub fn set_volume(&mut self,value:u32)->Result<(),String>{
        let value=value.min(100);if self.open{send(&format!("setaudio gaku_library_preview volume to {}",value*10))?;}
        self.volume=value;Ok(())
    }
    pub fn open_and_play(&mut self,path:&Path)->Result<(),String>{
        self.stop()?;let path_string=path.to_string_lossy();if path_string.contains('"'){return Err("音频路径无效".into());}
        send(&format!("open \"{}\" type mpegvideo alias gaku_library_preview",path_string))?;
        self.open=true;self.path=Some(path.to_path_buf());
        let result=(|| {send("set gaku_library_preview time format milliseconds")?;
            // Do not play if independent volume cannot be applied.
            send(&format!("setaudio gaku_library_preview volume to {}",self.volume*10))?;
            self.length=send("status gaku_library_preview length")?.trim().parse().map_err(|_|"无法读取试听时长".to_string())?;
            send("play gaku_library_preview from 0")?;self.paused=false;Ok(())})();
        if result.is_err(){let _=self.stop();}result
    }
    pub fn toggle_pause(&mut self)->Result<(),String>{if !self.open{return Ok(())};if self.paused {let at=self.position().unwrap_or(0);send(&format!("play gaku_library_preview from {at}"))?;}else{send("pause gaku_library_preview")?;}self.paused=!self.paused;Ok(())}
    pub fn seek(&mut self,ms:u32)->Result<(),String>{if !self.open{return Ok(())};let at=ms.min(self.length.saturating_sub(1));
        if self.paused{send(&format!("seek gaku_library_preview to {at}"))?;}else{send(&format!("play gaku_library_preview from {at}"))?;}Ok(())}
    pub fn position(&self)->Option<u32>{if !self.open{return None;}send("status gaku_library_preview position").ok()?.trim().parse().ok()}
    pub fn finished(&self)->bool {self.open && !self.paused && send("status gaku_library_preview mode").is_ok_and(|m|m.trim()=="stopped") && self.position().is_some_and(|p|p>=self.length.saturating_sub(500))}
    pub fn stop(&mut self)->Result<(),String>{if self.open{send("close gaku_library_preview")?;self.open=false;self.paused=false;self.length=0;}
        if let Some(path)=self.path.take(){let _=std::fs::remove_file(path);}Ok(())}
}
impl Drop for Player {fn drop(&mut self){let _=self.stop();}}
