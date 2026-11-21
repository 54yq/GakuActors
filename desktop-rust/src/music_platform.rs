//! Windows integration for the music window. Keep native window behavior and raw song titles.
use std::path::PathBuf;

/// Must run before Slint creates its font collection. Slint includes SLINT_FONT_PATH
/// in its generic fallback chain; registering a font alone does not put it in that chain.
/// Use the user's Windows fonts in place, rather than bundling copies of system fonts.
pub fn configure_font_fallback() {
    let fonts = PathBuf::from(std::env::var_os("WINDIR").unwrap_or_else(|| "C:\\Windows".into())).join("Fonts");
    let mut paths: Vec<PathBuf> = std::env::var_os("SLINT_FONT_PATH")
        .map(|value| std::env::split_paths(&value).collect()).unwrap_or_default();
    // A half-width kana mark may share a shaping cluster with a math symbol
    // (e.g. ∀ﾟ). Include a CJK family covering BOTH, not just a symbol-only font.
    for name in ["seguisym.ttf", "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc", "msjhl.ttc", "segoeui.ttf", "seguiemj.ttf"] {
        let path = fonts.join(name);
        if path.is_file() && !paths.contains(&path) { paths.push(path); }
    }
    if let Ok(value) = std::env::join_paths(paths) { std::env::set_var("SLINT_FONT_PATH", value); }
}

/// DWM uses COLORREF (0x00BBGGRR), whereas Slint's palette is RGB.
fn colorref(rgb: u32) -> u32 { ((rgb & 0xff) << 16) | (rgb & 0xff00) | (rgb >> 16) }

#[link(name="dwmapi")]
extern "system" { fn DwmSetWindowAttribute(hwnd:*mut std::ffi::c_void, attribute:u32, value:*const std::ffi::c_void, size:u32)->i32; }

fn apply_title_colors(hwnd: *mut std::ffi::c_void, mode: i32) -> [i32;4] {
    let dark = u32::from(mode == 2);
    let caption = colorref(match mode { 1 => 0xfdf1f0, 2 => 0x171a24, _ => 0xedf4fd });
    let text = colorref(if mode == 2 { 0xeff1f7 } else { 0x263449 });
    let border = colorref(match mode { 1 => 0xeee0de, 2 => 0x2b3243, _ => 0xdfe7f1 });
    // https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
    // Windows 11 supports explicit caption colors. Older Windows keeps native
    // decorations and still receives the light/dark preference where supported.
    [(20, dark), (35, caption), (36, text), (34, border)].map(|(attribute,value)|
        unsafe { DwmSetWindowAttribute(hwnd, attribute, &value as *const u32 as _, 4) })
}

pub fn sync_title_bar(window: &slint::Window, mode: i32) -> bool {
    use slint::winit_030::{WinitWindowAccessor, winit::{raw_window_handle::{HasWindowHandle, RawWindowHandle}, window::Theme}};
    window.with_winit_window(|native| {
        native.set_theme(Some(if mode == 2 { Theme::Dark } else { Theme::Light }));
        let Ok(handle) = native.window_handle() else { return false };
        let RawWindowHandle::Win32(handle) = handle.as_raw() else { return false };
        let hwnd = handle.hwnd.get() as *mut std::ffi::c_void;
        apply_title_colors(hwnd, mode);
        true
    }).unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_void;
    #[link(name="user32")]
    extern "system" {
        fn CreateWindowExW(ex:u32,class:*const u16,title:*const u16,style:u32,x:i32,y:i32,w:i32,h:i32,parent:*mut c_void,menu:*mut c_void,instance:*mut c_void,param:*mut c_void)->*mut c_void;
        fn DestroyWindow(hwnd:*mut c_void)->i32;
    }
    #[test]
    fn native_caption_colors_follow_all_three_themes() {
        // Invisible native window: verify DWM state without opening the application.
        let class:Vec<u16>="STATIC\0".encode_utf16().collect();let null=std::ptr::null_mut();
        let hwnd=unsafe{CreateWindowExW(0,class.as_ptr(),class.as_ptr(),0x00cf0000,0,0,500,300,null,null,null,null)};
        assert!(!hwnd.is_null());
        struct Native(*mut c_void);impl Drop for Native{fn drop(&mut self){unsafe{DestroyWindow(self.0);}}}
        let _owned=Native(hwnd);
        for mode in 0..3 {
            // Caption colors are documented as DwmSetWindowAttribute attributes;
            // they cannot be read back with DwmGetWindowAttribute on all builds.
            assert_eq!(apply_title_colors(hwnd,mode),[0;4],"DWM must accept theme {mode}");
        }
    }
}
