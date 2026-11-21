use crate::scene_tree::{self, TreeRow};
use crate::slint_ui::SceneAsset;
use serde_json::Value;
use std::collections::{BTreeMap, HashSet};

#[derive(Default)]
pub struct Detail {
    pub name: String,
    pub tags: String,
    pub state: String,
    pub path: String,
    pub note_writable: bool,
}

#[derive(Default)]
pub struct Workspace {
    pub fixed: Vec<SceneAsset>,
    pub moving: Vec<SceneAsset>,
    pub rows: Vec<TreeRow>,
    pub fixed_count: usize,
    pub moving_count: usize,
    pub detail: Detail,
}

const FIXED_GROUPS: [(&str, &str); 3] = [
    ("fixed-structure", "舞台结构"),
    ("fixed-background", "背景与装饰"),
    ("fixed-lighting", "灯光与后处理"),
];

pub fn project(
    resources: &[Value],
    scene: &str,
    selection: &str,
    collapsed: &HashSet<String>,
    filters: [&str; 2],
    notes: &BTreeMap<String, String>,
) -> Workspace {
    let resources = scene_tree::separated_resources(resources);
    let mut view = Workspace::default();
    for resource in &resources {
        match resource["kind"].as_i64() {
            Some(1) => view.moving_count += 1,
            Some(kind) if kind >= 2 && resource["moving_member"] != true => view.fixed_count += 1,
            _ => {}
        }
    }

    let mut fixed: [Vec<SceneAsset>; 3] = Default::default();
    let no_collapsed = HashSet::new();
    for (index, query) in filters.into_iter().enumerate() {
        let moving = index == 1;
        let rows = scene_tree::build(&resources, scene, moving, if moving { collapsed } else { &no_collapsed }, query, notes);
        let mut lanes: Vec<(usize, usize, bool)> = Vec::new();
        let mut fixed_children = vec![false; rows.len()];
        let mut ancestry: Vec<usize> = Vec::new();
        if !moving {
            for (i, row) in rows.iter().enumerate() {
                ancestry.truncate(row.depth);
                let parent = ancestry.last().copied();
                let category = scene_tree::category(&row.value);
                let explicit_group = FIXED_GROUPS.iter().position(|(_, title)| *title == category).unwrap_or(0);
                let group = if explicit_group == 0 {
                    parent.map_or(0, |p| lanes[p].0)
                } else { explicit_group };
                let same_group_parent = parent.filter(|&p| lanes[p].0 == group);
                let (depth, visible) = same_group_parent.map_or((0, true), |p| {
                    fixed_children[p] = true;
                    (lanes[p].1 + 1, lanes[p].2 && (!query.trim().is_empty() || !collapsed.contains(&rows[p].collapse_key)))
                });
                lanes.push((group, depth, visible));
                ancestry.push(i);
            }
        }
        for (i, row) in rows.into_iter().enumerate() {
            let value = &row.value;
            let name = value["name"].as_str().unwrap_or_default();
            let category = scene_tree::category(value);
            let display_category = if value["moving_member"] == true { "道具部件" } else if !moving { FIXED_GROUPS[lanes[i].0].1 } else { category };
            let tags = [Some(display_category), scene_tree::function_tag(name).or(row.guess),
                (value["contains_moving_asset"] == true).then_some("包含移动资产")]
                .into_iter().flatten().collect::<Vec<_>>().join(" · ");
            let selected = row.key == selection;
            let locked = value["locked"] == true;
            let hidden = value["hidden"] == true;
            if selected {
                view.detail = Detail {
                    name: name.into(),
                    tags: tags.clone(),
                    state: if locked { "只读 / 受保护" } else if hidden { "已隐藏" } else { "显示" }.into(),
                    path: row.path.join(" / "),
                    note_writable: row.note_key.is_some(),
                };
            }
            let (depth, expandable, expanded) = if moving {
                (row.depth, row.has_children, row.expanded)
            } else {
                (lanes[i].1, fixed_children[i], fixed_children[i] && (!query.trim().is_empty() || !collapsed.contains(&row.collapse_key)))
            };
            let item = SceneAsset {
                key: row.key.clone().into(), label: name.into(), tags: tags.into(), selected,
                depth: depth.min(8) as i32, expandable, expanded,
                hidden, locked, prop_group: value["kind"] == 1, ..Default::default()
            };
            if moving {
                view.moving.push(item);
            } else if lanes[i].2 {
                fixed[lanes[i].0].push(item);
            }
            view.rows.push(row);
        }
    }
    for ((key, label), rows) in FIXED_GROUPS.into_iter().zip(fixed) {
        if rows.is_empty() { continue; }
        let expanded = !filters[0].trim().is_empty() || !collapsed.contains(key);
        view.fixed.push(SceneAsset {
            key:key.into(), label:label.into(), group_header:true, expandable:true, expanded,
            ..Default::default()
        });
        if expanded { view.fixed.extend(rows); }
    }
    view
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn fixture() -> Vec<Value> {
        vec![
            json!({"scene":10,"kind":4,"instance":1,"name":"舞台主体"}),
            json!({"scene":10,"kind":2,"instance":2,"parent":1,"name":"Background"}),
            json!({"scene":10,"kind":4,"instance":3,"parent":1,"name":"Prop_Microphone"}),
            json!({"scene":10,"kind":4,"instance":4,"parent":3,"name":"MicrophoneMesh"}),
            json!({"scene":10,"kind":1,"instance":3,"name":"手持麦克风"}),
        ]
    }

    #[test]
    fn projects_fixed_groups_and_prop_parts_with_safe_control() {
        let assets = fixture();
        let view = project(&assets, "Live", "10:4:4", &HashSet::new(), ["", "Mesh"], &BTreeMap::new());
        assert_eq!((view.fixed_count, view.moving_count), (2, 1));
        assert_eq!(view.moving.len(), 2);
        let background = view.fixed.iter().find(|row| row.label == "Background").unwrap();
        assert_eq!(background.depth, 0);
        assert!(view.fixed.iter().any(|row| row.group_header && row.label == "背景与装饰"));
        assert!(view.moving[0].prop_group);
        assert!(view.moving[1].locked);
        assert_eq!(view.detail.path, "手持麦克风 / MicrophoneMesh");
        assert!(view.detail.note_writable);
    }
}
