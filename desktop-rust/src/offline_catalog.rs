//! Read-only offline browsing from exact official icon names, and a cache of the
//! last authoritative in-game catalog. Never touches Unity outside a scene.
use serde_json::{json, Value};
use std::{collections::BTreeSet, path::Path};

fn path(game:&Path)->std::path::PathBuf { game.join("GakuLoader/Cache/gakuactors") }
fn read(p:&Path)->Option<Value> { serde_json::from_slice(&std::fs::read(p).ok()?).ok() }
fn manifest(game:&Path)->String {
    let Some(saved)=read(&path(game).join("model-icons/index.json")).and_then(|v|v["manifest"].as_str().map(str::to_owned)) else{return String::new();};
    let latest=std::fs::read_dir(game.join("octo/pdb/400")).ok().into_iter().flatten().flatten().filter_map(|entry| {
        let name=entry.file_name().to_string_lossy().into_owned();let number=name.parse::<u64>().ok()?;
        let meta=std::fs::metadata(entry.path().join("octocacheevai")).ok()?;
        let nanos=meta.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok()?.as_nanos();
        Some((number,format!("{name}:{}:{nanos}",meta.len())))
    }).max_by_key(|v|v.0);
    if latest.is_some_and(|(_,stamp)|stamp==saved){saved}else{String::new()}
}
pub fn needs_prefetch(game:&Path)->bool {
    let stamp=manifest(game);if stamp.is_empty(){return true;}
    let base=path(game).join("model-icons");let index=read(&base.join("index.json"));let report=read(&base.join("prefetch-report.json"));
    let total=index.as_ref().and_then(|v|v["records"].as_array()).map(|a|a.len() as u64);
    !matches!((total,report), (Some(n),Some(r)) if n>0 && r["total"].as_u64()==Some(n) && r["ready"].as_u64()==Some(n) && r["failed"].as_u64()==Some(0) && r["manifest"]==stamp)
}
pub fn load(game:&Path)->Value {
    let base=path(game); let stamp=manifest(game);
    if let Some(v)=read(&base.join("desktop-catalog.json")) {
        if !stamp.is_empty() && v["manifest"]==stamp && v["catalog"]["bodies"].is_array() { return v["catalog"].clone(); }
    }
    if stamp.is_empty(){return Value::Null;}
    let Some(index)=read(&base.join("model-icons/index.json")) else {return Value::Null;};
    let mut bodies=Vec::new(); let mut heads=Vec::new(); let mut chars=BTreeSet::new();
    for record in index["records"].as_array().into_iter().flatten() {
        let Some(name)=record["name"].as_str() else {continue;};
        let (kind,id)=if let Some(id)=name.strip_prefix("img_cos_costume_head_").and_then(|s|s.strip_suffix("_head")){("heads",format!("costume_head_{id}"))}
          else if let Some(id)=name.strip_prefix("img_cos_").and_then(|s|s.strip_suffix("_body")){("bodies",id.to_owned())}else{continue;};
        let base_id=id.strip_prefix("costume_head_").unwrap_or(&id);let Some((character,_))=base_id.split_once('-') else {continue;};
        if character.is_empty() || !character.bytes().all(|c|c.is_ascii_lowercase()||c.is_ascii_digit()) {continue;}
        chars.insert(character.to_owned()); let row=json!({"id":id,"name":base_id,"character":character,"thumbnail":name,"provisional":true});
        if kind=="bodies" {bodies.push(row)}else{heads.push(row)}
    }
    bodies.sort_by(|a,b|a["id"].as_str().cmp(&b["id"].as_str()));heads.sort_by(|a,b|a["id"].as_str().cmp(&b["id"].as_str()));
    json!({"characters":chars.into_iter().map(|id|json!({"name":id,"id":id})).collect::<Vec<_>>(),"bodies":bodies,"heads":heads,"resources":[],"presets":[],"provisional":true})
}
pub fn save(game:&Path,snapshot:&Value) {
    if snapshot["catalog_revision"].as_u64().unwrap_or(0)==0 || snapshot["bodies"].as_array().is_none_or(|a|a.is_empty()) {return;}
    let stamp=manifest(game); if stamp.is_empty(){return;}
    let mut c=json!({});for k in ["characters","bodies","heads","resources","models"] {c[k]=snapshot[k].clone();}
    c["presets"]=snapshot["presets"].as_array().map(|a|a.iter().filter(|p|p["builtin"]==true).cloned().collect::<Vec<_>>()).unwrap_or_default().into();
    let target=path(game).join("desktop-catalog.json");let tmp=target.with_extension("json.tmp");
    if let Ok(bytes)=serde_json::to_vec(&json!({"version":1,"manifest":stamp,"catalog":c})) {if std::fs::write(&tmp,bytes).is_ok(){let _=std::fs::rename(&tmp,&target).or_else(|_|{std::fs::copy(&tmp,&target).map(|_|())});let _=std::fs::remove_file(tmp);}}
}
pub fn merge(snapshot:&Value,catalog:&Value)->Value {
    let mut out=snapshot.clone(); if snapshot["catalog_revision"].as_u64().unwrap_or(0)>0 || !catalog["characters"].is_array(){return out;}
    for k in ["characters","bodies","heads","resources","models"] {
        let mut merged=catalog[k].as_array().cloned().unwrap_or_default();
        for row in snapshot[k].as_array().into_iter().flatten() {if !merged.iter().any(|x|x["id"].is_string() && x["id"]==row["id"] || (k=="resources"||k=="models") && x==row){merged.push(row.clone());}}
        out[k]=merged.into();
    }
    let mut presets=catalog["presets"].as_array().cloned().unwrap_or_default();presets.extend(snapshot["presets"].as_array().into_iter().flatten().filter(|p|p["builtin"]!=true).cloned());out["presets"]=presets.into();
    out["offline_provisional"]=catalog["provisional"].clone();out["offline_catalog"]=true.into();out
}
#[cfg(test)] mod tests {use super::*;#[test] fn preserves_user_presets_and_scene_status(){let live=json!({"catalog_revision":0,"admitted":false,"presets":[{"id":"mine","builtin":false}],"characters":[],"bodies":[],"heads":[],"resources":[]});let cached=json!({"characters":[{"id":"x"}],"bodies":[],"heads":[],"resources":[],"presets":[{"id":"base","builtin":true}]});let result=merge(&live,&cached);assert_eq!(result["presets"].as_array().unwrap().len(),2);assert_eq!(result["admitted"],false);assert_eq!(result["offline_catalog"],true);}}

#[cfg(test)] mod filesystem_tests {
    use super::*;
    #[test] fn indexes_exact_icons_and_requires_current_manifest() {
        let dir=std::env::temp_dir().join(format!("gakuactors-catalog-test-{}",std::process::id()));
        let m=dir.join("octo/pdb/400/1/octocacheevai");std::fs::create_dir_all(m.parent().unwrap()).unwrap();std::fs::write(&m,b"manifest").unwrap();
        let meta=std::fs::metadata(&m).unwrap();let ns=meta.modified().unwrap().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let base=path(&dir).join("model-icons");std::fs::create_dir_all(&base).unwrap();
        let index=json!({"manifest":format!("1:{}:{ns}",meta.len()),"records":[{"name":"img_cos_hski-cstm-0000_body"},{"name":"img_cos_costume_head_hski-cstm-0000_head"},{"name":"irrelevant"}]});
        std::fs::write(base.join("index.json"),index.to_string()).unwrap();let c=load(&dir);
        assert_eq!(c["bodies"][0]["id"],"hski-cstm-0000");assert_eq!(c["heads"][0]["id"],"costume_head_hski-cstm-0000");assert_eq!(c["provisional"],true);
        std::fs::write(&m,b"updated manifest").unwrap();assert!(load(&dir).is_null());
        std::fs::remove_dir_all(dir).unwrap();
    }
}
