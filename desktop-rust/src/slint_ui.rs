#![deny(deprecated)]
slint::slint! {
#[edition = "1.19"]
import { LineEdit, ComboBox, CheckBox, ListView, ScrollView, VerticalBox, HorizontalBox, SpinBox, Slider, Palette } from "std-widgets.slint";
export struct Entry { key: string, label: string, detail: string, selected: bool, depth: int, expandable: bool, expanded: bool, body: image, head: image, body-ready: bool, head-ready: bool, body-count: int, head-count: int, face-count: int, hair-count: int }
export global ActorsTheme {
    out property <color> accent:#3977c9;
    out property <color> text:#263449;
    out property <color> muted:#748196;
    out property <color> line:#dfe7f1;
    out property <color> paper:#fbfdff;
    out property <color> panel:#ffffff;
    out property <color> header:#edf4fd;
    out property <color> aside:#f6f9fe;
    out property <color> soft:#edf5ff;
    out property <color> selection:#dfecff;
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
component Panel inherits Rectangle { background: ActorsTheme.panel; border-radius: 10px; border-width: 1px; border-color: ActorsTheme.line; }
component Caption inherits Text { color: ActorsTheme.text; font-size: 17px; font-weight: 600; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component Note inherits Text { color: ActorsTheme.muted; font-size: 13px; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component NavButton inherits Rectangle {
    in property <string> text; in property <bool> selected; callback clicked();
    height:38px; border-radius:6px; background: selected ? ActorsTheme.selection : touch.has-hover ? ActorsTheme.header : transparent;
    accessible-role:button; accessible-label:root.text;
    Text {x:10px;width:parent.width - 20px;height:parent.height;text:root.text;color:root.selected ? ActorsTheme.accent : ActorsTheme.text;vertical-alignment:center;overflow:elide;}
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
    callback action(string, string); callback changed(string);
    VerticalLayout { x:0px; y:0px; width:root.width; height:root.height; spacing:0px;
        Rectangle { height:68px; background:ActorsTheme.header;
          HorizontalLayout {x:16px;y:20px;width:parent.width - 32px;height:36px;spacing:12px;
            Text {text:"GakuActors";font-size:18px;font-weight:600;color:ActorsTheme.text;vertical-alignment:center;}
            Note {text:"场景资源管理";vertical-alignment:center;horizontal-stretch:1;}
                Rectangle { width: 160px; height: 36px; background: ActorsTheme.soft; border-radius: 8px;
                    HorizontalLayout { padding: 8px; spacing: 7px;
                        Rectangle { width: 12px; background: transparent; Rectangle { width: 10px; height: 10px; x: 1px; y: (parent.height - self.height) / 2; border-radius: 5px; background: root.connection-state == 2 ? #18a45b : root.connection-state == 1 ? #e1a427 : #d64a4a; } }
                        Text { text: root.connection-label; color: ActorsTheme.text; font-size: 12px; vertical-alignment: center; overflow: elide; }
                    }
                }
                Button {width:96px;height:36px; text: "手动连接"; clicked => {root.action("reconnect", "");} }
                if root.catalog-failed: Button { text: "重试目录"; enabled: root.can-scan-catalog; clicked => {root.action("scan", "");} }
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
              if root.page == 0: Button {text:root.settings ? "收起资源设置" : "资源设置";clicked=>{root.settings = !root.settings;}}
            }
            Note { text: root.status; }
            if root.page == 0: HorizontalLayout { spacing: 14px;
                Panel { width: root.width < 1100px ? 202px : 228px; VerticalLayout { padding: 12px; spacing: 10px;
                    if root.settings: VerticalLayout { spacing: 6px;
                        Button { height: 34px; text: "读取人物目录"; enabled: root.can-scan-catalog; clicked => {root.action("scan", "");} }
                        Button { height: 34px; text: root.creation ? "停用人物加载" : "启用人物加载"; enabled: root.ready; clicked => {root.action("creation", "");} }
                        Note { text: "连接后自动读取，也可手动更新。"; }
                    }
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
                        if root.references.length>0: ComboBox {height:34px;vertical-stretch:0;min-width:0px;model:root.references;current-index <=> root.reference-index;selected=>{root.action("reference","");}}
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
                        Caption {text:"身体";} ComboBox {min-width:0px;model:root.raw-bodies;current-index <=> root.body-index;selected=>{root.changed("raw-selection");}}
                        Caption {text:"脸部";} ComboBox {min-width:0px;model:root.raw-faces;current-index <=> root.face-index;selected=>{root.changed("raw-selection");}}
                        Caption {text:"头发";} ComboBox {min-width:0px;model:root.raw-hairs;current-index <=> root.hair-index;selected=>{root.changed("raw-selection");}}
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
                    if root.page!=1: HorizontalLayout {spacing:8px;
                        Button {text:root.resource-pending?"等待 Motion 交接":root.motion-present?(root.resource-enabled?"交还 Motion 资源控制":"启用资源控制（释放 Motion 接管）"):(root.resource-enabled?"已自动接管（无需交接）":"等待进入场景");enabled:root.admitted&&root.motion-present&&!root.resource-pending;clicked=>{root.action("resources","");}}
                    }
                    LineEdit {text <=> root.resource-search;placeholder-text:"按名称或备注筛选";edited=>{root.changed("resource-search");}}
                    ListView {
                        for e in (root.page==1?root.actors:root.resources): Rectangle {height:68px;background:e.selected?ActorsTheme.selection:white;clip:true;
                            HorizontalLayout {padding:6px;spacing:5px;
                                Rectangle {width:Math.min(e.depth, 8)*14px;background:transparent;}
                                if e.expandable: Button {width:30px;text:e.expanded?"▾":"▸";clicked=>{root.action("tree-toggle",e.key);}}
                                Rectangle {horizontal-stretch:1;min-width:0px;background:transparent;
                                    VerticalLayout {spacing:4px;Text {text:e.label;overflow:elide;color:ActorsTheme.text;min-width:0px;} Note {text:e.detail;overflow:elide;}}
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
                        Note {text:"选择滑块后可用方向键以 0.01 米微调。";}
                        Text {text:"X："+root.x-value;color:ActorsTheme.text;}
                        Slider {minimum:-256;maximum:256;step:0.01;value <=> root.x-pos;enabled:root.ready&&root.pose-supported;changed=>{root.action("slider-move","x");}}
                        Text {text:"Y："+root.y-value;color:ActorsTheme.text;}
                        Slider {minimum:-256;maximum:256;step:0.01;value <=> root.y-pos;enabled:root.ready&&root.pose-supported;changed=>{root.action("slider-move","y");}}
                        Text {text:"Z："+root.z-value;color:ActorsTheme.text;}
                        Slider {minimum:-256;maximum:256;step:0.01;value <=> root.z-pos;enabled:root.ready&&root.pose-supported;changed=>{root.action("slider-move","z");}}
                        Button {text:"重置位置";enabled:root.ready&&root.pose-supported;clicked=>{root.action("reset","");}}
                        Text {text:"朝向："+root.yaw-value+"°";color:ActorsTheme.text;}
                        Slider {minimum:0;maximum:360;step:0.1;value <=> root.yaw-pos;enabled:root.ready&&root.pose-supported;changed=>{root.action("slider-yaw","");}released=>{root.action("slider-yaw-end","");}}
                        Button {text:"重置朝向";enabled:root.ready&&root.pose-supported;clicked=>{root.action("reset-yaw","");}}
                        Button {text:"人物模型显隐";enabled:root.can-hide;clicked=>{root.action("hide-actor","");}}
                        Button {text:"移除新增人物";enabled:root.ready&&root.actor-owned;clicked=>{root.action("remove","");}}
                    }
                    if root.page!=1: VerticalLayout {spacing:8px;
                        Button {text:"切换显隐";enabled:root.can-hide;clicked=>{root.action("hide","");}}
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
                ComboBox {min-width:0px;model:["游戏原生摄影场景","通用 Live 舞台（实验）","初星舞台（实验）","中庭舞台（日中）"];current-index <=> root.stage-index;}
                Button {text:"应用到下次背景载入";enabled:root.ready;clicked=>{root.action("stage","");}}
            } }
            Rectangle {height:root.notice==""?0px:68px;background:ActorsTheme.soft;border-radius:6px;ScrollView {VerticalLayout {padding:10px;Note {text:root.notice;color:ActorsTheme.muted;}}} }
          }
        }
    }
    if root.zoom-open: Rectangle {background:#17253dcc;TouchArea {} VerticalLayout {padding:36px;spacing:12px;HorizontalLayout {Caption {text:root.zoom-title;color:white;} Button {text:"关闭预览";clicked=>{root.zoom-open=false;}}} Image {source:root.zoom-image;image-fit:contain;} } }
    if root.confirmation!="": Rectangle {background:#17253d88;TouchArea {} Rectangle {width:460px;height:220px;x:(parent.width - self.width)/2;y:(parent.height - self.height)/2;background:white;border-radius:12px;VerticalLayout {padding:24px;spacing:18px;Caption {text:"确认操作";} Text {text:root.confirmation;wrap:word-wrap;color:ActorsTheme.text;} HorizontalLayout {Button {text:"取消";clicked=>{root.action("cancel","");}} Button {text:"确认";clicked=>{root.action("confirm","");}}}} } }
}
}
