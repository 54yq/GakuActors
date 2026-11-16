//! Local Octo music index and WAV export. Never writes to the game cache.
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use serde_json::Value;

pub(crate) const SCRIPT: &str = include_str!("music_catalog.py");
const NO_WINDOW: u32 = 0x08000000;

pub(crate) fn python() -> Result<PathBuf,String> {
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
    cmd.arg("-c").arg(SCRIPT);
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
    let mut songs=catalog["songs"].as_array().cloned().ok_or("清单缺少歌曲列表")?;
    for song in &mut songs { refresh_at(root,song); }
    Ok(songs)
}
fn valid_digest(digest:&str)->bool {digest.len()==32 && digest.bytes().all(|b|b.is_ascii_hexdigit())}
fn raw_path(root:&Path,digest:&str)->PathBuf {root.join("GakuLoader/Cache/gakuactors/music").join(digest)}
pub fn refresh_at(root:&Path,song:&mut Value) {
    let digest=song["md5"].as_str().unwrap_or("");
    let own=valid_digest(digest) && raw_path(root,digest).is_file();
    let game=song["game_path"].as_str().filter(|p|!p.is_empty()).is_some_and(|p|Path::new(p).is_file());
    let compiled=valid_digest(digest) && valid_wav(&root.join("GakuLoader/Cache/gakuactors/music-preview-v1").join(format!("{digest}.wav")));
    song["cached"]=Value::Bool(own||game);song["own_cached"]=Value::Bool(own);song["game_cached"]=Value::Bool(game);song["compiled"]=Value::Bool(compiled);
}
pub fn refresh(song:&mut Value){if let Ok(root)=game_root(){refresh_at(&root,song);}}
pub fn download(name:&str)->Result<(),String>{run_at(&game_root()?, &["--download".as_ref(),name.as_ref()])?;Ok(())}
pub fn remove_raw(digest:&str)->Result<(),String>{
    remove_raw_at(&game_root()?,digest)
}
fn remove_raw_at(root:&Path,digest:&str)->Result<(),String>{
    if !valid_digest(digest){return Err("缓存标识无效。".into());}
    match std::fs::remove_file(raw_path(root,digest)){Ok(())=>Ok(()),Err(e) if e.kind()==std::io::ErrorKind::NotFound=>Ok(()),Err(e)=>Err(format!("无法删除乐库缓存：{e}"))}
}

pub fn export(name: &str, output: &Path) -> Result<(),String> {
    export_at(&game_root()?, name, output)
}

// The game cache contains compressed assets, not playback-ready audio. Keep a
// separate decoded cache keyed by the manifest content hash for later visits.
pub fn prepare_preview(name: &str, digest: &str, output: &Path) -> Result<(),String> {
    if digest.len()!=32 || !digest.bytes().all(|b|b.is_ascii_hexdigit()) {
        return export(name, output);
    }
    let dir=game_root()?.join("GakuLoader/Cache/gakuactors/music-preview-v1");
    let cache=dir.join(format!("{digest}.wav"));
    if valid_wav(&cache) {
        let mut input=std::fs::File::open(&cache).map_err(|e|e.to_string())?;
        let mut target=std::fs::OpenOptions::new().write(true).create_new(true).open(output).map_err(|e|format!("无法创建音频文件：{e}"))?;
        if let Err(e)=std::io::copy(&mut input,&mut target) {drop(target);let _=std::fs::remove_file(output);return Err(e.to_string());}
        return Ok(());
    }
    if cache.exists() && !valid_wav(&cache) {let _=std::fs::remove_file(&cache);}
    export(name,output)?;
    if std::fs::create_dir_all(&dir).is_ok() {
        let temp=dir.join(format!("{digest}-{}-{}.tmp",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos()));
        if std::fs::copy(output,&temp).is_ok() {let _=std::fs::rename(&temp,&cache);}
        let _=std::fs::remove_file(temp);
    }
    Ok(())
}
pub fn preview_ready(digest:&str)->bool {
    digest.len()==32 && digest.bytes().all(|b|b.is_ascii_hexdigit()) && game_root().is_ok_and(|root|valid_wav(&root.join("GakuLoader/Cache/gakuactors/music-preview-v1").join(format!("{digest}.wav"))))
}
pub fn remove_preview(digest:&str)->Result<(),String> {
    if digest.len()!=32 || !digest.bytes().all(|b|b.is_ascii_hexdigit()){return Err("缓存标识无效。".into());}
    let path=game_root()?.join("GakuLoader/Cache/gakuactors/music-preview-v1").join(format!("{digest}.wav"));
    match std::fs::remove_file(path){Ok(())=>Ok(()),Err(e) if e.kind()==std::io::ErrorKind::NotFound=>Ok(()),Err(e)=>Err(format!("无法删除缓存：{e}"))}
}
pub fn prepare(name:&str,digest:&str)->Result<(),String> {
    if preview_ready(digest){return Ok(());}
    let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
    let path=std::env::temp_dir().join(format!("gakuactors-prepare-{}-{stamp}.wav",std::process::id()));
    let result=prepare_preview(name,digest,&path);let _=std::fs::remove_file(path);result?;
    if preview_ready(digest){Ok(())}else{Err("音频已转换，但无法保存试听缓存。".into())}
}
fn valid_wav(path:&Path)->bool {
    use std::io::Read;
    let Ok(mut f)=std::fs::File::open(path) else{return false};
    let mut h=[0u8;12];
    f.read_exact(&mut h).is_ok() && &h[..4]==b"RIFF" && &h[8..]==b"WAVE"
        && f.metadata().is_ok_and(|m|m.len()>=u32::from_le_bytes([h[4],h[5],h[6],h[7]]) as u64+8)
}

pub fn export_at(root: &Path, name: &str, output: &Path) -> Result<(),String> {
    run_at(root, &["--filter".as_ref(), name.as_ref(), "--export".as_ref(), name.as_ref(),
          "--output".as_ref(), output.as_os_str()])?;
    Ok(())
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn cache_states_and_deletion_are_independent() {
        let root=std::env::temp_dir().join(format!("gaku-music-state-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let digest="0123456789abcdef0123456789abcdef";
        let game=root.join("game-cache");let raw=raw_path(&root,digest);let wav=root.join("GakuLoader/Cache/gakuactors/music-preview-v1").join(format!("{digest}.wav"));
        std::fs::create_dir_all(raw.parent().unwrap()).unwrap();std::fs::create_dir_all(wav.parent().unwrap()).unwrap();
        let mut song=serde_json::json!({"md5":digest,"game_path":game.to_string_lossy()});
        refresh_at(&root,&mut song);assert_eq!(song["cached"],false);assert_eq!(song["compiled"],false);
        std::fs::write(&raw,b"resource").unwrap();refresh_at(&root,&mut song);assert_eq!(song["own_cached"],true);assert_eq!(song["compiled"],false);
        std::fs::write(&wav,b"RIFF\x04\x00\x00\x00WAVE").unwrap();refresh_at(&root,&mut song);assert_eq!(song["compiled"],true);
        std::fs::write(&game,b"game").unwrap();remove_raw_at(&root,digest).unwrap();refresh_at(&root,&mut song);
        assert_eq!(song["own_cached"],false);assert_eq!(song["game_cached"],true);assert_eq!(song["cached"],true);assert_eq!(song["compiled"],true);
        assert!(remove_raw_at(&root,"../escape").is_err());assert!(game.is_file());
        std::fs::remove_file(&wav).unwrap();refresh_at(&root,&mut song);assert_eq!(song["compiled"],false);assert_eq!(song["cached"],true);
        std::fs::remove_dir_all(root).unwrap();
    }
}
