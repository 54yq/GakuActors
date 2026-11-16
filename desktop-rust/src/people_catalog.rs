//! Character counts and exact links between raw models and costume records.
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};

fn text<'a>(v: &'a Value, field: &str) -> &'a str { v[field].as_str().unwrap_or("") }
fn list<'a>(v: &'a Value, field: &str) -> impl Iterator<Item=&'a Value> { v[field].as_array().into_iter().flatten() }

#[derive(Default)]
pub struct Counts { pub bodies: usize, pub heads: usize, pub faces: usize, pub hairs: usize }
pub fn counts(snapshot: &Value) -> BTreeMap<String, Counts> {
    let mut parts: BTreeMap<String, [BTreeSet<String>; 4]> = BTreeMap::new();
    let mut add = |character: &str, index: usize, value: &str| {
        if !character.is_empty() && !value.is_empty() { parts.entry(character.into()).or_default()[index].insert(value.into()); }
    };
    for row in list(snapshot,"resources") { if text(row,"source")=="saved_preset" {continue;} if let Some(i) = ["body","face","hair"].iter().position(|k|*k==text(row,"kind")) { add(text(row,"character"),[0,2,3][i],text(row,"resource")); } }
    for row in list(snapshot,"bodies") { add(text(row,"character"),0,text(row,"resource")); }
    for row in list(snapshot,"heads") { let c=text(row,"character");add(c,1,text(row,"id"));add(c,2,text(row,"face_resource"));add(c,3,text(row,"resource")); }
    for row in list(snapshot,"models") { let c=text(row,"character");for (index,key) in [(0,"body"),(2,"face"),(3,"hair")] {add(c,index,text(row,key));} }
    parts.into_iter().map(|(id, p)| (id,Counts{bodies:p[0].len(),heads:p[1].len(),faces:p[2].len(),hairs:p[3].len()})).collect()
}

// Only use direct catalog references or descriptor-to-costume links from the
// game's resolver. Several records with different icons are ambiguous.
pub fn raw_part(snapshot: &Value, kind: &str, body: &str, face: &str, hair: &str) -> Option<String> {
    let (table,resource,model_field,id_field)=if kind=="body" {("bodies",body,"body","costume_id")} else {("heads",hair,"hair","head_id")};
    if resource.is_empty() || (kind=="head" && face.is_empty()) {return None;}
    let mut ids=BTreeSet::new();
    for row in list(snapshot,table) {
        if text(row,"resource")==resource && (kind=="body" || text(row,"face_resource")==face) {ids.insert(text(row,"id").to_owned());}
    }
    for row in list(snapshot,"models") {
        if text(row,model_field)==resource && (kind=="body" || text(row,"face")==face) {ids.insert(text(row,id_field).to_owned());}
    }
    ids.remove("");
    let records:Vec<_>=list(snapshot,table).filter(|r|ids.contains(text(r,"id"))).collect();
    if records.len()==1 {return Some(text(records[0],"id").into());}
    // A shared official icon is safe even when several records reference it.
    let icons:BTreeSet<_>=records.iter().map(|r|text(r,"thumbnail")).collect();
    if !records.is_empty() && icons.len()==1 && !icons.contains("") {Some(text(records[0],"id").into())} else {None}
}

#[cfg(test)] mod tests {
    use super::*; use serde_json::json;
    #[test] fn counts_distinct_categories_and_sources() {let s=json!({"resources":[{"character":"a","kind":"body","resource":"b"},{"character":"a","kind":"body","resource":"b"},{"character":"a","kind":"face","resource":"f"}],"heads":[{"character":"a","id":"h","resource":"hair","face_resource":"f"}],"models":[{"character":"a","body":"b","hair":"hair"}]});let c=counts(&s);let c=&c["a"];assert_eq!((c.bodies,c.heads,c.faces,c.hairs),(1,1,1,1));}
    #[test] fn exact_raw_mapping_handles_shared_and_ambiguous_icons() {let mut s=json!({"bodies":[{"id":"c1","thumbnail":"one"},{"id":"c2","thumbnail":"two"}],"heads":[{"id":"h1","thumbnail":"head"}],"models":[{"body":"body","face":"face","hair":"hair","costume_id":"c1","head_id":"h1"}]});assert_eq!(raw_part(&s,"body","body","",""),Some("c1".into()));assert_eq!(raw_part(&s,"head","","face","hair"),Some("h1".into()));s["models"].as_array_mut().unwrap().push(json!({"body":"body","costume_id":"c2"}));assert_eq!(raw_part(&s,"body","body","",""),None);}
}
