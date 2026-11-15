//! Local Octo music index and WAV export. Never writes to the game cache.
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use serde_json::Value;

const SCRIPT: &str = include_str!("music_catalog.py");
const NO_WINDOW: u32 = 0x08000000;

fn python() -> Result<PathBuf,String> {
    let mut candidates = Vec::new();
    if let Some(home) = std::env::var_os("USERPROFILE") {
        candidates.push(PathBuf::from(home).join("anaconda3/python.exe"));
    }
    candidates.push(PathBuf::from("python"));
    for p in candidates {
        if Command::new(&p).args(["-c", "import Crypto.Cipher.AES"]).creation_flags(NO_WINDOW)
            .output().is_ok_and(|o| o.status.success()) { return Ok(p); }
    }
    Err("找不到带 PyCryptodome 的 Python，无法解析乐库。".into())
}

pub fn game_root() -> Result<PathBuf,String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let root = exe.parent().and_then(Path::parent).and_then(Path::parent)
        .ok_or("无法定位游戏目录")?.to_path_buf();
    if !root.join("octo/pdb/400").is_dir() { return Err("游戏资源清单不存在。".into()); }
    Ok(root)
}

fn run_at(root: &Path, args: &[&std::ffi::OsStr]) -> Result<String,String> {
    let python = python()?;
    let mut cmd=Command::new(python);
    let script=std::env::current_exe().ok().and_then(|p|p.parent().map(|v|v.join("GakuActors.music.py"))).filter(|p|p.is_file());
    if let Some(script)=script {cmd.arg(script);} else {cmd.arg("-c").arg(SCRIPT);}
    cmd.arg("--game").arg(root).args(args).env("PYTHONIOENCODING", "utf-8").creation_flags(NO_WINDOW);
    if let Some(deps)=std::env::current_exe().ok().and_then(|p|p.parent().map(|v|v.join("GakuActors.Desktop.deps"))).filter(|p|p.is_dir()) {
        cmd.env("PYTHONPATH",deps);
    }
    let output=cmd.output().map_err(|e| format!("无法启动音频工具：{e}"))?;
    if !output.status.success() {
        let err = String::from_utf8_lossy(&output.stderr);
        return Err(format!("音频处理失败：{}", err.lines().last().unwrap_or("未知错误")));
    }
    String::from_utf8(output.stdout).map_err(|e| e.to_string())
}

pub fn scan() -> Result<Vec<Value>,String> {
    scan_at(&game_root()?)
}

pub fn scan_at(root: &Path) -> Result<Vec<Value>,String> {
    let data = run_at(root, &["--json".as_ref(), "-".as_ref()])?;
    let catalog: Value = serde_json::from_str(&data).map_err(|e| format!("清单格式错误：{e}"))?;
    catalog["songs"].as_array().cloned().ok_or("清单缺少歌曲列表".into())
}

pub fn export(name: &str, output: &Path) -> Result<(),String> {
    export_at(&game_root()?, name, output)
}

pub fn export_at(root: &Path, name: &str, output: &Path) -> Result<(),String> {
    run_at(root, &["--filter".as_ref(), name.as_ref(), "--export".as_ref(), name.as_ref(),
          "--output".as_ref(), output.as_os_str()])?;
    Ok(())
}
