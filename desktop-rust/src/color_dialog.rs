//! Native Windows colour picker; called only by an explicit user action.
use std::ffi::c_void;
#[repr(C)] struct ChooseColor { size:u32, owner:*mut c_void, instance:*mut c_void, result:u32, custom:*mut u32, flags:u32, data:isize, hook:*mut c_void, template:*const u16 }
#[link(name="comdlg32")] extern "system" {fn ChooseColorW(value:*mut ChooseColor)->i32;}
pub fn choose(current:[u8;3])->Option<[u8;3]>{
    let mut custom=[0x00ffffffu32;16];
    let rgb=u32::from(current[0])|(u32::from(current[1])<<8)|(u32::from(current[2])<<16);
    let mut choice=ChooseColor{size:std::mem::size_of::<ChooseColor>() as u32,owner:std::ptr::null_mut(),instance:std::ptr::null_mut(),result:rgb,custom:custom.as_mut_ptr(),flags:0x0001|0x0002,data:0,hook:std::ptr::null_mut(),template:std::ptr::null()};
    if unsafe{ChooseColorW(&mut choice)}==0{return None;}
    Some([(choice.result&255) as u8,((choice.result>>8)&255) as u8,((choice.result>>16)&255) as u8])
}
