#![deny(deprecated)]
slint::slint! {
#[edition = "1.19"]
import { LineEdit, ComboBox, CheckBox, ListView, ScrollView, VerticalBox, HorizontalBox, SpinBox, Palette } from "std-widgets.slint";
export struct Entry { key: string, label: string, detail: string, tags: string, selected: bool, depth: int, expandable: bool, expanded: bool, hidden: bool, locked: bool, body: image, head: image, body-ready: bool, head-ready: bool, body-count: int, head-count: int, face-count: int, hair-count: int }
export global ActorsTheme {
    out property <color> accent:#526c80;
    out property <color> text:#34475b;
    out property <color> muted:#738398;
    out property <color> line:#dbe4eb;
    out property <color> paper:#f7f9fa;
    out property <color> panel:#fdfefe;
    out property <color> header:#edf4f8;
    out property <color> aside:#f2f7fa;
    out property <color> soft:#e8f1f6;
    out property <color> selection:#dcebf4;
}
component Button inherits Rectangle {
    in property <string> text;in property <bool> enabled:true;in property <bool> primary:false;
    callback clicked();height:34px;vertical-stretch:0;horizontal-stretch:0;min-width:0px;preferred-width:Math.max(64px,label.preferred-width + 24px);border-radius:8px;border-width:1px;
    background:!root.enabled ? ActorsTheme.aside : root.primary ? ActorsTheme.accent : touch.has-hover ? ActorsTheme.soft : ActorsTheme.panel;
    border-color:root.primary && root.enabled ? ActorsTheme.accent : ActorsTheme.line;
    accessible-role:button;accessible-label:root.text;accessible-enabled:root.enabled;
    label := Text {x:8px;width:Math.max(0px,parent.width - 16px);height:parent.height;text:root.text;font-size:13px;horizontal-alignment:center;vertical-alignment:center;overflow:elide;
        color:!root.enabled ? ActorsTheme.muted : root.primary ? white : ActorsTheme.text;}
    if focus.has-focus: Rectangle {x:2px;y:2px;width:parent.width - 4px;height:parent.height - 4px;border-width:1px;border-color:ActorsTheme.accent;border-radius:6px;background:transparent;}
    focus := FocusScope {enabled:root.enabled;key-pressed(event)=>{if event.text=="\n" || event.text==" "{root.clicked();return accept;}return reject;}
        touch := TouchArea {enabled:root.enabled;clicked=>{focus.focus();root.clicked();}}
    }
}
// Decorative arrow has no input handler: clicks still reach the native ComboBox.
component ThemeComboBox inherits Rectangle {
    in property <[string]> model;
    in-out property <int> current-index: 0;
    callback selected(string);
    height:34px; min-width:0px; vertical-stretch:0; background:transparent;
    ComboBox { x:0px; y:0px; width:parent.width; height:parent.height;
        model:root.model; current-index <=> root.current-index;
        selected(value) => { root.selected(value); }
    }
    Rectangle { x:parent.width - 38px; y:2px; width:36px; height:parent.height - 4px;
        background:#d9e9f2; border-radius:5px;
        Text { width:parent.width; height:parent.height; text:"▼"; font-size:14px; font-weight:700;
            color:ActorsTheme.text; horizontal-alignment:center; vertical-alignment:center; }
    }
}
component DragNumber inherits Rectangle {
    in property <string> axis; in property <string> value; in property <bool> enabled; callback dragged(string, float);
    height: 34px; border-radius: 6px; border-width: 1px; border-color: ActorsTheme.line; background: touch.has-hover && root.enabled ? ActorsTheme.soft : ActorsTheme.aside;
    Text { x: 10px; width: parent.width - 20px; height: parent.height; text: root.axis + "   " + root.value; color: root.enabled ? ActorsTheme.text : ActorsTheme.muted; vertical-alignment: center; }
    touch := TouchArea { enabled: root.enabled; mouse-cursor: ew-resize; property <length> last-x;
        pointer-event(event) => { if event.button == PointerEventButton.left && event.kind == PointerEventKind.down { self.last-x = self.mouse-x; } }
        moved => { if self.pressed { root.dragged(root.axis, (self.mouse-x - self.last-x) / 1px); self.last-x = self.mouse-x; } }
    }
}
component Panel inherits Rectangle { background: ActorsTheme.panel; border-radius: 10px; border-width: 1px; border-color: ActorsTheme.line; }
component Caption inherits Text { color: ActorsTheme.text; font-size: 17px; font-weight: 600; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component Note inherits Text { color: ActorsTheme.muted; font-size: 13px; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component NavButton inherits Rectangle {
    in property <string> text; in property <bool> selected; callback clicked();
    height:38px; border-radius:6px; background: selected ? ActorsTheme.accent : touch.has-hover ? ActorsTheme.header : transparent;
    accessible-role:button; accessible-label:root.text;
    Text {x:10px;width:parent.width - 20px;height:parent.height;text:root.text;color:root.selected ? white : ActorsTheme.text;vertical-alignment:center;overflow:elide;}
    focus := FocusScope {key-pressed(event)=>{if event.text=="\n" || event.text==" " {root.clicked();return accept;}return reject;}
      touch := TouchArea {clicked=>{focus.focus();root.clicked();}}
    }
}
component Picture inherits Rectangle {
    in property <image> source; in property <bool> ready; in property <string> placeholder: "暂无图片";
    background: ActorsTheme.aside; border-radius: 6px; clip: true; min-width: 0px; min-height: 0px;
    if ready: Image { x: 0px; y: 0px; width: parent.width; height: parent.height; source: parent.source; image-fit: contain; }
    if !ready: Text { x: 4px; y: 0px; width: parent.width - 8px; height: parent.height; text: parent.placeholder; color: ActorsTheme.muted; overflow: elide; horizontal-alignment: center; vertical-alignment: center; }
}
component Card inherits Rectangle {
    in property <Entry> entry; in property <bool> pair: false; callback picked();
    clip: true; background: entry.selected ? ActorsTheme.selection : ActorsTheme.aside; border-width: 1px; border-color: entry.selected ? ActorsTheme.accent : ActorsTheme.line; border-radius: 8px;
    VerticalLayout { padding: 6px; spacing: 4px;
        HorizontalLayout { height: 100px; spacing: 4px;
            Picture { source: entry.body; ready: entry.body-ready; }
            if pair: Picture { source: entry.head; ready: entry.head-ready; }
        }
        Text { text: entry.label; color: ActorsTheme.text; font-size: 13px; height: 24px; min-width: 0px; overflow: elide; horizontal-alignment: center; vertical-alignment: center; }
    }
    TouchArea { clicked => { root.picked(); } }
}
component Gallery inherits ScrollView {
    in property <[Entry]> entries; in property <bool> pair: false; callback picked(string);
    property <length> usable-width: Math.max(1px, self.visible-width - 16px);
    property <int> columns: Math.max(1, Math.floor(self.usable-width / 110px));
    property <length> cell-width: self.usable-width / self.columns;
    content-width: self.usable-width;
    horizontal-scrollbar-policy: always-off;
    content-height: Math.ceil(self.entries.length / self.columns) * 146px;
    for e[index] in root.entries: Card {
        x: Math.mod(index, root.columns) * root.cell-width;
        y: Math.floor(index / root.columns) * 146px;
        width: root.cell-width - 6px; height: 140px;
        entry: e; pair: root.pair; picked => {root.picked(e.key);}
    }
}
export component Manager inherits Window {
    init=>{Palette.color-scheme=ColorScheme.light;}
    icon: @image-url("../../assets/scene-manager-window.png");
    title: "GakuActors · 场景资源管理"; preferred-width: 1320px; preferred-height: 960px; min-width: 880px; min-height: 440px;
    background: ActorsTheme.paper; default-font-family: "Microsoft YaHei UI"; default-font-size: 14px;
    in-out property <int> page: 0;
    in-out property <bool> settings: false;
    in property <bool> in-live; in property <bool> live-pause-available; in property <bool> live-paused; in property <bool> live-pause-owned; in property <string> live-pause-status;
    in property <bool> admitted; in property <bool> ready; in property <bool> writable; in property <bool> creation; in property <bool> resource-enabled; in property <bool> resource-pending; in property <bool> camera-active;
    in property <bool> motion-present; in property <bool> can-scan-catalog; in property <bool> can-undo; in property <bool> actor-owned; in property <bool> pose-supported; in property <bool> save-ready; in property <bool> can-delete; in property <bool> can-hide;
    in property <bool> catalog-failed; in property <int> connection-state: 0; in property <string> connection-label: "正在检测游戏";
    in property <string> status: "等待游戏插件连接"; in property <string> notice; in property <string> detail; in property <string> selection;
    in-out property <string> search; in-out property <string> resource-search; in-out property <string> preset-name;
    in-out property <float> x-pos; in-out property <float> y-pos; in-out property <float> z-pos; in-out property <float> yaw-pos;
    in property <brush> color-preview: #00ff00; in-out property <string> resource-note; in property <bool> note-writable;
    in-out property <string> x-value; in-out property <string> y-value; in-out property <string> z-value; in-out property <string> yaw-value: "0";
    in-out property <bool> cross; in-out property <bool> raw-mode;
    in property <[Entry]> characters; in property <[Entry]> presets; in property <[Entry]> bodies; in property <[Entry]> heads; in property <[Entry]> actors; in property <[Entry]> resources;
    in property <[string]> references; in property <[string]> raw-bodies; in property <[string]> raw-faces; in property <[string]> raw-hairs;
    in-out property <int> reference-index: -1; in-out property <int> body-index: -1; in-out property <int> face-index: -1; in-out property <int> hair-index: -1;
    in-out property <int> stage-index: 0;
    in-out property <string> red-value: "0"; in-out property <string> green-value: "1"; in-out property <string> blue-value: "0";
    in property <string> preset-pager; in property <string> body-pager; in property <string> head-pager;
    in property <string> body-placeholder: "暂无图片"; in property <string> head-placeholder: "暂无图片";
    in property <image> selected-body; in property <image> selected-head; in property <bool> selected-body-ready; in property <bool> selected-head-ready;
    in property <string> confirmation;
    in-out property <bool> zoom-open; in property <image> zoom-image; in property <string> zoom-title;
    in-out property <bool> library-mode: false;
    in-out property <bool> library-show-tasks: false;
    in-out property <string> library-query; in-out property <int> library-category:0; in-out property <int> library-format:0;
    in property <string> library-status: "正在连接资源库…"; in property <string> library-manifest; in property <string> library-page; in property <string> library-selection; in property <string> library-plan; in property <string> library-job-detail; in property <string> library-job-summary: "暂无任务";
    in property <bool> library-can-plan; in property <bool> library-can-submit; in property <bool> library-can-cancel; in property <bool> library-can-retry; in property <bool> library-can-open;
    in property <[Entry]> library-resources; in property <[Entry]> library-jobs; in property <[Entry]> library-selected;
    callback library-action(string, string); callback library-change();
    callback action(string, string); callback changed(string); callback drag-position(string, float);
    if !root.library-mode: VerticalLayout { x:0px; y:0px; width:root.width; height:root.height; spacing:0px;
        Rectangle { height:68px; background:ActorsTheme.header;
          HorizontalLayout {x:16px;y:20px;width:parent.width - 32px;height:36px;spacing:12px;
            Text {text:"GakuActors";font-size:18px;font-weight:600;color:ActorsTheme.text;vertical-alignment:center;}
            Button {text:"游戏管理";primary:true;}
            Button {text:"资源库";clicked=>{root.library-mode=true;root.library-action("refresh", "");}}
            Rectangle {background:transparent;horizontal-stretch:1;}
                Rectangle { width: 160px; height: 36px; background: ActorsTheme.soft; border-radius: 8px;
                    HorizontalLayout { padding: 8px; spacing: 7px;
                        Rectangle { width: 12px; background: transparent; Rectangle { width: 10px; height: 10px; x: 1px; y: (parent.height - self.height) / 2; border-radius: 5px; background: root.connection-state == 2 ? #18a45b : root.connection-state == 1 ? #e1a427 : #d64a4a; } }
                        Text { text: root.connection-label; color: ActorsTheme.text; font-size: 12px; vertical-alignment: center; overflow: elide; }
                    }
                }
                Button {width:96px;height:36px; text: "手动连接"; clicked => {root.action("reconnect", "");} }
                if root.catalog-failed: Button { text: "重试目录"; enabled: root.can-scan-catalog; clicked => {root.action("scan", "");} }
                Button {height:36px;text:root.resource-pending?"等待 Motion 交接":root.motion-present?(root.resource-enabled?"交还 Motion 资源控制":"启用资源控制"):(root.resource-enabled?"资源控制已就绪":"等待进入场景");enabled:root.admitted&&root.motion-present&&!root.resource-pending;clicked=>{root.action("resources","");}}
                Button {width:120px;height:36px; text: root.camera-active ? "关闭自由视角" : "开启自由视角"; enabled: root.admitted; clicked => {root.action("camera", "");} }
          }
        }
        HorizontalLayout {spacing:0px;
          Rectangle {width:root.width < 1100px ? 132px : 168px; background:ActorsTheme.aside;
            VerticalLayout {padding:12px;spacing:8px;
              Note {text:"工作区";height:26px;}
              for label[index] in ["人物与预设", "场景人物", "场景管理", "道具管理", "舞台导入"]: NavButton {text:label;selected:root.page == (index == 4 ? 5 : index) || (root.page == 4 && index == 0);clicked=>{root.action("nav", index == 4 ? 5 : index);}}
              Rectangle {vertical-stretch:1;background:transparent;}
              Button {text:"打开乐库";clicked=>{root.action("music","");}}
              Note {text:"仅在拍摄 / Live 场景启用游戏操作";}
            }
          }
          VerticalLayout {padding:16px;spacing:10px;
            HorizontalLayout {spacing:12px;
              Caption {text:root.page == 4 ? "新建 / 编辑人物预设" : root.page == 0 ? "人物与预设" : root.page == 1 ? "当前场景人物" : root.page == 2 ? "场景管理" : root.page == 5 ? "舞台导入" : "道具管理";horizontal-stretch:1;}
              if root.page == 0 || root.page == 1: Button {text:"读取人物目录"; enabled:root.can-scan-catalog; clicked=>{root.action("scan", "");}}
              if root.page == 0 || root.page == 1: Button {text:root.creation ? "停用人物加载" : "启用人物加载"; enabled:root.ready; clicked=>{root.action("creation", "");}}
            }
            Note { text: root.status; }
            if root.page == 0: HorizontalLayout { spacing: 14px;
                Panel { width: root.width < 1100px ? 202px : 228px; VerticalLayout { padding: 12px; spacing: 10px;
                    Caption { text: "选择人物"; }
                    LineEdit {height:32px;vertical-stretch:0; placeholder-text: "搜索人物"; text <=> root.search; edited => {root.changed("search");} }
                    character-list := ListView { horizontal-scrollbar-policy:always-off; for e in root.characters: Rectangle {
                        width: Math.max(1px,character-list.visible-width - 12px); height:82px; background:e.selected ? ActorsTheme.selection : ActorsTheme.panel;clip:true;
                        VerticalLayout {padding:8px;spacing:3px;alignment:start;
                            Text {text:e.label;overflow:elide;color:ActorsTheme.text;font-size:14px;height:21px;vertical-stretch:0;}
                            HorizontalLayout {height:18px;vertical-stretch:0;spacing:4px;
                                Text {text:"身体 " + e.body-count;color:ActorsTheme.muted;font-size:11px;overflow:elide;horizontal-stretch:1;}
                                Text {text:"头部组合 " + e.head-count;color:ActorsTheme.muted;font-size:11px;overflow:elide;horizontal-stretch:1;}
                            }
                            HorizontalLayout {height:18px;vertical-stretch:0;spacing:4px;
                                Text {text:"脸部 " + e.face-count;color:ActorsTheme.muted;font-size:11px;overflow:elide;horizontal-stretch:1;}
                                Text {text:"发型 " + e.hair-count;color:ActorsTheme.muted;font-size:11px;overflow:elide;horizontal-stretch:1;}
                            }
                        }
                        TouchArea {clicked=>{root.action("character",e.key);}}
                    } }

                } }
                Panel { horizontal-stretch: 1; VerticalLayout { padding: 12px; spacing: 10px;
                    VerticalLayout {spacing:6px;vertical-stretch:0;
                        Caption {text:"造型预设";wrap:no-wrap;}
                        HorizontalLayout {height:34px;vertical-stretch:0;spacing:8px;alignment:end;
                            if root.can-undo: Button {text:"撤销删除";clicked=>{root.action("undo","");}}
                            Button {text:"＋ 新建预设";primary:true;enabled:root.writable;clicked=>{root.action("new","");}}
                        }
                    }
                    if root.presets.length > 0: Gallery { entries: root.presets; pair: true; picked(key) => {root.action("preset",key);} }
                    if root.presets.length == 0: Rectangle {background:transparent;vertical-stretch:1;
                        VerticalLayout {padding:20px;spacing:10px;alignment:center;
                            Text {text:"这个人物还没有预设";color:ActorsTheme.muted;horizontal-alignment:center;vertical-stretch:0;}
                            Note {text:"新建预设后，选择身体与头部并保存。";horizontal-alignment:center;}
                        }
                    }
                    if root.presets.length > 0: HorizontalLayout {height:34px;vertical-stretch:0; Button {text:"上一页";clicked=>{root.action("preset-page","-1");}} Text {text:root.preset-pager;horizontal-alignment:center;vertical-alignment:center;} Button {text:"下一页";clicked=>{root.action("preset-page","1");}} }
                } }
                Panel { width: root.width < 1100px ? 246px : 280px;clip:true; VerticalLayout { padding: 14px; spacing: 12px;
                    Caption { text: "已选造型"; }
                    ScrollView { vertical-stretch: 1; content-width:Math.max(1px,self.visible-width - 16px);horizontal-scrollbar-policy:always-off; VerticalLayout { spacing: 12px; alignment: start;
                        if root.selection!="": HorizontalLayout {height:132px;vertical-stretch:0; spacing: 8px; Picture {source:root.selected-body;ready:root.selected-body-ready;placeholder:root.body-placeholder;height:132px;TouchArea {clicked=>{root.action("zoom","body");}}} Picture {source:root.selected-head;ready:root.selected-head-ready;placeholder:root.head-placeholder;height:132px;TouchArea {clicked=>{root.action("zoom","head");}}} }
                        if root.selection!="": Text {text:root.selection;wrap:word-wrap;color:ActorsTheme.text;vertical-stretch:0;min-width:0px;}
                        if root.detail!="": Note {text:root.detail;}
                        if root.selection!="": HorizontalLayout { spacing: 8px; height: 38px;
                            Button {text:"编辑预设";primary:true;enabled:root.selection!="";clicked=>{root.action("edit","");}}
                            Button {text:"删除预设";enabled:root.can-delete;clicked=>{root.action("delete","");}}
                        }
                        if root.selection=="": Note {text:"在中间选择一个造型，或新建人物预设。";}
                        Caption {text:"加入场景";}
                        Text {text:"参考人物";color:ActorsTheme.text;height:24px;}
                        if root.references.length>0: ThemeComboBox {height:34px;vertical-stretch:0;min-width:0px;model:root.references;current-index <=> root.reference-index;selected=>{root.action("reference","");}}
                        if root.references.length==0: Note {text:root.admitted?"当前未发现人物，请在场景人物页面刷新。":"进入拍摄或 Live 场景后，选择场景中已有的人物。";}
                        Note {text:"新增人物会出现在参考人物的位置，之后可单独调整。";}
                        Button {text:"加入人物";height:40px;primary:true;enabled:root.ready && root.creation && root.reference-index>=0 && root.selection!="";clicked=>{root.action("spawn","");}}
                    } }
                    Button {text:"重试当前图片";clicked=>{root.action("retry","");}}
                } }
            }
            if root.page == 4: VerticalLayout { spacing: 12px;
                HorizontalLayout { CheckBox {text:"跨人物部件";checked <=> root.cross;toggled=>{root.changed("cross");}} CheckBox {text:"原始身体 / 脸 / 头发";checked <=> root.raw-mode;toggled=>{root.changed("raw");}} Rectangle {horizontal-stretch:1;background:transparent;} Button {text:"返回预设列表";clicked=>{root.action("nav","0");}} }
                HorizontalLayout { spacing: 14px;
                    if !root.raw-mode: Panel { min-width:0px; horizontal-stretch:1; VerticalLayout {padding:12px;spacing:10px;
                        Caption {text:"身体服装";}
                        if root.bodies.length==0: Note {text:"没有可用记录，可开启跨人物部件或切换原始部件组合。";}
                        Gallery {entries:root.bodies;picked(key)=>{root.action("body",key);}}
                        HorizontalLayout {Button {text:"上一页";clicked=>{root.action("body-page","-1");}} Text {text:root.body-pager;vertical-alignment:center;horizontal-alignment:center;} Button {text:"下一页";clicked=>{root.action("body-page","1");}}}
                    } }
                    if !root.raw-mode: Panel { min-width:0px; horizontal-stretch:1; VerticalLayout {padding:12px;spacing:10px;
                        Caption {text:"头部组合";}
                        if root.heads.length==0: Note {text:"没有可用记录，可开启跨人物部件或切换原始部件组合。";}
                        Gallery {entries:root.heads;picked(key)=>{root.action("head",key);}}
                        HorizontalLayout {Button {text:"上一页";clicked=>{root.action("head-page","-1");}} Text {text:root.head-pager;vertical-alignment:center;horizontal-alignment:center;} Button {text:"下一页";clicked=>{root.action("head-page","1");}}}
                    } }
                    if root.raw-mode: Panel { horizontal-stretch:1; ScrollView {content-width:Math.max(1px,self.visible-width - 16px);horizontal-scrollbar-policy:always-off; VerticalLayout { padding:18px;spacing:16px;alignment:start;
                        Caption {text:"原始部件组合";} Note {text:"按顺序选择身体、脸部和头发。列表来自游戏目录；跨人物组合的兼容性以游戏结果为准。";}
                        Caption {text:"身体";} ThemeComboBox {min-width:0px;model:root.raw-bodies;current-index <=> root.body-index;selected=>{root.changed("raw-selection");}}
                        Caption {text:"脸部";} ThemeComboBox {min-width:0px;model:root.raw-faces;current-index <=> root.face-index;selected=>{root.changed("raw-selection");}}
                        Caption {text:"头发";} ThemeComboBox {min-width:0px;model:root.raw-hairs;current-index <=> root.hair-index;selected=>{root.changed("raw-selection");}}
                    } } }
                    Panel {width:root.width < 1100px ? 250px : 300px;clip:true;ScrollView {content-width:Math.max(1px,self.visible-width - 16px);horizontal-scrollbar-policy:always-off;VerticalLayout {padding:14px;spacing:12px;alignment:start;
                        Caption {text:"组合与保存";}
                        HorizontalLayout {spacing:8px;Picture {source:root.selected-body;ready:root.selected-body-ready;placeholder:root.body-placeholder;height:130px;TouchArea {clicked=>{root.action("zoom","body");}}} Picture {source:root.selected-head;ready:root.selected-head-ready;placeholder:root.head-placeholder;height:130px;TouchArea {clicked=>{root.action("zoom","head");}}}}
                        if root.detail!="": Note {text:root.detail;}
                        Text {text:"预设名称";color:ActorsTheme.text;}
                        LineEdit {text <=> root.preset-name;placeholder-text:"留空自动命名";}
                        Button {text:"保存预设";enabled:root.save-ready;clicked=>{root.action("save","");}}
                    } } }
                }
            }
            if root.page >= 1 && root.page <= 3: HorizontalLayout {spacing:14px;
                Panel {horizontal-stretch:1;min-width:0px;VerticalLayout {padding:14px;spacing:10px;
                    HorizontalLayout {Caption {text:root.page==1?"场景中的人物":root.page==2?"场景节点":"道具列表";horizontal-stretch:1;} Button {text:root.page==1?"刷新":"扫描场景资源";enabled:root.ready;clicked=>{root.action(root.page==1?"scan-scene":"scan-resources","");}}}
                    if root.page!=1: Note {text:"勾选隐藏，取消勾选恢复";}
                    LineEdit {text <=> root.resource-search;placeholder-text:"按名称或备注筛选";edited=>{root.changed("resource-search");}}
                    ListView {
                        for e in (root.page==1?root.actors:root.resources): Rectangle {height:root.page==1?68px:34px;background:e.selected?ActorsTheme.selection:white;clip:true;
                            HorizontalLayout {padding:6px;spacing:5px;
                                Rectangle {width:Math.min(e.depth, 8)*14px;background:transparent;}
                                if root.page!=1: Rectangle {width:24px;background:transparent;
                                    accessible-role:button;accessible-label:e.expanded ? "收起 " + e.label : "展开 " + e.label;accessible-enabled:e.expandable;
                                    if e.expandable && e.expanded: Path {x:6px;y:(parent.height - 12px)/2;width:12px;height:12px;viewbox-width:12;viewbox-height:12;fill:ActorsTheme.text;commands:"M 2 4 L 10 4 L 6 9 Z";}
                                    if e.expandable && !e.expanded: Path {x:6px;y:(parent.height - 12px)/2;width:12px;height:12px;viewbox-width:12;viewbox-height:12;fill:ActorsTheme.text;commands:"M 4 2 L 9 6 L 4 10 Z";}
                                    TouchArea {enabled:e.expandable;clicked=>{root.action("tree-toggle",e.key);}}
                                }
                                if root.page!=1: CheckBox {text:"";accessible-label:"隐藏 " + e.label;checked:e.hidden;enabled:root.ready && root.resource-enabled && !e.locked;toggled=>{root.action(self.checked ? "hide-row-on" : "hide-row-off",e.key);}}
                                Rectangle {horizontal-stretch:1;min-width:0px;background:transparent;
                                    VerticalLayout {spacing:4px;Text {text:e.label;overflow:elide;color:ActorsTheme.text;min-width:0px;vertical-alignment:center;} if root.page==1: Note {text:e.detail;overflow:elide;}}
                                    TouchArea {clicked=>{root.action(root.page==1?"actor":"resource",e.key);}}
                                }
                            }
                        }
                    }
                } }
                Panel {width:root.width < 1100px ? 250px : 320px;clip:true;ScrollView {content-width:Math.max(1px,self.visible-width - 16px);horizontal-scrollbar-policy:always-off;VerticalLayout {padding:14px;spacing:10px;alignment:start;
                    Caption {text:root.selection==""?"选择一个项目":root.selection;overflow:elide;}
                    if root.detail!="": Note {text:root.detail;}
                    if root.page==1: VerticalLayout {spacing:7px;
                        if root.in-live: Button {text:root.live-pause-owned ? "恢复 Live 播放" : root.live-paused ? "Live 已暂停" : "暂停人物动作（暂停 Live）";enabled:root.live-pause-available && (!root.live-paused || root.live-pause-owned);clicked=>{root.action("live-pause","");}}
                        if root.in-live: Note {text:root.live-pause-status == "" ? "暂停会同时暂停 Live 音乐；恢复后从暂停位置继续。" : root.live-pause-status;}
                        Caption {text:"世界坐标（米）";}
                        Note {text:"按住数字向左或向右拖动，每像素移动 0.01 米。";}
                        DragNumber {axis:"X";value:root.x-value;enabled:root.ready&&root.pose-supported;dragged(axis, delta)=>{root.drag-position(axis,delta);}}
                        DragNumber {axis:"Y";value:root.y-value;enabled:root.ready&&root.pose-supported;dragged(axis, delta)=>{root.drag-position(axis,delta);}}
                        DragNumber {axis:"Z";value:root.z-value;enabled:root.ready&&root.pose-supported;dragged(axis, delta)=>{root.drag-position(axis,delta);}}
                        Button {text:"重置位置";enabled:root.ready&&root.pose-supported;clicked=>{root.action("reset","");}}
                        Caption {text:"朝向（−180° 至 +180°）";}
                        Note {text:"按住数字左右拖动，每像素旋转 0.1°。";}
                        DragNumber {axis:"朝向";value:root.yaw-value+"°";enabled:root.ready&&root.pose-supported;dragged(axis, delta)=>{root.drag-position(axis,delta);}}
                        Button {text:"重置朝向";enabled:root.ready&&root.pose-supported;clicked=>{root.action("reset-yaw","");}}
                        Button {text:"人物模型显隐";enabled:root.can-hide;clicked=>{root.action("hide-actor","");}}
                        Button {text:"移除新增人物";enabled:root.ready&&root.actor-owned;clicked=>{root.action("remove","");}}
                    }
                    if root.page!=1: VerticalLayout {spacing:8px;
                        Text {text:"我的备注";color:ActorsTheme.text;}
                        LineEdit {min-width:0px;text <=> root.resource-note;enabled:root.note-writable;placeholder-text:"为此节点填写备注";}
                        if root.selection!="" && !root.note-writable: Note {text:"当前节点路径无法唯一识别，暂不能保存备注。";}
                        Button {text:"保存备注";enabled:root.note-writable;clicked=>{root.action("save-note","");}}
                    }
                    if root.page==2: VerticalLayout {spacing:8px;
                        Caption {text:"纯色背景";}
                        Rectangle {height:60px;background:root.color-preview;border-radius:5px;}
                        Button {text:"选择背景颜色…";clicked=>{root.action("choose-color","");}}
                        Note {text:"选中颜色将显示在上方，应用后生效。";}
                        Button {text:"启用纯色背景";enabled:root.ready&&root.resource-enabled;clicked=>{root.action("color","");}}
                        Button {text:"恢复背景";enabled:root.ready&&root.resource-enabled;clicked=>{root.action("color-off","");}}
                    }
                } } }
            }
            if root.page==5: Panel {VerticalLayout {padding:18px;spacing:12px;alignment:start;
                Caption {text:"舞台导入";}
                Note {text:"摄影背景舞台将在下次背景载入时应用。";}
                ThemeComboBox {height:40px;min-width:0px;model:["游戏原生摄影场景","通用 Live 舞台（实验）","初星舞台（实验）","中庭舞台（日中）"];current-index <=> root.stage-index;}
                Button {text:"应用到下次背景载入";enabled:root.ready;clicked=>{root.action("stage","");}}
            } }
            Rectangle {height:root.notice==""?0px:68px;background:ActorsTheme.soft;border-radius:6px;ScrollView {VerticalLayout {padding:10px;Note {text:root.notice;color:ActorsTheme.muted;}}} }
          }
        }
    }
    if root.library-mode: VerticalLayout { x:0px; y:0px; width:root.width; height:root.height; spacing:0px;
        Rectangle {height:68px;background:ActorsTheme.header;
          HorizontalLayout {x:16px;y:20px;width:parent.width - 32px;height:36px;spacing:12px;
            Text {text:"GakuActors";font-size:18px;font-weight:600;color:ActorsTheme.text;vertical-alignment:center;}
            Button {text:"游戏管理";clicked=>{root.library-mode=false;}}
            Button {text:"资源库";primary:true;clicked=>{root.library-show-tasks=false;}}
            Note {text:root.library-status;vertical-alignment:center;horizontal-stretch:1;horizontal-alignment:right;}
          }
        }
        HorizontalLayout {spacing:0px;
          Rectangle {width:root.width < 1100px ? 132px : 168px;background:ActorsTheme.aside;
            VerticalLayout {padding:12px;spacing:8px;
              Note {text:"资源分类";height:26px;}
              for label[index] in ["全部资源", "模型部件", "图片与贴图", "原生动作", "舞台与场景", "音频", "其他资源"]: NavButton {text:label;selected:!root.library-show-tasks && root.library-category==index;clicked=>{root.library-show-tasks=false;root.library-category=index;root.library-action("category", "");}}
              Rectangle {vertical-stretch:1;background:transparent;}
              NavButton {text:"任务中心";selected:root.library-show-tasks;clicked=>{root.library-show-tasks=true;root.library-action("jobs", "");}}
              Note {text:"清单在启动时自动检查";}
            }
          }
          VerticalLayout {padding:16px;spacing:10px;
            HorizontalLayout {height:40px;vertical-stretch:0;spacing:10px;
              Caption {text:root.library-show-tasks ? "任务中心" : "资源库";horizontal-stretch:1;}
              Note {text:root.library-manifest;vertical-alignment:center;}
              if !root.library-show-tasks: Button {text:"导入清单…";clicked=>{root.library-action("import", "");}}
              Button {text:"刷新";clicked=>{root.library-action("refresh", "");}}
            }
            if !root.library-show-tasks: HorizontalLayout {height:36px;vertical-stretch:0;spacing:8px;
              LineEdit {horizontal-stretch:1;placeholder-text:"搜索文件名或标签，例如 花海咲季 发型、Live 舞台";text <=> root.library-query;accepted=>{root.library-action("search", "");}}
              Button {text:"搜索";clicked=>{root.library-action("search", "");}}
            }
            if !root.library-show-tasks: HorizontalLayout {spacing:12px;vertical-stretch:1;
              Panel {horizontal-stretch:3;VerticalLayout {padding:12px;spacing:8px;
                HorizontalLayout {height:26px;vertical-stretch:0;Note {text:"资源名称与内容标签";horizontal-stretch:1;}}
                library-list := ListView {horizontal-scrollbar-policy:always-off;
                  for e in root.library-resources: Rectangle {width:Math.max(1px,library-list.visible-width - 12px);height:72px;background:e.selected ? ActorsTheme.selection : ActorsTheme.panel;border-color:ActorsTheme.line;border-width:1px;border-radius:6px;
                    VerticalLayout {padding:7px;spacing:2px;
                      Text {text:(e.selected ? "☑  " : "☐  ")+e.label;color:ActorsTheme.text;overflow:elide;font-size:13px;}
                      Text {text:e.tags;color:ActorsTheme.text;font-size:12px;overflow:elide;}
                      Text {text:e.detail;color:ActorsTheme.muted;font-size:11px;overflow:elide;}
                    }
                    TouchArea {clicked=>{root.library-action("select", e.key);}}
                  }
                }
                HorizontalLayout {height:34px;vertical-stretch:0;Button {text:"上一页";clicked=>{root.library-action("prev", "");}} Note {text:root.library-page;horizontal-stretch:1;horizontal-alignment:center;vertical-alignment:center;} Button {text:"下一页";clicked=>{root.library-action("next", "");}}}
              }}
              Panel {width:root.width < 1100px ? 235px : 280px;ScrollView {horizontal-scrollbar-policy:always-off;VerticalLayout {padding:14px;spacing:9px;alignment:start;
                Caption {text:"下载与导出";}
                HorizontalLayout {height:30px;vertical-stretch:0;Note {text:root.library-selection;horizontal-stretch:1;vertical-alignment:center;} Button {text:"清空";clicked=>{root.library-action("clear", "");}}}
                selected-list := ListView {min-height:45px;preferred-height:100px;max-height:120px;vertical-stretch:1;horizontal-scrollbar-policy:always-off;
                  for e in root.library-selected: Rectangle {width:Math.max(1px,selected-list.visible-width - 12px);height:28px;background:transparent;
                    Text {x:4px;width:parent.width - 8px;height:parent.height;text:e.label;overflow:elide;color:ActorsTheme.text;font-size:12px;vertical-alignment:center;}
                  }
                }
                Text {text:"导出方式";color:ActorsTheme.text;font-size:13px;}
                ThemeComboBox {min-width:0px;model:["自动导出", "仅下载并解码", "模型 → FBX", "贴图 → PNG", "动作 → .anim"];current-index <=> root.library-format;}
                Note {text:"下载上限 256 MiB · Unity 6000.0.77f1";}
                Rectangle {height:1px;background:ActorsTheme.line;}
                Note {text:root.library-plan;}
                Button {text:"检查依赖";enabled:root.library-can-plan;clicked=>{root.library-action("plan", "");}}
                Button {text:"开始处理";primary:true;enabled:root.library-can-submit;clicked=>{root.library-action("submit", "");}}
                Note {text:"下载和导出无需运行游戏。";}
              }}}
            }
            Panel {height:root.library-show-tasks ? 1px : 84px;vertical-stretch:0;visible:!root.library-show-tasks;VerticalLayout {padding:10px;spacing:4px;
              HorizontalLayout {height:28px;vertical-stretch:0;Caption {text:"当前任务";horizontal-stretch:1;} Button {text:"查看全部任务";clicked=>{root.library-show-tasks=true;root.library-action("jobs", "");}}}
              Note {text:root.library-job-summary;}
            }}
            if root.library-show-tasks: Panel {vertical-stretch:1;VerticalLayout {padding:12px;spacing:8px;
              HorizontalLayout {height:30px;vertical-stretch:0;Caption {text:"任务与导出";horizontal-stretch:1;} Button {text:"刷新任务";clicked=>{root.library-action("jobs", "");}}}
              HorizontalLayout {spacing:10px;
                job-list := ListView {horizontal-stretch:2;horizontal-scrollbar-policy:always-off;
                  for e in root.library-jobs: Rectangle {width:Math.max(1px,job-list.visible-width - 12px);height:55px;background:e.selected ? ActorsTheme.selection : ActorsTheme.panel;border-width:1px;border-color:ActorsTheme.line;border-radius:6px;
                    VerticalLayout {padding:7px;Text {text:e.label;color:ActorsTheme.text;font-size:13px;overflow:elide;} Text {text:e.detail;color:ActorsTheme.muted;font-size:11px;overflow:elide;}}
                    TouchArea {clicked=>{root.library-action("job", e.key);}}
                  }
                }
                VerticalLayout {horizontal-stretch:1;spacing:6px;Note {text:root.library-job-detail;vertical-stretch:1;} HorizontalLayout {spacing:5px;Button {text:"取消";enabled:root.library-can-cancel;clicked=>{root.library-action("cancel", "");}} Button {text:"重试";enabled:root.library-can-retry;clicked=>{root.library-action("retry", "");}} Button {text:"打开文件夹";enabled:root.library-can-open;clicked=>{root.library-action("open", "");}}}}
              }
            }}
          }
        }
    }
    if !root.library-mode && root.zoom-open: Rectangle {background:#17253dcc;TouchArea {} VerticalLayout {padding:36px;spacing:12px;HorizontalLayout {Caption {text:root.zoom-title;color:white;} Button {text:"关闭预览";clicked=>{root.zoom-open=false;}}} Image {source:root.zoom-image;image-fit:contain;} } }
    if !root.library-mode && root.confirmation!="": Rectangle {background:#17253d88;TouchArea {} Rectangle {width:460px;height:220px;x:(parent.width - self.width)/2;y:(parent.height - self.height)/2;background:white;border-radius:12px;VerticalLayout {padding:24px;spacing:18px;Caption {text:"确认操作";} Text {text:root.confirmation;wrap:word-wrap;color:ActorsTheme.text;} HorizontalLayout {Button {text:"取消";clicked=>{root.action("cancel","");}} Button {text:"确认";clicked=>{root.action("confirm","");}}}} } }
}
}
