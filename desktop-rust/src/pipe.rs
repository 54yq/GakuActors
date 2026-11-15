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
    fn frames_round_trip_and_reject_invalid_lengths() {
        let mut out=Vec::new();write_frame(&mut out,b"{\"x\":1}").unwrap();
        assert_eq!(read_frame(&mut Cursor::new(out)).unwrap(),b"{\"x\":1}");
        assert!(write_frame(&mut Vec::new(),&vec![0;MAX_REQUEST+1]).is_err());
        assert!(read_frame(&mut Cursor::new((MAX_RESPONSE as u32+1).to_le_bytes())).is_err());
        assert!(read_frame(&mut Cursor::new([3,0,0,0,b'a'])).is_err());
    }
}
