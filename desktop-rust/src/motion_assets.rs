use crate::{scene_tree::TreeRow, scene_workspace::{Detail, Workspace}, slint_ui::SceneAsset};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap, HashSet};

const GROUPS: [(&str, &str); 3] = [
    ("fixed-structure", "舞台结构"),
    ("fixed-background", "背景与装饰"),
    ("fixed-lighting", "灯光与后处理"),
];

fn text<'a>(value: &'a Value, key: &str) -> &'a str { value[key].as_str().unwrap_or_default() }
fn has_any(text: &str, words: &[&str]) -> bool { words.iter().any(|word| text.contains(word)) }
fn flag(value: &Value, key: &str) -> bool { value[key] == true }

// Keep rule order and normalization aligned with Motion StagePage.tsx.
fn function_tag(node: &Value) -> Option<&'static str> {
    let name = format!("{} {}", text(node, "name"), text(node, "displayName"))
        .to_lowercase().replace(['_', '-'], "");
    let words: &[(&[&str], &str)] = &[
        (&["floodlight", "泛光灯"], "泛光灯"),
        (&["bloom", "glow", "泛光"], "泛光"),
        (&["colorgrading", "colorcorrection", "colorgra", "lutfilter", "调色"], "色彩分级"),
        (&["filter", "滤镜"], "滤镜"),
        (&["depthoffield", "vldof", "景深"], "景深"),
        (&["vignette", "暗角"], "暗角"),
        (&["tonemapping", "tonemap"], "色调映射"),
        (&["motionblur", "动态模糊"], "动态模糊"),
        (&["lensflare", "paraffin", "光晕"], "镜头光晕"),
        (&["truss", "lightrig", "lightframe", "灯架", "桁架"], "灯架"),
        (&["spotlight", "spotlamp", "聚光灯"], "聚光灯"),
        (&["directionallight", "sunlight", "方向光"], "方向光"),
        (&["pointlight", "点光源"], "点光源"),
        (&["decolight", "装饰灯"], "装饰灯"),
        (&["reallight", "lightfixture", "灯具"], "舞台灯具"),
        (&["ledscreen", "ledwall", "backscreen", "屏幕"], "舞台屏幕"),
        (&["backdrop", "幕布"], "背景幕布"),
        (&["decal", "贴花"], "贴花"),
        (&["stagefloor", "floorpanel", "地板"], "舞台地板"),
        (&["scaffold", "supportframe", "支架"], "支架"),
    ];
    words.iter().enumerate().find_map(|(index, (matches, label))| {
        let extra = match index {
            2 => name.split(|c: char| !c.is_ascii_alphanumeric() && c != '_').any(|word| word == "lut"),
            4 => name.match_indices("dof").any(|(index, _)| index == 0 || !name.as_bytes()[index-1].is_ascii_lowercase()),
            _ => false,
        };
        (extra || has_any(&name, matches)).then_some(*label)
    })
}

fn ancestry<'a>(node: &'a Value, nodes: &HashMap<i64, &'a Value>) -> Vec<&'a Value> {
    let mut path = vec![node];
    let mut current = node;
    let mut seen = HashSet::new();
    if let Some(id) = node["index"].as_i64() { seen.insert(id); }
    while let Some(id) = current["parentIndex"].as_i64().filter(|id| seen.insert(*id)) {
        let Some(parent) = nodes.get(&id) else { break };
        path.push(*parent);
        current = parent;
    }
    path
}

fn category(path: &[&Value]) -> usize {
    if path.iter().any(|n| flag(n, "isLightingRoot") || flag(n, "isLightingNode")
        || has_any(&text(n, "name").to_lowercase(), &["light", "posteffect", "dof"])) { 2 }
    else if path.iter().any(|n| flag(n, "isIdentifiedBackground")
        || has_any(&text(n, "name").to_lowercase(), &["decal", "background"])) { 1 }
    else { 0 }
}

fn fixed_key(node: &Value) -> String { format!("motion-fixed/{}", node["index"]) }

fn display_prop(prop: &Value) -> Option<(String, String)> {
    let name = text(prop, "name");
    let model = text(prop, "modelName");
    let model = if model.is_empty() { name } else { model };
    let identity = format!("{} {model} {name}", text(prop, "actor")).to_lowercase();
    let penlight = has_any(&identity, &["penlight", "荧光棒"]);
    if prop["moving"] == false || (prop["moving"].is_null() && flag(prop,"timeline") && !penlight &&
        !has_any(&identity, &["手持","移动","轨道","摇臂","trolley","dolly","crane","角色位","constraint","baton","灯柱","movable"])) {
        return None;
    }
    let model_id = prop["modelId"].as_i64().filter(|id| *id != 0);
    let group_key = if penlight { "penlight".to_owned() }
        else if let Some(id) = model_id { id.to_string() }
        else { text(prop,"key").to_owned() };
    let lower = model.to_lowercase();
    let label = if penlight { "荧光棒" } else if lower.contains("trolley") { "移动箱" }
        else if lower.contains("dolly") { "轨道摄影车" } else if lower.contains("crane") { "摇臂" }
        else if has_any(&identity, &["baton", "灯柱"]) { "舞台灯柱" } else { model };
    Some((group_key, label.to_owned()))
}

fn push_detail(view: &mut Workspace, selection: &str, row: TreeRow, label: &str, tags: &str, hidden: bool) {
    if row.key == selection {
        view.detail = Detail {
            name: label.into(), tags: tags.into(),
            state: if hidden { "已隐藏" } else { "显示" }.into(),
            path: row.path.join(" / "), note_writable: row.note_key.is_some(),
        };
    }
    view.rows.push(row);
}

pub fn project(
    assets: &Value,
    resources: &[Value],
    scene: &str,
    selection: &str,
    expanded: &HashSet<String>,
    filters: [&str; 2],
    notes: &BTreeMap<String,String>,
) -> Workspace {
    let mut view = Workspace::default();
    let (Some(nodes), Some(props)) = (assets["nodes"].as_array(),assets["props"].as_array()) else {return view};
    view.fixed_count = nodes.len();
    let by_index: HashMap<_,_> = nodes.iter().filter_map(|n| Some((n["index"].as_i64()?, n))).collect();
    let parents: HashSet<_> = nodes.iter().filter_map(|n| n["parentIndex"].as_i64()).collect();
    let path_by_node: Vec<_> = nodes.iter().map(|n| ancestry(n, &by_index)).collect();
    let query = filters[0].trim().to_lowercase();
    let mut matches = HashSet::new();
    for (node,path) in nodes.iter().zip(&path_by_node) {
        if !query.is_empty() && [text(node,"displayName"), text(node,"name"), function_tag(node).unwrap_or("")]
            .iter().any(|value| value.to_lowercase().contains(&query)) {
            matches.extend(path.iter().filter_map(|n| n["index"].as_i64()));
        }
    }
    let mut local_by_id = HashMap::new();
    for resource in resources.iter().filter(|r|r["kind"].as_i64().is_some_and(|kind|kind>=2)) {
        if let Some(id) = resource["instance"].as_i64().filter(|id|*id!=0) {
            local_by_id.entry(id).and_modify(|candidate|*candidate=None).or_insert(Some(resource));
        }
    }
    let local_notes: HashMap<_,_> = if scene.is_empty() || resources.is_empty() { HashMap::new() } else {
        crate::scene_tree::build(resources,scene,false,&HashSet::new(),"",notes).into_iter()
            .filter_map(|row|Some((row.value["instance"].as_i64()?,row.note_key?))).collect()
    };
    let mut sections: [Vec<SceneAsset>;3] = Default::default();
    for (node,path) in nodes.iter().zip(&path_by_node) {
        let Some(id) = node["index"].as_i64() else {continue};
        if query.is_empty() && path.iter().skip(1).any(|parent| !expanded.contains(&fixed_key(parent))) {continue}
        if !query.is_empty() && !matches.contains(&id) {continue}
        let group = category(path);
        let name = text(node,"name");
        let label = text(node,"displayName");
        let label = if label.is_empty() {name} else {label};
        let key = fixed_key(node);
        let instance = node["instanceId"].as_i64().unwrap_or(0);
        let candidate = local_by_id.get(&instance).and_then(|r|*r).filter(|r|r["name"]==node["name"]);
        let locked = candidate.is_none_or(|r|flag(r,"locked")) || flag(node,"isColliderLocked") || flag(node,"containsMovingAsset")
            || path.iter().skip(1).any(|n|n["currentlyActive"]==false);
        let hidden = candidate.filter(|r|flag(r,"hidden")).is_some() || node["currentlyActive"]==false;
        let tags = [Some(GROUPS[group].1),function_tag(node),flag(node,"containsMovingAsset").then_some("包含移动资产"),
            flag(node,"isColliderLocked").then_some("碰撞锁定")].into_iter().flatten().collect::<Vec<_>>().join(" · ");
        let path: Vec<_> = path.iter().rev().map(|n| text(n,"name").to_owned()).collect();
        let note_key = candidate.and_then(|_|local_notes.get(&instance)).cloned();
        let note = note_key.as_ref().and_then(|key|notes.get(key)).cloned().unwrap_or_default();
        let mut value = candidate.cloned().unwrap_or_else(||json!({}));
        value["locked"] = locked.into();
        let has_children = parents.contains(&id);
        let open = !query.is_empty() || expanded.contains(&key);
        let depth = node["depth"].as_u64().unwrap_or_default().min(8) as i32;
        sections[group].push(SceneAsset {key:key.as_str().into(),label:label.into(),tags:tags.as_str().into(),selected:key==selection,
            depth,expandable:has_children,expanded:open,hidden,locked,..Default::default()});
        push_detail(&mut view,selection,TreeRow {path,value,key:key.clone(),note_key,collapse_key:key,
            depth:depth as usize,has_children,expanded:open,guess:None,note},label,&tags,hidden);
    }
    for ((key,title),rows) in GROUPS.into_iter().zip(sections) {
        if rows.is_empty() {continue}
        // Motion has heading labels, and only the nodes have expansion state.
        view.fixed.push(SceneAsset {key:key.into(),label:title.into(),group_header:true,..Default::default()});
        view.fixed.extend(rows);
    }
    let mut groups: Vec<(String,String,Vec<&Value>)> = Vec::new();
    let mut by_group = HashMap::new();
    for prop in props {
        let Some((key,label)) = display_prop(prop) else {continue};
        let index = *by_group.entry(key.clone()).or_insert_with(|| {
            groups.push((key,label,Vec::new()));
            groups.len()-1
        });
        groups[index].2.push(prop);
    }
    view.moving_count = groups.len();
    let query = filters[1].trim().to_lowercase();
    for (group,label,parts) in groups {
        if !query.is_empty() && !label.to_lowercase().contains(&query) &&
            !parts.iter().any(|p| [text(p,"actor"),text(p,"name")].iter().any(|s|s.to_lowercase().contains(&query))) {continue}
        let key = format!("motion-group/{group}");
        let open = expanded.contains(&key);
        let hidden = parts.iter().all(|v|flag(v,"hidden"));
        let tags = format!("{} 个部件",parts.len());
        view.moving.push(SceneAsset {key:key.as_str().into(),label:label.as_str().into(),tags:tags.as_str().into(),
            selected:key==selection,expandable:true,expanded:open,hidden,locked:true,prop_group:true,..Default::default()});
        push_detail(&mut view, selection, TreeRow {path:vec![label.clone()],value:json!({"locked":true}),
            key:key.clone(),note_key:None,collapse_key:key,depth:0,has_children:true,expanded:open,guess:None,note:String::new()},&label,&tags,hidden);
        if !open {continue}
        for part in parts {
            let name = text(part,"name");
            let actor = text(part,"actor");
            let key = format!("motion-part/{}",text(part,"key"));
            let hidden = flag(part,"hidden");
            view.moving.push(SceneAsset {key:key.as_str().into(),label:name.into(),tags:actor.into(),selected:key==selection,
                depth:1,hidden,locked:true,..Default::default()});
            push_detail(&mut view,selection,TreeRow {path:vec![label.clone(),name.into()],value:json!({"locked":true}),
                key:key.clone(),note_key:None,collapse_key:key,depth:1,has_children:false,expanded:false,guess:None,note:String::new()},name,actor,hidden);
        }
    }
    view
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_live_snapshot_matches_motion_fixed_names_tags_and_prop_groups() {
        let assets=json!({
            "nodes":[
                {"index":0,"parentIndex":-1,"depth":0,"name":"Stage","displayName":"舞台","currentlyActive":true},
                {"index":3,"parentIndex":0,"depth":1,"name":"LiveVLDoF","displayName":"后处理","currentlyActive":true},
                {"index":4,"parentIndex":0,"depth":1,"name":"mdl_env","displayName":"远景","isIdentifiedBackground":true}
            ],
            "props":[
                {"key":"t1","modelId":44,"name":"箱体","modelName":"TrolleyA","actor":"琴音","moving":true,"timeline":true},
                {"key":"t2","modelId":44,"name":"发光层","modelName":"TrolleyA","actor":"琴音","moving":true,"timeline":true},
                {"key":"p1","modelId":8,"name":"PenlightBlue","modelName":"Penlight","actor":"咲季","moving":true},
                {"key":"p2","modelId":9,"name":"PenlightRed","modelName":"Penlight","actor":"手毬","moving":true},
                {"key":"extra","modelId":11,"name":"舞台固定灯","moving":false}
            ]
        });
        let expand = HashSet::from(["motion-group/44".to_owned()]);
        let notes = BTreeMap::new();
        for (fixed,moving) in [("景深","发光层"),("LiveVLDoF","琴音"),("后处理","移动箱")] {
            let result=project(&assets,&[],"Live","",&expand,[fixed,moving],&notes);
            let fixed_names:Vec<_>=result.fixed.iter().filter(|e|!e.group_header).map(|e|e.label.as_str()).collect();
            let moving_names:Vec<_>=result.moving.iter().map(|e|e.label.as_str()).collect();
            assert_eq!(fixed_names, ["舞台","后处理"]);
            assert_eq!(moving_names, ["移动箱","箱体","发光层"]);
            assert_eq!(result.moving_count,2);
            assert!(result.moving.iter().all(|e|e.locked));
        }
        let result=project(&assets,&[],"Live","",&expand,["背景与装饰","手毬"],&notes);
        assert!(result.fixed.is_empty()); // Category headings are not searchable Motion fields.
        assert_eq!(result.moving[0].label,"荧光棒");
    }
}
