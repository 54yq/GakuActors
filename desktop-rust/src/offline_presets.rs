use serde_json::{json, Value};
use std::{collections::HashSet, fs, io::{self, Read}, os::windows::fs::OpenOptionsExt, path::Path};

const MAX_BYTES: usize = 1024 * 1024;
const MAX_PRESETS: usize = 256;

fn valid(preset: &Value) -> bool {
    let required = |key: &str, max| preset[key].as_str().is_some_and(|s| {
        !s.is_empty() && s.len() <= max && !s.bytes().any(|byte| byte < 32)
    });
    let empty = |key: &str| preset.get(key).is_none_or(|v| v.as_str() == Some(""));
    if !["body", "head", "bodyAsset", "faceAsset", "hairAsset", "factory"]
        .iter().all(|key| preset.get(key).is_none_or(Value::is_string))
        || !required("id", 96) || !required("name", 160) || !required("character", 128) {
        return false;
    }
    let assets = ["bodyAsset", "faceAsset", "hairAsset"];
    if !empty("factory") {
        return matches!(preset["factory"].as_str(), Some("CreateTeacherDescriptor" |
            "CreateTeacherGlassHairDescriptor" | "CreateVocalTrainerDescriptor" |
            "CreateDanceTrainerDescriptor" | "CreateVisualTrainerDescriptor"))
            && empty("body") && empty("head") && assets.iter().all(|key| empty(key));
    }
    if assets.iter().all(|key| empty(key)) { return required("body", 256) && required("head", 256); }
    empty("body") && empty("head") && assets.iter().all(|key| required(key, 256))
}

fn path(game: &Path) -> std::path::PathBuf {
    game.join("GakuLoader/Configs/gakuactors/actor_presets.json")
}

pub fn load(game: &Path) -> Result<Vec<Value>, String> {
    let file = match fs::OpenOptions::new().read(true).share_mode(7).open(path(game)) {
        Ok(file) => file,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(e) => return Err(format!("无法读取预设文件：{e}")),
    };
    let mut bytes = Vec::new();
    file.take((MAX_BYTES + 1) as u64).read_to_end(&mut bytes).map_err(|e| e.to_string())?;
    if bytes.len() > MAX_BYTES { return Err("预设文件过大，已保留原文件。".into()); }
    let mut root: Value = serde_json::from_slice(&bytes).map_err(|_| "预设文件无效，已保留原文件。")?;
    if root["version"] != 1 { return Err("预设文件版本不受支持。".into()); }
    let mut presets = root["presets"].take().as_array().cloned().ok_or("预设文件缺少预设列表。")?;
    let mut ids = HashSet::new();
    if presets.len() > MAX_PRESETS || !presets.iter().all(|p| valid(p) && ids.insert(p["id"].as_str().unwrap())) {
        return Err("预设字段或标识无效，已保留原文件。".into());
    }
    for preset in &mut presets { preset["builtin"] = false.into(); }
    Ok(presets)
}

pub fn update(game: &Path, request: &Value) -> Result<Vec<Value>, String> {
    let target = path(game);
    fs::create_dir_all(target.parent().unwrap()).map_err(|e| e.to_string())?;
    // The plugin takes the same exclusive handle for its read-modify-write.
    let _lock = fs::OpenOptions::new().write(true).create(true).truncate(false)
        .share_mode(0).open(target.with_extension("json.lock"))
        .map_err(|e| format!("预设文件暂时无法锁定：{e}"))?;
    let mut presets = load(game)?;
    if request["action"] == "save_preset" {
        let mut preset = request["preset"].clone();
        if !valid(&preset) { return Err("人物预设字段无效。".into()); }
        preset["builtin"] = false.into();
        if let Some(row) = presets.iter_mut().find(|row| row["id"] == preset["id"]) {
            *row = preset;
        } else {
            if presets.len() == MAX_PRESETS { return Err("最多保存 256 个预设。".into()); }
            presets.push(preset);
        }
    } else {
        presets.retain(|row| row["id"] != request["preset_id"]);
    }
    let bytes = serde_json::to_vec_pretty(&json!({"version":1,"presets":presets})).map_err(|e| e.to_string())?;
    if bytes.len() > MAX_BYTES { return Err("预设文件过大，已保留原文件。".into()); }
    crate::scene_tree::write_atomic(&target, &bytes).map_err(|e| format!("预设保存失败：{e}"))?;
    Ok(presets)
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn offline_crud_preserves_other_presets_and_refuses_corruption() {
        let game = std::env::current_dir().unwrap().join("target").join(format!("gakuactors-offline-presets-{}", std::process::id()));
        let first = json!({"id":"a","name":"初始预设","character":"hski","body":"hski-cstm-0000","head":"costume_head_hski-cstm-0000"});
        let second = json!({"id":"b","name":"其他预设","character":"hume","body":"hume-cstm-0000","head":"costume_head_hume-cstm-0000"});
        assert!(load(&game).unwrap().is_empty());
        update(&game, &json!({"action":"save_preset","preset":first})).unwrap();
        update(&game, &json!({"action":"save_preset","preset":second})).unwrap();
        let mut edited = first;
        edited["name"] = "修改后".into();
        update(&game, &json!({"action":"save_preset","preset":edited})).unwrap();
        let presets = load(&game).unwrap();
        assert_eq!(presets[0]["name"], "修改后");
        assert_eq!(presets[1]["id"], "b");
        let presets = update(&game, &json!({"action":"delete_preset","preset_id":"a"})).unwrap();
        assert_eq!(presets.len(), 1);
        assert_eq!(presets[0]["id"], "b");
        let target = path(&game);
        fs::write(&target, b"{broken").unwrap();
        assert!(update(&game, &json!({"action":"save_preset","preset":edited})).is_err());
        assert_eq!(fs::read(&target).unwrap(), b"{broken");
        fs::remove_file(target.with_extension("json.lock")).unwrap();
        fs::remove_file(target).unwrap();
        for dir in ["GakuLoader/Configs/gakuactors", "GakuLoader/Configs", "GakuLoader", ""] {
            fs::remove_dir(game.join(dir)).unwrap();
        }
    }
}
