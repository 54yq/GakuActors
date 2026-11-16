//! Length-prefixed JSON frames over handles inherited from the plugin.
use std::fs::File;
use std::io::{self, Read, Write};
use std::os::windows::io::FromRawHandle;
use serde_json::Value;

const MAX_REQUEST: usize = 64 * 1024;
const MAX_RESPONSE: usize = 16 * 1024 * 1024;

fn write_frame<W: Write>(out: &mut W, data: &[u8]) -> io::Result<()> {
    if data.is_empty() || data.len() > MAX_REQUEST { return Err(io::Error::new(io::ErrorKind::InvalidInput,"request size")); }
    out.write_all(&(data.len() as u32).to_le_bytes())?;
    out.write_all(data)?;
    out.flush()
}
fn read_frame<R: Read>(input: &mut R) -> io::Result<Vec<u8>> {
    let mut length=[0;4];input.read_exact(&mut length)?;
    let length=u32::from_le_bytes(length) as usize;
    if length==0 || length>MAX_RESPONSE { return Err(io::Error::new(io::ErrorKind::InvalidData,"response size")); }
    let mut data=vec![0;length];input.read_exact(&mut data)?;Ok(data)
}

fn detection_failure(game_found:bool,unverified:bool)->io::Error {
    if game_found {io::Error::new(io::ErrorKind::NotConnected,"已检测到游戏，等待 GakuActors 插件连接")}
    else if unverified {io::Error::new(io::ErrorKind::PermissionDenied,"无法确认游戏进程，请检查权限")}
    else {io::Error::new(io::ErrorKind::NotFound,"未检测到游戏启动")}
}

pub struct Client { input: File, output: File }
impl Client {
    pub fn from_args() -> io::Result<Self> {
        let mut args=std::env::args().skip(1);let mut read=None;let mut write=None;
        while let Some(arg)=args.next() {
            match arg.as_str() {
                "--pipe-read" => read=args.next().and_then(|x|x.parse::<usize>().ok()),
                "--pipe-write" => write=args.next().and_then(|x|x.parse::<usize>().ok()),
                _ => {}
            }
        }
        // Handles are supplied only by the plugin's restricted handle list.
        unsafe { Self::from_handles(read.unwrap_or(0),write.unwrap_or(0)) }
    }
    /// Safety: arguments must be valid, uniquely owned inherited pipe handles.
    pub unsafe fn from_handles(read: usize,write: usize) -> io::Result<Self> {
        if read==0 || write==0 || read==write || read==usize::MAX || write==usize::MAX {
            return Err(io::Error::new(io::ErrorKind::InvalidInput,"pipe handles"));
        }
        Ok(Self{input:File::from_raw_handle(read as _),output:File::from_raw_handle(write as _)})
    }
    /// Discover only the game executable beside this installation, then verify
    /// the pipe server PID before sending any commands.
    pub fn connect_game(game:&std::path::Path) -> io::Result<Self> {
        use std::os::windows::ffi::OsStrExt;
        type Handle=*mut std::ffi::c_void;
        #[repr(C)] struct ProcessEntry {size:u32,usage:u32,pid:u32,heap:usize,module:u32,threads:u32,parent:u32,priority:i32,flags:u32,exe:[u16;260]}
        #[link(name="kernel32")] extern "system" {
            fn CreateToolhelp32Snapshot(flags:u32,pid:u32)->Handle;fn Process32FirstW(h:Handle,e:*mut ProcessEntry)->i32;fn Process32NextW(h:Handle,e:*mut ProcessEntry)->i32;
            fn OpenProcess(access:u32,inherit:i32,pid:u32)->Handle;fn QueryFullProcessImageNameW(h:Handle,flags:u32,path:*mut u16,size:*mut u32)->i32;
            fn CloseHandle(h:Handle)->i32;fn CreateFileW(name:*const u16,access:u32,share:u32,security:*mut std::ffi::c_void,creation:u32,flags:u32,template:Handle)->Handle;
            fn GetNamedPipeServerProcessId(pipe:Handle,pid:*mut u32)->i32;
        }
        let expected=game.join("gakumas.exe").to_string_lossy().to_lowercase();
        let snapshot=unsafe{CreateToolhelp32Snapshot(2,0)};if snapshot as isize == -1{return Err(io::Error::last_os_error());}
        let mut entry:ProcessEntry=unsafe{std::mem::zeroed()};entry.size=std::mem::size_of::<ProcessEntry>() as u32;
        let mut found=unsafe{Process32FirstW(snapshot,&mut entry)}!=0;if !found{let error=io::Error::last_os_error();unsafe{CloseHandle(snapshot)};return Err(error);}
        let mut result=None;let mut game_found=false;let mut unverified=false;
        while found {
            let name=String::from_utf16_lossy(&entry.exe[..entry.exe.iter().position(|x|*x==0).unwrap_or(260)]);
            if name.eq_ignore_ascii_case("gakumas.exe") {
                let process=unsafe{OpenProcess(0x1000,0,entry.pid)};
                if !process.is_null(){let mut path=[0u16;32768];let mut len=path.len() as u32;
                    let readable=unsafe{QueryFullProcessImageNameW(process,0,path.as_mut_ptr(),&mut len)}!=0;
                    let valid=readable && String::from_utf16_lossy(&path[..len as usize]).to_lowercase()==expected;
                    if !readable{unverified=true;}unsafe{CloseHandle(process)};
                    if valid {game_found=true;let pipe=format!(r"\\.\pipe\GakuActors-v1-{}",entry.pid);let wide:Vec<u16>=std::ffi::OsStr::new(&pipe).encode_wide().chain(Some(0)).collect();
                        let handle=unsafe{CreateFileW(wide.as_ptr(),0xc0000000,0,std::ptr::null_mut(),3,0,std::ptr::null_mut())};
                        if handle as isize != -1 {let mut server=0;let valid=unsafe{GetNamedPipeServerProcessId(handle,&mut server)}!=0 && server==entry.pid;
                            if valid {let file=unsafe{File::from_raw_handle(handle)};if let Ok(output)=file.try_clone(){result=Some(Self{input:file,output});break;}}
                            else {unsafe{CloseHandle(handle)};}
                        }
                    }
                }else{unverified=true;}
            }
            found=unsafe{Process32NextW(snapshot,&mut entry)}!=0;
        }
        unsafe{CloseHandle(snapshot)};
        result.ok_or_else(||detection_failure(game_found,unverified))
    }
    pub fn request(&mut self, request: &Value) -> io::Result<Value> {
        let bytes=serde_json::to_vec(request).map_err(io::Error::other)?;
        write_frame(&mut self.output,&bytes)?;
        serde_json::from_slice(&read_frame(&mut self.input)?).map_err(io::Error::other)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;
    #[test]
    fn game_detection_reports_distinct_states() {
        assert_eq!(detection_failure(false,false).kind(),io::ErrorKind::NotFound);
        assert_eq!(detection_failure(false,false).to_string(),"未检测到游戏启动");
        assert_eq!(detection_failure(true,false).kind(),io::ErrorKind::NotConnected);
        assert_eq!(detection_failure(false,true).kind(),io::ErrorKind::PermissionDenied);
    }
    #[test]
    fn frames_round_trip_and_reject_invalid_lengths() {
        let mut out=Vec::new();write_frame(&mut out,b"{\"x\":1}").unwrap();
        assert_eq!(read_frame(&mut Cursor::new(out)).unwrap(),b"{\"x\":1}");
        assert!(write_frame(&mut Vec::new(),&vec![0;MAX_REQUEST+1]).is_err());
        assert!(read_frame(&mut Cursor::new((MAX_RESPONSE as u32+1).to_le_bytes())).is_err());
        assert!(read_frame(&mut Cursor::new([3,0,0,0,b'a'])).is_err());
    }
}
