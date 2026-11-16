//! Character workspace and icon-first preset editor. All pictures come from
//! catalog-bound thumbnail requests; an unavailable image is never fabricated.
use super::*;
use std::collections::{HashMap,HashSet};
use std::time::Instant;

pub const FIRST:u16=400;
pub const LAST:u16=489;
const PAGE:usize=6;
static ACTIVE:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
static EDITOR:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
static SETTINGS:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
static RAW:std::sync::atomic::AtomicBool=std::sync::atomic::AtomicBool::new(false);
thread_local! {
    static VISIBILITY:RefCell<Option<std::collections::BTreeMap<u16,bool>>>=const {RefCell::new(None)};
}
pub fn active()->bool {ACTIVE.load(Ordering::Relaxed)}

#[derive(Default)]
pub struct Workspace {
    cache:HashMap<String,ThumbResult>,requested:HashSet<String>,
    pub settings:bool,raw_mode:bool,body:String,head:String,reveal_selection:bool,
    bodies:Vec<Value>,heads:Vec<Value>,character_rows:Vec<String>,
    body_page:usize,head_page:usize,preset_page:usize,
    raw:[String;3],raw_items:[Vec<String>;3],
    pending:Option<(Value,Instant)>,undo:Option<Value>,feedback:String,
    catalog_signature:String,
}
impl Workspace {
    pub fn accept_thumbnail(&mut self,result:ThumbResult) {
        if self.cache.len()>=96 {if let Some(key)=self.cache.keys().next().cloned(){self.cache.remove(&key);self.requested.remove(&key);}}
        self.cache.insert(result.id.clone(),result);
    }
}
pub fn choices(snapshot:&Value,character:&str,cross:bool,kind:&str)->Vec<Value> {
    snapshot[kind].as_array().map(|xs|xs.iter().filter(|p|cross||text_value(p,"character")==character).cloned().collect()).unwrap_or_default()
}
fn page_slice<T>(xs:&[T],page:usize)->&[T] {let start=(page*PAGE).min(xs.len());&xs[start..(start+PAGE).min(xs.len())]}
fn last_page(count:usize)->usize {count.saturating_sub(1)/PAGE}
fn same_preset(a:&Value,b:&Value)->bool { ["id","name","character","body","head","bodyAsset","faceAsset","hairAsset","factory"].iter().all(|k|text_value(a,k)==text_value(b,k)) }
fn part_key(kind:&str,id:&str)->String {format!("part:{kind}:{id}")}
fn char_name(snapshot:&Value,id:&str)->String {snapshot["characters"].as_array().and_then(|xs|xs.iter().find(|x|text_value(x,"id")==id)).map(|x|text_value(x,"name").to_owned()).unwrap_or_else(||id.to_owned())}

pub fn panels(w:i32,h:i32,editing:bool)->[Rect;3] {
    let x=174;let right=330;let gap=16;let available=w-x-22;
    let left=if editing {(available-right-2*gap)/2}else{230};
    let middle=available-left-right-2*gap;
    let top=if SETTINGS.load(Ordering::Relaxed){202}else{150};
    [Rect{left:x,top,right:x+left,bottom:h-60},Rect{left:x+left+gap,top,right:x+left+gap+middle,bottom:h-60},Rect{left:w-22-right,top,right:w-22,bottom:h-60}]
}
unsafe fn place(hwnd:Handle,id:u16,x:i32,y:i32,w:i32,h:i32) {
    let scale=*SCALE.get().unwrap_or(&1.0);let p=|v:i32|(v as f64*scale).round() as i32;
    MoveWindow(GetDlgItem(hwnd,id as i32),p(x),p(y),p(w.max(1)),p(h.max(1)),0);
}
unsafe fn visible(hwnd:Handle,id:u16,on:bool) {
    let buffered=VISIBILITY.with(|v|{let mut v=v.borrow_mut();if let Some(map)=v.as_mut(){map.insert(id,on);true}else{false}});
    if !buffered {let child=GetDlgItem(hwnd,id as i32);if ((GetWindowLongPtrW(child,-16) as u32&WS_VISIBLE)!=0)!=on {ShowWindow(child,if on{5}else{0});}}
}
unsafe fn enabled(hwnd:Handle,id:u16,on:bool) {EnableWindow(GetDlgItem(hwnd,id as i32),on as i32);}

pub unsafe fn create_controls(hwnd:Handle,instance:Handle) {
    for (id,label) in [(402,"＋ 新建预设"),(403,"编辑预设"),(404,"删除"),(405,"资源设置"),(406,"读取人物目录"),(407,"启用人物加载"),(408,"扫描场景资源"),(409,"启用资源控制"),(411,"加入场景"),(412,"查看场景人物"),(414,"身体服装预览"),(415,"头部组合预览"),(417,"上一页"),(418,"下一页"),(419,"重试当前图片"),(452,"上一页"),(453,"下一页"),(454,"上一页"),(455,"下一页"),(457,"保存预设"),(458,"← 返回预设列表"),(459,"跨人物部件：关"),(460,"原始部件组合"),(464,"放大身体图"),(465,"放大头部图"),(489,"撤销删除")] {
        control(hwnd,instance,"BUTTON",label,0,0,100,34,0,id);
    }
    for id in 430..=451 {control(hwnd,instance,"BUTTON","",0,0,100,160,0,id);}
    control(hwnd,instance,"LISTBOX","",0,0,100,200,0x0010|0x0040|0x0100|0x00200000|1,400);
    control(hwnd,instance,"EDIT","",0,0,100,32,WS_BORDER|0x80,401);
    control(hwnd,instance,"EDIT","",0,0,100,36,WS_BORDER|0x80,456);
    SendMessageW(GetDlgItem(hwnd,456),0x00c5,80,0);
    for id in [410,461,462,463] {control(hwnd,instance,"COMBOBOX","",0,0,180,240,3|0x00200000,id);}
    for id in [421,475] {control(hwnd,instance,"EDIT","",0,0,100,100,0x0004|0x0040|0x0800|0x00200000,id);}
    for id in [420,422,423,424,425,470,471,472,473,474,476,477,478,480,481,482,483,484,485,486,487,488] {
        control(hwnd,instance,"STATIC","",0,0,100,24,0,id);
    }
    SendMessageW(GetDlgItem(hwnd,400),0x01a0,0,(48.0 * *SCALE.get().unwrap_or(&1.0)) as isize);
}

impl App {
    pub unsafe fn hide_people(&mut self) {
        if !ACTIVE.swap(false,Ordering::Relaxed){return;}
        for id in FIRST..=LAST {visible(self.hwnd,id,false);}
        place(self.hwnd,330,240,25,500,32);place(self.hwnd,331,240,59,750,24);place(self.hwnd,315,240,808,1150,35);
        for (i,id) in [106,126,107,108].into_iter().enumerate(){place(self.hwnd,id,20,135+i as i32*52,170,44);}
        place(self.hwnd,140,880,97,210,36);InvalidateRect(self.hwnd,null(),1);
    }
    unsafe fn request_icon(&mut self,kind:&str,id:&str) {
        if id.is_empty()||SENDER.get().is_none()||!self.snapshot["admitted"].as_bool().unwrap_or(false){return;}
        let key=if kind=="preset"{format!("preset:{id}")}else{part_key(kind,id)};
        if !self.people.requested.insert(key){return;}
        if kind=="preset"{send(json!({"version":1,"action":"load_thumbnail","preset_id":id}));}
        else {send(json!({"version":1,"action":"load_part_thumbnail","kind":kind,"part_id":id}));}
    }
    pub(super) unsafe fn layout_people(&self) {
        let (w,h)=music_client_size(self.hwnd);let [a,b,c]=panels(w,h,self.editing);
        let top=a.top;let cw=c.right-c.left;let iw=(cw-42)/2;
        for (i,id) in [106,126,107,108].into_iter().enumerate(){place(self.hwnd,id,12,122+i as i32*48,130,40);visible(self.hwnd,id,true);}
        place(self.hwnd,330,174,24,w-510,34);place(self.hwnd,331,174,65,w-210,28);
        place(self.hwnd,315,174,h-44,w-198,30);
        place(self.hwnd,405,w-312,28,132,36);place(self.hwnd,140,w-168,28,145,36);
        place(self.hwnd,425,174,108,w-198,28);
        for (i,id) in [406,407,408,409].into_iter().enumerate(){place(self.hwnd,id,174+i as i32*174,150,164,34);}
        let grid=|pane:Rect,start:u16,y:i32| {
            let row=((pane.bottom-y-82)/3).clamp(110,220);let col=(pane.right-pane.left-40)/2;
            for i in 0..6 {place(self.hwnd,start+i,pane.left+14+(i%2) as i32*(col+12),y+(i/2) as i32*(row+12),col,row);}
        };
        let pager=|pane:Rect,prev:u16,next:u16,label:u16| {
            place(self.hwnd,prev,pane.left+14,pane.bottom-46,84,30);
            place(self.hwnd,next,pane.right-98,pane.bottom-46,84,30);
            place(self.hwnd,label,pane.left+106,pane.bottom-42,pane.right-pane.left-212,24);
        };
        if !self.editing {
            place(self.hwnd,483,a.left+14,top+18,a.right-a.left-28,26);
            place(self.hwnd,484,a.left+14,top+57,a.right-a.left-28,22);
            place(self.hwnd,401,a.left+14,top+84,a.right-a.left-28,34);
            place(self.hwnd,400,a.left+8,top+130,a.right-a.left-16,a.bottom-top-144);
            place(self.hwnd,482,b.left+14,top+18,b.right-b.left-180,28);
            place(self.hwnd,402,b.right-153,top+13,139,36);
            place(self.hwnd,422,b.left+20,top+80,b.right-b.left-40,200);
            grid(b,430,top+66);pager(b,417,418,487);
            place(self.hwnd,489,(b.left+b.right-110)/2,b.bottom-46,110,30);
            place(self.hwnd,481,c.left+14,top+18,cw-28,28);
            place(self.hwnd,414,c.left+14,top+58,iw,130);place(self.hwnd,415,c.left+28+iw,top+58,iw,130);
            let edit=c.bottom-322;
            place(self.hwnd,421,c.left+14,top+202,cw-28,(edit-top-214).max(70));
            place(self.hwnd,403,c.left+14,edit,110,32);place(self.hwnd,404,c.right-100,edit,86,32);
            place(self.hwnd,480,c.left+14,edit+50,cw-28,24);
            place(self.hwnd,410,c.left+14,edit+78,cw-28,240);
            place(self.hwnd,424,c.left+14,edit+117,cw-28,48);
            place(self.hwnd,411,c.left+14,edit+175,cw-28,42);
            place(self.hwnd,423,c.left+14,edit+225,cw-28,48);
            place(self.hwnd,419,c.left+14,c.bottom-44,132,30);place(self.hwnd,412,c.left+156,c.bottom-44,cw-170,30);
        } else {
            for (pane,start,heading,prev,next,label) in [(a,440,470,452,453,476),(b,446,471,454,455,477)] {
                place(self.hwnd,heading,pane.left+14,top+18,pane.right-pane.left-28,28);
                grid(pane,start,top+102);pager(pane,prev,next,label);
            }
            place(self.hwnd,459,a.left+14,top+55,160,32);place(self.hwnd,460,b.left+14,top+55,160,32);
            place(self.hwnd,485,a.left+16,top+104,b.right-a.left-32,60);
            for (i,id) in [461,462,463].into_iter().enumerate(){place(self.hwnd,id,a.left+20,top+188+i as i32*100,b.right-a.left-40,240);}
            place(self.hwnd,478,a.left+16,a.bottom-100,b.right-a.left-32,48);
            place(self.hwnd,486,a.left+14,top+108,a.right-a.left-28,140);place(self.hwnd,488,b.left+14,top+108,b.right-b.left-28,140);
            place(self.hwnd,472,c.left+14,top+18,cw-28,28);
            place(self.hwnd,414,c.left+14,top+58,iw,120);place(self.hwnd,415,c.left+28+iw,top+58,iw,120);
            place(self.hwnd,464,c.left+14,top+190,iw,30);place(self.hwnd,465,c.left+28+iw,top+190,iw,30);
            place(self.hwnd,475,c.left+14,top+234,cw-28,132);
            place(self.hwnd,473,c.left+14,top+382,cw-28,24);place(self.hwnd,456,c.left+14,top+412,cw-28,36);
            place(self.hwnd,457,c.left+14,top+464,cw-28,42);place(self.hwnd,474,c.left+14,top+518,cw-28,56);
            place(self.hwnd,458,c.left+14,c.bottom-46,cw-28,32);
        }
    }
    pub unsafe fn render_people(&mut self) {
        VISIBILITY.with(|v|*v.borrow_mut()=Some(Default::default()));
        ACTIVE.store(true,Ordering::Relaxed);EDITOR.store(self.editing,Ordering::Relaxed);
        SETTINGS.store(self.people.settings,Ordering::Relaxed);RAW.store(self.people.raw_mode,Ordering::Relaxed);
        let available=SENDER.get().is_some();
        let connected=available&&self.snapshot["admitted"].as_bool().unwrap_or(false);
        let ready=connected&&!self.snapshot["busy"].as_bool().unwrap_or(false)&&!self.snapshot["playing"].as_bool().unwrap_or(false)&&!self.snapshot["faulted"].as_bool().unwrap_or(false);
        let writable=self.snapshot["writable"].as_bool().unwrap_or(false);
        if let Some((expected,since))=&self.people.pending {
            if self.snapshot["presets"].as_array().is_some_and(|ps|ps.iter().any(|p|same_preset(p,expected))){
                self.selected_preset=text_value(expected,"id").into();self.selected_character=text_value(expected,"character").into();self.editing=false;self.people.feedback=format!("已保存并选择“{}”，可继续加入场景。",text_value(expected,"name"));self.people.pending=None;KillTimer(self.hwnd,2);
            }else if since.elapsed()>Duration::from_secs(8){self.people.pending=None;self.people.feedback="未确认预设保存成功，请检查下方存储状态后重试。".into();KillTimer(self.hwnd,2);}
        }
        EDITOR.store(self.editing,Ordering::Relaxed);
        for id in 100..=336 {if !matches!(id,106|126|107|108|140|315|330|331){visible(self.hwnd,id,false);}}
        for picture in [self.picture,self.picture_status,self.head_picture,self.head_status]{ShowWindow(picture,0);}
        for id in FIRST..=LAST {visible(self.hwnd,id,false);}
        for id in [106,126,107,108,140,315,330,331,405,425]{visible(self.hwnd,id,true);}
        if self.people.settings {for id in [406,407,408,409]{visible(self.hwnd,id,true);}}
        NAV_SELECTED.store(106,Ordering::Relaxed);
        let signature=format!("{}:{}",self.snapshot["admitted"],self.snapshot["catalog_revision"]);
        if signature!=self.people.catalog_signature {self.people.cache.clear();self.people.requested.clear();self.people.catalog_signature=signature;}
        let all_chars=self.snapshot["characters"].as_array().cloned().unwrap_or_default();
        if !all_chars.iter().any(|c|text_value(c,"id")==self.selected_character){self.selected_character=all_chars.first().map(|c|text_value(c,"id").to_owned()).unwrap_or_default();}
        self.presets=self.snapshot["presets"].as_array().map(|ps|ps.iter().filter(|p|text_value(p,"character")==self.selected_character).cloned().collect()).unwrap_or_default();
        if !self.presets.iter().any(|p|text_value(p,"id")==self.selected_preset){self.selected_preset=self.presets.first().map(|p|text_value(p,"id").to_owned()).unwrap_or_default();}
        self.people.preset_page=self.people.preset_page.min(last_page(self.presets.len()));
        if !self.editing {if let Some(i)=self.presets.iter().position(|p|text_value(p,"id")==self.selected_preset){self.people.preset_page=i/PAGE;}}
        let name=char_name(&self.snapshot,&self.selected_character);
        text(GetDlgItem(self.hwnd,330),if self.editing {"新建 / 编辑人物预设"}else{"人物与预设"});
        text(GetDlgItem(self.hwnd,331),if self.editing {"看系统预览图选择身体与头部，确认组合后保存。"}else{"选择人物和造型，确认放置位置后加入场景。"});
        let catalog_loaded=self.snapshot["catalog_revision"].as_u64().unwrap_or(0)>0;
        text(GetDlgItem(self.hwnd,425),if !connected {"可编辑已保存预设；场景操作需进入拍摄 / Live"}else if !catalog_loaded {if self.snapshot["catalog_auto_pending"]==false{"自动读取未完成，可手动重试"}else{"正在准备自动读取人物目录…"}}else{"人物目录已就绪"});
        text(GetDlgItem(self.hwnd,406),if catalog_loaded{"重新读取目录"}else{"读取人物目录"});
        text(GetDlgItem(self.hwnd,407),if self.snapshot["creation_enabled"]==true {"人物加载：已开启"}else{"启用人物加载"});
        text(GetDlgItem(self.hwnd,409),if self.snapshot["resource_enabled"]==true {"资源控制：已开启"}else{"启用资源控制"});
        text(GetDlgItem(self.hwnd,140),if self.snapshot["free_camera_active"]==true {"关闭自由视角"}else{"开启自由视角"});
        for id in [406,408,409]{enabled(self.hwnd,id,connected);}enabled(self.hwnd,407,ready);enabled(self.hwnd,140,connected||self.snapshot["free_camera_active"]==true);
        let status=if !self.people.feedback.is_empty(){self.people.feedback.clone()}else if !text_value(&self.snapshot,"notice").is_empty(){text_value(&self.snapshot,"notice").into()}else{text_value(&self.snapshot,"status").into()};
        let store=text_value(&self.snapshot,"store_notice");text(self.status,&format!("{}{}{}",status,if store.is_empty(){""}else{" · "},store));
        if self.editing {
            self.render_editor(available,writable);
        } else {
            for id in [400,401,402,403,404,410,411,412,414,415,417,418,419,421,423,424,480,481,482,483,484,487]{visible(self.hwnd,id,true);}
            let filter=get_text(GetDlgItem(self.hwnd,401)).trim().to_lowercase();self.characters.clear();let mut rows=Vec::new();
            for c in &all_chars{let id=text_value(c,"id");let n=text_value(c,"name");if !filter.is_empty()&&!n.to_lowercase().contains(&filter)&&!id.contains(&filter){continue;}let count=self.snapshot["presets"].as_array().map(|ps|ps.iter().filter(|p|text_value(p,"character")==id).count()).unwrap_or(0);self.characters.push(id.into());rows.push(format!("{}  · {} 个造型",n,count));}
            sync_resource_list(GetDlgItem(self.hwnd,400),rows,&mut self.people.character_rows,self.characters.iter().position(|x|x==&self.selected_character),false);
            text(GetDlgItem(self.hwnd,483),"01 / 选择人物");text(GetDlgItem(self.hwnd,484),"搜索人物");text(GetDlgItem(self.hwnd,482),&format!("{}的造型 · {}",name,self.presets.len()));
            text(GetDlgItem(self.hwnd,481),"当前造型 · 身体 / 头部");text(GetDlgItem(self.hwnd,480),"03 / 放置位置");
            if self.presets.is_empty(){visible(self.hwnd,422,true);text(GetDlgItem(self.hwnd,422),if all_chars.is_empty(){"人物目录会在游戏场景就绪后自动读取。\r\n\r\n也可打开“资源设置”，点击“读取人物目录”手动重试。"}else{"这个人物还没有造型预设。\r\n\r\n点击“新建预设”，看图选择身体和头部。"});}
            let page=page_slice(&self.presets,self.people.preset_page).to_vec();for (i,p) in page.iter().enumerate(){let id=430+i as u16;visible(self.hwnd,id,true);text(GetDlgItem(self.hwnd,id as i32),&format!("{}{}",text_value(p,"name"),if text_value(p,"id")==self.selected_preset{" · 已选择"}else{""}));self.request_icon("preset",text_value(p,"id"));}
            text(GetDlgItem(self.hwnd,487),&format!("{} / {}",self.people.preset_page+1,last_page(self.presets.len())+1));enabled(self.hwnd,417,self.people.preset_page>0);enabled(self.hwnd,418,self.people.preset_page<last_page(self.presets.len()));
            let preset=self.presets.iter().find(|p|text_value(p,"id")==self.selected_preset).cloned();
            let desc=preset.as_ref().map(|p|format!("{} · {}\r\n\r\n身体：{}\r\n头部：{}",name,text_value(p,"name"),if text_value(p,"body").is_empty(){text_value(p,"bodyAsset")}else{text_value(p,"body")},if text_value(p,"head").is_empty(){text_value(p,"hairAsset")}else{text_value(p,"head")})).unwrap_or_else(||"选择或创建造型后，在这里确认。".into());text(GetDlgItem(self.hwnd,421),&desc);
            enabled(self.hwnd,402,available&&!self.selected_character.is_empty());enabled(self.hwnd,403,preset.as_ref().is_some_and(|p|text_value(p,"factory").is_empty()));enabled(self.hwnd,404,writable&&available&&preset.as_ref().is_some_and(|p|p["builtin"]!=true));
            let actors=self.snapshot["actors"].as_array().cloned().unwrap_or_default();let ids:Vec<_>=actors.iter().filter_map(|a|a["instance"].as_i64()).filter(|i|*i!=0).collect();
            let labels:Vec<_>=actors.iter().filter(|a|a["instance"].as_i64().unwrap_or(0)!=0).map(|a|format!("与{}同位置",text_value(a,"name"))).collect();
            if ids!=self.actors||labels!=self.actor_labels{self.actors=ids;self.actor_labels=labels;SendMessageW(GetDlgItem(self.hwnd,410),CB_RESETCONTENT,0,0);for label in &self.actor_labels{combo_add(GetDlgItem(self.hwnd,410),label);}}
            if !self.actors.contains(&self.selected_reference){self.selected_reference=self.actors.first().copied().unwrap_or(0);}if let Some(i)=self.actors.iter().position(|i|*i==self.selected_reference){SendMessageW(GetDlgItem(self.hwnd,410),CB_SETCURSEL,i,0);}
            text(GetDlgItem(self.hwnd,424),"加入到参考人物当前位置；可能重叠，之后可在场景人物中调整站位。");
            let reason=if !connected{"请先进入拍摄 / Live 场景。"}else if preset.is_none(){"请先选择或创建一个造型。"}else if self.snapshot["playing"]==true{"请先停止动作播放。"}else if self.snapshot["busy"]==true{"人物正在构建，请稍候。"}else if self.snapshot["faulted"]==true{"人物操作已锁定，请查看状态。"}else if self.snapshot["creation_enabled"]!=true{"请在资源设置中启用人物加载。"}else if self.selected_reference==0{"场景中还没有可用的参考人物。"}else{"已选齐，可以加入场景。"};text(GetDlgItem(self.hwnd,423),reason);enabled(self.hwnd,411,ready&&preset.is_some()&&self.snapshot["creation_enabled"]==true&&self.selected_reference!=0);
            let can_undo=self.people.undo.as_ref().is_some_and(|p|!self.snapshot["presets"].as_array().is_some_and(|ps|ps.iter().any(|x|text_value(x,"id")==text_value(p,"id"))));visible(self.hwnd,489,can_undo);visible(self.hwnd,487,!can_undo);enabled(self.hwnd,489,writable&&available);
        }
        RAW.store(self.people.raw_mode,Ordering::Relaxed);
        self.layout_people();
        let visibility=VISIBILITY.with(|v|v.borrow_mut().take().unwrap_or_default());
        for (id,on) in visibility {visible(self.hwnd,id,on);}
        RedrawWindow(self.hwnd,null(),null_mut(),0x0001|0x0004|0x0080);
        for id in [414,415].into_iter().chain(430..=451){InvalidateRect(GetDlgItem(self.hwnd,id),null(),0);}
    }
    unsafe fn render_editor(&mut self,connected:bool,writable:bool) {
        for id in [414,415,456,457,458,459,460,464,465,470,471,472,473,474,475]{visible(self.hwnd,id,true);}
        self.people.bodies=choices(&self.snapshot,&self.selected_character,self.cross_character,"bodies");self.people.heads=choices(&self.snapshot,&self.selected_character,self.cross_character,"heads");
        if self.people.reveal_selection {if let Some(i)=self.people.bodies.iter().position(|p|text_value(p,"id")==self.people.body){self.people.body_page=i/PAGE;}if let Some(i)=self.people.heads.iter().position(|p|text_value(p,"id")==self.people.head){self.people.head_page=i/PAGE;}self.people.reveal_selection=false;}
        if !self.people.bodies.iter().any(|p|text_value(p,"id")==self.people.body){self.people.body.clear();}if !self.people.heads.iter().any(|p|text_value(p,"id")==self.people.head){self.people.head.clear();}
        self.people.body_page=self.people.body_page.min(last_page(self.people.bodies.len()));self.people.head_page=self.people.head_page.min(last_page(self.people.heads.len()));
        text(GetDlgItem(self.hwnd,470),&format!("01 / 身体服装 · {}",self.people.bodies.len()));text(GetDlgItem(self.hwnd,471),&format!("02 / 头部组合 · {}",self.people.heads.len()));text(GetDlgItem(self.hwnd,472),"已选模型组合");text(GetDlgItem(self.hwnd,473),"预设名称（留空时自动命名）");
        text(GetDlgItem(self.hwnd,459),if self.cross_character{"跨人物部件：开"}else{"跨人物部件：关"});text(GetDlgItem(self.hwnd,460),if self.people.raw_mode{"返回图标选择"}else{"原始部件组合"});
        if self.people.raw_mode {
            for id in [461,462,463,478,485]{visible(self.hwnd,id,true);}
            text(GetDlgItem(self.hwnd,485),"原始部件组合（高级）\r\n分别选择身体、脸部和头发。没有官方图标时会明确显示不可用，不用虚构图片代替。");text(GetDlgItem(self.hwnd,478),"从上到下依次为：身体、脸部、头发。跨人物组合的兼容性由实际资源决定。");
            for i in 0..3 {
                let kind=["body","face","hair"][i];let mut items:Vec<_>=self.snapshot["resources"].as_array().map(|xs|xs.iter().filter(|x|text_value(x,"kind")==kind&&(self.cross_character||text_value(x,"character")==self.selected_character)).map(|x|text_value(x,"resource").to_owned()).collect()).unwrap_or_default();items.sort();items.dedup();
                if items!=self.people.raw_items[i]{self.people.raw_items[i]=items;let combo=GetDlgItem(self.hwnd,461+i as i32);SendMessageW(combo,CB_RESETCONTENT,0,0);for s in &self.people.raw_items[i]{combo_add(combo,s);}if let Some(n)=self.people.raw_items[i].iter().position(|s|s==&self.people.raw[i]){SendMessageW(combo,CB_SETCURSEL,n,0);}else{self.people.raw[i].clear();}}
                let wanted=self.people.raw_items[i].iter().position(|s|s==&self.people.raw[i]);if wanted.is_none(){self.people.raw[i].clear();}let combo=GetDlgItem(self.hwnd,461+i as i32);if combo_selected(combo)!=wanted{SendMessageW(combo,CB_SETCURSEL,wanted.unwrap_or(usize::MAX),0);}
            }
        } else {
            for id in [452,453,454,455,476,477]{visible(self.hwnd,id,true);}
            for (kind,page,start,items,prev,next,pager,empty) in [("body",self.people.body_page,440,self.people.bodies.clone(),452,453,476,486),("head",self.people.head_page,446,self.people.heads.clone(),454,455,477,488)] {
                for (i,part) in page_slice(&items,page).iter().enumerate(){let id=start+i as u16;visible(self.hwnd,id,true);text(GetDlgItem(self.hwnd,id as i32),&format!("{}{}",text_value(part,"name"),if text_value(part,"id")==if kind=="body"{&self.people.body}else{&self.people.head}{" · 已选择"}else{""}));self.request_icon(kind,text_value(part,"id"));}
                text(GetDlgItem(self.hwnd,pager),&format!("{} / {}",page+1,last_page(items.len())+1));enabled(self.hwnd,prev,page>0);enabled(self.hwnd,next,page<last_page(items.len()));
                if items.is_empty(){visible(self.hwnd,empty,true);text(GetDlgItem(self.hwnd,empty as i32),"此人物没有对应的官方模型记录。\r\n\r\n可开启跨人物部件，或使用原始部件组合。");}
            }
        }
        let b=self.people.bodies.iter().find(|p|text_value(p,"id")==self.people.body);let h=self.people.heads.iter().find(|p|text_value(p,"id")==self.people.head);
        let valid=if self.people.raw_mode{self.people.raw.iter().all(|s|!s.is_empty())}else{b.is_some()&&h.is_some()};
        let info=if self.people.raw_mode {format!("身体：{}\r\n脸部：{}\r\n头发：{}",self.people.raw[0],self.people.raw[1],self.people.raw[2])}else{format!("身体：{}\r\n\r\n头部：{}",b.map(|x|text_value(x,"name")).unwrap_or("尚未选择"),h.map(|x|text_value(x,"name")).unwrap_or("尚未选择"))};text(GetDlgItem(self.hwnd,475),&info);
        let reason=if self.people.pending.is_some(){"正在保存，确认成功后返回预设列表…"}else if !connected{"尚未连接游戏插件。"}else if !writable{"预设存储不可写，请检查下方状态。"}else if !valid{"请选齐身体和头部；高级模式需选齐三个部件。"}else{"模型已选齐。保存后返回列表并选中该预设。"};text(GetDlgItem(self.hwnd,474),reason);enabled(self.hwnd,457,connected&&writable&&valid&&self.people.pending.is_none());enabled(self.hwnd,458,self.people.pending.is_none());
        for id in [459,460,456,461,462,463].into_iter().chain(440..=451){enabled(self.hwnd,id,self.people.pending.is_none());}
        if self.people.pending.is_some(){for id in 452..=455{enabled(self.hwnd,id,false);}}
    }
    unsafe fn begin_editor(&mut self,edit:bool) {
        self.people.raw_mode=false;self.people.body.clear();self.people.head.clear();self.people.body_page=0;self.people.head_page=0;self.people.reveal_selection=true;self.people.raw=std::array::from_fn(|_|String::new());self.people.feedback.clear();self.edit_id.clear();text(GetDlgItem(self.hwnd,456),"");self.editing=true;
        if edit {if let Some(p)=self.presets.iter().find(|p|text_value(p,"id")==self.selected_preset){self.edit_id=if p["builtin"]==true{String::new()}else{text_value(p,"id").into()};self.people.body=text_value(p,"body").into();self.people.head=text_value(p,"head").into();text(GetDlgItem(self.hwnd,456),text_value(p,"name"));if !text_value(p,"bodyAsset").is_empty(){self.people.raw_mode=true;self.people.raw=[text_value(p,"bodyAsset").into(),text_value(p,"faceAsset").into(),text_value(p,"hairAsset").into()];}}}
        self.render_people();
    }
    pub unsafe fn people_command(&mut self,id:u16,notification:u16)->bool {
        if !(FIRST..=LAST).contains(&id){return false;}
        match id {
            401 if notification==0x0300=>self.render_people(),
            400 if notification==1=>{if let Some(character)=selected(GetDlgItem(self.hwnd,400)).and_then(|n|self.characters.get(n)).cloned(){self.selected_character=character;self.selected_preset.clear();self.people.preset_page=0;self.render_people();}},
            402=>self.begin_editor(false),403=>self.begin_editor(true),
            405=>{self.people.settings=!self.people.settings;self.render_people();},
            406=>{self.people.feedback.clear();self.people.cache.clear();self.people.requested.clear();send(json!({"version":1,"action":"scan"}));},
            407=>send(json!({"version":1,"action":"enable_creation","enabled":self.snapshot["creation_enabled"]!=true})),
            408=>send(json!({"version":1,"action":"scan_resources"})),
            409=>send(json!({"version":1,"action":"enable_resources","enabled":self.snapshot["resource_enabled"]!=true&&self.snapshot["resource_enable_pending"]!=true})),
            410 if notification==1=>{self.selected_reference=combo_selected(GetDlgItem(self.hwnd,410)).and_then(|i|self.actors.get(i)).copied().unwrap_or(0);self.render_people();},
            411=>{self.people.feedback.clear();send(json!({"version":1,"action":"spawn","preset_id":self.selected_preset,"instance":self.selected_reference}));},
            412=>{self.view=View::Actors;self.render();},
            417|418=>{let page=self.people.preset_page;self.people.preset_page=if id==417{page.saturating_sub(1)}else{(page+1).min(last_page(self.presets.len()))};if let Some(p)=self.presets.get(self.people.preset_page*PAGE){self.selected_preset=text_value(p,"id").into();}self.render_people();},
            430..=435=>{if let Some(p)=self.presets.get(self.people.preset_page*PAGE+(id-430) as usize){self.selected_preset=text_value(p,"id").into();}self.render_people();},
            440..=451=>{if self.people.pending.is_none(){if id<446 {if let Some(p)=self.people.bodies.get(self.people.body_page*PAGE+(id-440) as usize){self.people.body=text_value(p,"id").into();}}else if let Some(p)=self.people.heads.get(self.people.head_page*PAGE+(id-446) as usize){self.people.head=text_value(p,"id").into();}self.render_people();}},
            452=>{self.people.body_page=self.people.body_page.saturating_sub(1);self.render_people();},453=>{self.people.body_page=(self.people.body_page+1).min(last_page(self.people.bodies.len()));self.render_people();},454=>{self.people.head_page=self.people.head_page.saturating_sub(1);self.render_people();},455=>{self.people.head_page=(self.people.head_page+1).min(last_page(self.people.heads.len()));self.render_people();},
            458=>{if self.people.pending.is_none(){self.editing=false;self.people.feedback.clear();self.render_people();}},
            459=>{self.cross_character=!self.cross_character;self.render_people();},
            460=>{self.people.raw_mode=!self.people.raw_mode;self.render_people();},
            461..=463 if notification==1=>{let i=(id-461) as usize;self.people.raw[i]=combo_selected(GetDlgItem(self.hwnd,id as i32)).and_then(|n|self.people.raw_items[i].get(n)).cloned().unwrap_or_default();self.render_people();},
            457=>self.save_editor(),
            404=>{if let Some(p)=self.presets.iter().find(|p|text_value(p,"id")==self.selected_preset&&p["builtin"]!=true).cloned(){if MessageBoxW(self.hwnd,wide("删除此自定义预设？场景中的人物保持不变，可撤销删除。").as_ptr(),wide("删除预设").as_ptr(),0x34)==6{self.people.undo=Some(p.clone());self.people.feedback="正在删除预设，场景人物保持不变。".into();send(json!({"version":1,"action":"delete_preset","preset_id":p["id"]}));}}},
            489=>{if let Some(p)=self.people.undo.take(){self.selected_preset=text_value(&p,"id").into();self.people.pending=Some((p.clone(),Instant::now()));send(json!({"version":1,"action":"save_preset","preset":p}));SetTimer(self.hwnd,2,250,null_mut());}},
            419=>{self.people.cache.clear();self.people.requested.clear();self.render_people();},
            414|415|464|465=>{let head=id==415||id==465;if let Some(image)=self.people_image(head).cloned(){show_zoom(self.hwnd,image,if head{"头部模型系统预览图"}else{"身体模型系统预览图"});}else{self.people.feedback="系统图标尚不可用，可点击重试图片，或在游戏中打开对应服装页后重试。".into();self.render_people();}},
            _=>{},
        }
        true
    }
    unsafe fn save_editor(&mut self) {
        if self.people.pending.is_some(){return;}
        let body=self.people.bodies.iter().find(|p|text_value(p,"id")==self.people.body);let head=self.people.heads.iter().find(|p|text_value(p,"id")==self.people.head);
        let mut name=get_text(GetDlgItem(self.hwnd,456)).trim().to_owned();if name.is_empty(){name=if self.people.raw_mode{format!("{} · 自定义组合",char_name(&self.snapshot,&self.selected_character))}else{format!("{} · {}",body.map(|p|text_value(p,"name")).unwrap_or(""),head.map(|p|text_value(p,"name")).unwrap_or(""))};}
        if name.len()>160||name.chars().any(char::is_control){self.people.feedback="预设名称过长或含有控制字符，请缩短名称后保存。".into();self.render_people();return;}
        let id=if self.edit_id.is_empty(){format!("user-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos())}else{self.edit_id.clone()};
        let mut p=json!({"id":id,"name":name,"character":self.selected_character,"body":"","head":"","factory":"","bodyAsset":"","faceAsset":"","hairAsset":""});
        if self.people.raw_mode {if self.people.raw.iter().any(String::is_empty){return;}p["bodyAsset"]=self.people.raw[0].clone().into();p["faceAsset"]=self.people.raw[1].clone().into();p["hairAsset"]=self.people.raw[2].clone().into();}else{let (Some(body),Some(head))=(body,head)else{return;};p["body"]=body["id"].clone();p["head"]=head["id"].clone();}
        self.people.pending=Some((p.clone(),Instant::now()));self.people.feedback.clear();send(json!({"version":1,"action":"save_preset","preset":p}));SetTimer(self.hwnd,2,250,null_mut());self.render_people();
    }
    fn people_image(&self,head:bool)->Option<&thumbnail::Bitmap> {
        if self.editing {if self.people.raw_mode{return None;}let key=if head{part_key("head",&self.people.head)}else{part_key("body",&self.people.body)};self.people.cache.get(&key).and_then(|r|if head{r.head_bitmap.as_ref()}else{r.bitmap.as_ref()})}
        else {self.people.cache.get(&format!("preset:{}",self.selected_preset)).and_then(|r|if head{r.head_bitmap.as_ref()}else{r.bitmap.as_ref()})}
    }
}

pub unsafe fn paint(dc:Handle,hwnd:Handle) {
    let scale=*SCALE.get().unwrap_or(&1.0);let p=|v:i32|(v as f64*scale).round() as i32;let (w,h)=music_client_size(hwnd);
    let sidebar=CreateSolidBrush(rgb(23,34,57));let rect=Rect{left:0,top:0,right:p(154),bottom:p(h)};FillRect(dc,&rect,sidebar);DeleteObject(sidebar);
    for r in panels(w,h,EDITOR.load(Ordering::Relaxed)){rounded(dc,Rect{left:p(r.left),top:p(r.top),right:p(r.right),bottom:p(r.bottom)},rgb(255,255,255),rgb(220,227,238),p(10));}
    SetBkMode(dc,1);SetTextColor(dc,rgb(230,237,248));if let Some(font)=HEADING_FONT.get(){SelectObject(dc,*font as Handle);}let mut title=Rect{left:p(16),top:p(30),right:p(150),bottom:p(60)};DrawTextW(dc,wide("GAKU").as_ptr(),-1,&mut title,0x24);
    if let Some(font)=FONT.get(){SelectObject(dc,*font as Handle);}SetTextColor(dc,rgb(172,190,216));let mut sub=Rect{left:p(17),top:p(69),right:p(150),bottom:p(95)};DrawTextW(dc,wide("场景资源管理").as_ptr(),-1,&mut sub,0x24);
}
unsafe fn label(dc:Handle,value:&str,rect:Rect,color:u32,flags:u32){SetBkMode(dc,1);SetTextColor(dc,color);if let Some(font)=FONT.get(){SelectObject(dc,*font as Handle);}let mut r=rect;DrawTextW(dc,wide(value).as_ptr(),-1,&mut r,flags|0x0800);}
unsafe fn bitmap(dc:Handle,image:&thumbnail::Bitmap,rect:Rect){
    let factor=((rect.right-rect.left) as f64/image.width as f64).min((rect.bottom-rect.top) as f64/image.height as f64);
    let w=(image.width as f64*factor).round().max(1.0) as i32;let h=(image.height as f64*factor).round().max(1.0) as i32;
    let info=BitmapInfo{header:BitmapHeader{size:40,width:image.width as i32,height:-(image.height as i32),planes:1,bits:32,compression:0,image_size:0,xppm:0,yppm:0,used:0,important:0},colors:[0]};
    StretchDIBits(dc,rect.left+(rect.right-rect.left-w)/2,rect.top+(rect.bottom-rect.top-h)/2,w,h,0,0,image.width as i32,image.height as i32,image.bgra.as_ptr() as _,&info,0,0x00cc0020);
}
unsafe fn icon(dc:Handle,result:Option<&ThumbResult>,head:bool,rect:Rect){if let Some(image)=result.and_then(|r|if head{r.head_bitmap.as_ref()}else{r.bitmap.as_ref()}){bitmap(dc,image,rect);}else{let status=result.map(|r|if head{r.head_status.as_str()}else{r.status.as_str()}).filter(|s|!s.is_empty()).unwrap_or("等待系统预览图");label(dc,status,rect,rgb(92,107,131),0x10|0x1);}}
pub unsafe fn draw_card(item:&DrawItem)->bool {
    let id=item.ctl_id as u16;if !active()||!matches!(id,414|415|430..=451){return false;}
    APP.with(|cell|{let Ok(app)=cell.try_borrow()else{return false;};let Some(a)=app.as_ref()else{return false;};
        let scale=*SCALE.get().unwrap_or(&1.0);let p=|v:i32|(v as f64*scale).round() as i32;let r=item.rect;
        let (name,key,head,selected,pair)=if (430..=435).contains(&id){let Some(v)=a.presets.get(a.people.preset_page*PAGE+(id-430) as usize)else{return true;};(text_value(v,"name"),format!("preset:{}",text_value(v,"id")),false,text_value(v,"id")==a.selected_preset,true)}
        else if (440..=451).contains(&id){let head=id>=446;let (xs,page,start,selected)=if head{(&a.people.heads,a.people.head_page,446,&a.people.head)}else{(&a.people.bodies,a.people.body_page,440,&a.people.body)};let Some(v)=xs.get(page*PAGE+(id-start) as usize)else{return true;};(text_value(v,"name"),part_key(if head{"head"}else{"body"},text_value(v,"id")),head,text_value(v,"id")==selected,false)}
        else {let head=id==415;let key=if a.editing{if a.people.raw_mode{String::new()}else{part_key(if head{"head"}else{"body"},if head{&a.people.head}else{&a.people.body})}}else{format!("preset:{}",a.selected_preset)};(if head{"头部预览"}else{"身体预览"},key,head,false,false)};
        rounded(item.dc,r,if selected{rgb(237,242,255)}else{rgb(248,250,253)},if selected{rgb(53,91,211)}else{rgb(220,227,238)},p(7));
        let ir=Rect{left:r.left+p(9),top:r.top+p(9),right:r.right-p(9),bottom:r.bottom-p(56)};let result=a.people.cache.get(&key);
        if pair{let mid=(ir.left+ir.right)/2;icon(item.dc,result,false,Rect{right:mid-p(3),..ir});icon(item.dc,result,true,Rect{left:mid+p(3),..ir});}else{icon(item.dc,result,head,ir);}
        let name=if selected{format!("✓ {}",name)}else{name.into()};label(item.dc,&name,Rect{left:r.left+p(9),top:r.bottom-p(50),right:r.right-p(9),bottom:r.bottom-p(5)},if selected{rgb(49,78,170)}else{rgb(35,51,78)},0x10|0x1|0x8000);
        if item.state&0x10!=0{let mut focus=r;focus.left+=p(3);focus.top+=p(3);focus.right-=p(3);focus.bottom-=p(3);DrawFocusRect(item.dc,&focus);}
        true
    })
}
#[link(name="user32")]
extern "system"{fn DrawFocusRect(dc:Handle,rect:*const Rect)->i32;}
unsafe extern "system" fn zoom_proc(hwnd:Handle,msg:u32,wp:usize,lp:isize)->isize {
    let ptr=GetWindowLongPtrW(hwnd,-21) as *mut thumbnail::Bitmap;
    match msg {0x000f if !ptr.is_null()=>{let mut ps:PaintStruct=std::mem::zeroed();let dc=BeginPaint(hwnd,&mut ps);let mut rect=Rect{left:0,top:0,right:0,bottom:0};GetClientRect(hwnd,&mut rect);FillRect(dc,&rect,*BACK_BRUSH.get().unwrap_or(&0) as Handle);rect.left+=20;rect.top+=20;rect.right-=20;rect.bottom-=20;bitmap(dc,&*ptr,rect);EndPaint(hwnd,&ps);0},0x0005=>{InvalidateRect(hwnd,null(),1);0},0x0100 if wp==27=>{DestroyWindow(hwnd);0},0x0082=>{SetWindowLongPtrW(hwnd,-21,0);if !ptr.is_null(){drop(Box::from_raw(ptr));}DefWindowProcW(hwnd,msg,wp,lp)},_=>DefWindowProcW(hwnd,msg,wp,lp)}
}
unsafe fn show_zoom(owner:Handle,image:thumbnail::Bitmap,title:&str){
    let instance=GetModuleHandleW(null());let name=wide("GakuActors.SystemIconPreview");let class=WndClass{style:0,proc:Some(zoom_proc),class_extra:0,window_extra:0,instance,icon:null_mut(),cursor:LoadCursorW(null_mut(),IDC_ARROW as _),background:6usize as _,menu_name:null(),class_name:name.as_ptr()};RegisterClassW(&class);
    let hwnd=CreateWindowExW(0,name.as_ptr(),wide(title).as_ptr(),WS_OVERLAPPEDWINDOW,0x80000000u32 as i32,0x80000000u32 as i32,540,540,owner,null_mut(),instance,null_mut());if !hwnd.is_null(){SetWindowLongPtrW(hwnd,-21,Box::into_raw(Box::new(image)) as isize);ShowWindow(hwnd,1);UpdateWindow(hwnd);}
}

#[cfg(test)]
mod tests{
    use super::*;
    #[test]fn columns_and_pagers_fit(){for (w,h) in [(1200,790),(1440,860),(1920,1040)]{for editing in [false,true]{let ps=panels(w,h,editing);assert!(ps[0].left>=154);for p in ps{assert!(p.right<=w-20&&p.bottom<=h-44&&p.right-p.left>=190);}assert!(ps[0].right<ps[1].left&&ps[1].right<ps[2].left);}}let rows:Vec<_>=(0..13).collect();assert_eq!(page_slice(&rows,0),&[0,1,2,3,4,5]);assert_eq!(page_slice(&rows,2),&[12]);assert!(page_slice(&rows,3).is_empty());}
    #[test]fn choices_use_native_relations(){let snapshot=json!({"bodies":[{"id":"a","character":"one","thumbnail":"native-a"},{"id":"b","character":"two","thumbnail":"native-b"}]});let own=choices(&snapshot,"one",false,"bodies");assert_eq!(own.len(),1);assert_eq!(own[0]["thumbnail"],"native-a");assert_eq!(choices(&snapshot,"one",true,"bodies").len(),2);}
    #[test]fn save_receipt_requires_exact_combination(){let saved=json!({"id":"p","name":"n","body":"b","head":"h"});let mut different=saved.clone();different["head"]="other".into();assert!(!same_preset(&saved,&different));assert!(same_preset(&saved,&saved));}
}
