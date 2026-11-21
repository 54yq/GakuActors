#![deny(deprecated)]
slint::slint! {
#[edition = "1.19"]
import { LineEdit, ComboBox, ListView, ScrollView, Slider, TextEdit, Palette } from "std-widgets.slint";
export struct Entry { key: string, label: string, detail: string, tags: string, group-header: bool, selected: bool, avatar: string, hidden-known: bool, depth: int, expandable: bool, expanded: bool, hidden: bool, locked: bool, body: image, head: image, body-ready: bool, head-ready: bool, body-count: int, head-count: int, face-count: int, hair-count: int }
export struct StageMarker { key:string, name:string, label:string, x:float, z:float, selected:bool }
export struct SceneAsset { key:string, label:string, tags:string, group-header:bool, selected:bool, depth:int, expandable:bool, expanded:bool, hidden:bool, locked:bool, prop-group:bool }
import { ActorsTheme, Panel, Divider, PageTitle, SelectionMark, NightCheck } from "night_theme.slint";
component Button inherits Rectangle {
    in property <string> text;in property <bool> enabled:true;in property <bool> primary:false;
    callback clicked();height:34px;vertical-stretch:0;horizontal-stretch:0;min-width:0px;preferred-width:Math.max(64px,label.preferred-width + 24px);border-radius:7px;border-width:1px;
    background:!root.enabled ? ActorsTheme.aside : root.primary ? ActorsTheme.accent : touch.has-hover ? ActorsTheme.soft : ActorsTheme.control;
    border-color:root.primary && root.enabled ? ActorsTheme.accent : ActorsTheme.control-line;
    accessible-role:button;accessible-label:root.text;accessible-enabled:root.enabled;
    label := Text {x:8px;width:Math.max(0px,parent.width - 16px);height:parent.height;text:root.text;font-size:13px;font-weight:root.primary ? 650 : 400;horizontal-alignment:center;vertical-alignment:center;overflow:elide;
        color:!root.enabled ? ActorsTheme.muted.with-alpha(0.6) : root.primary ? ActorsTheme.on-accent : ActorsTheme.text;}
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
        background:ActorsTheme.selection; border-radius:5px;
        Text { width:parent.width; height:parent.height; text:"▼"; font-size:14px; font-weight:700;
            color:ActorsTheme.text; horizontal-alignment:center; vertical-alignment:center; }
    }
}
component DragNumber inherits Rectangle {
    in property <string> axis; in property <string> value; in property <bool> enabled; callback dragged(string, float);
    height: 34px; border-radius: 6px; border-width: 1px; border-color: ActorsTheme.line; background: touch.has-hover && root.enabled ? ActorsTheme.soft : ActorsTheme.aside;
    accessible-role:slider; accessible-label:root.axis+" "+root.value;
    HorizontalLayout { padding-left:8px; padding-right:6px; spacing:5px;
        Text { text:root.axis; color:ActorsTheme.accent; font-size:13px; vertical-alignment:center; }
        Text { text:root.value; color:root.enabled ? ActorsTheme.text : ActorsTheme.muted; overflow:elide; min-width:0px; font-size:13px; vertical-alignment:center; }
    }
    touch := TouchArea { enabled: root.enabled; mouse-cursor: ew-resize; property <length> last-x;
        pointer-event(event) => { if event.button == PointerEventButton.left && event.kind == PointerEventKind.down { self.last-x = self.mouse-x; } }
        moved => { if self.pressed { root.dragged(root.axis, (self.mouse-x - self.last-x) / 1px); self.last-x = self.mouse-x; } }
    }
}
component Caption inherits Text { color: ActorsTheme.text; font-size: 15px; font-weight: 650; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component Note inherits Text { color: ActorsTheme.muted; font-size: 13px; wrap: word-wrap; min-width: 0px; vertical-stretch:0; }
component NavButton inherits Rectangle {
    in property <string> text;
    in property <string> icon;
    in property <bool> selected;
    in property <length> row-height:40px;
    callback clicked();
    height:root.row-height;
    border-radius:7px;
    clip:true;
    background:root.selected ? ActorsTheme.selection : touch.has-hover ? ActorsTheme.soft : transparent;
    accessible-role:button; accessible-label:root.text;
    if root.selected: SelectionMark { height:parent.height; }
    HorizontalLayout { padding-left:10px; padding-right:8px; spacing:9px;
        if root.icon!="": Rectangle { width:18px;
            Path { x:0px; y:(parent.height - 18px)/2; width:18px; height:18px; viewbox-width:24; viewbox-height:24;
                fill:transparent; stroke:ActorsTheme.accent; stroke-width:1.45px;
                commands:root.icon=="catalog" ? "M4 4 H20 V20 H4 Z M8 8 H16 M8 12 H16 M8 16 H13" :
                    root.icon=="people" ? "M16 7 A4 4 0 1 1 8 7 A4 4 0 1 1 16 7 M4 21 V19 A6 6 0 0 1 10 13 H14 A6 6 0 0 1 20 19 V21 Z" :
                    root.icon=="scene" ? "M7 5 H22 L17 19 H2 Z M7 15 L11 11 L14 15 L17 12" :
                    root.icon=="stage" ? "M3 12 H16 M11 7 L16 12 L11 17 M17 4 H21 V20 H17" :
                    root.icon=="library" ? "M12 3 L21 12 L12 21 L3 12 Z M8 12 H16" :
                    root.icon=="music" ? "M9 18 V5 L20 3 V16 M9 18 A3 2 0 1 1 3 18 A3 2 0 1 1 9 18 M20 16 A3 2 0 1 1 14 16 A3 2 0 1 1 20 16" :
                    root.icon=="image" ? "M4 4 H20 V20 H4 Z M6 16 L11 11 L15 14 L18 11 M9 7 H10" :
                    root.icon=="motion" ? "M4 16 L9 7 L14 14 L20 6 M17 6 H20 V9" :
                    root.icon=="tasks" ? "M8 6 H20 M8 12 H20 M8 18 H20 M4 6 H5 M4 12 H5 M4 18 H5" :
                    "M4 12 H6 M11 12 H13 M18 12 H20";
            }
        }
        Text { text:root.text; min-width:0px; overflow:elide; color:root.selected ? ActorsTheme.selected-text : ActorsTheme.nav-text; vertical-alignment:center; }
    }
    focus := FocusScope {key-pressed(event)=>{if event.text=="\n" || event.text==" " {root.clicked();return accept;}return reject;}
      touch := TouchArea {clicked=>{focus.focus();root.clicked();}}
    }
}
component PageHeading inherits HorizontalLayout {
    in property <string> title;
    in property <string> subtitle;
    spacing:10px;
    vertical-stretch:0;
    VerticalLayout { spacing:3px; horizontal-stretch:1;
        PageTitle { text:root.title; }
        if root.subtitle!="": Note { text:root.subtitle; overflow:elide; wrap:no-wrap; }
    }
    @children
}
component Brand inherits HorizontalLayout {
    in property <string> name:"GakuActors";
    in property <image> logo;
    callback clicked();
    spacing:12px;
    horizontal-stretch:0;
    Rectangle { width:28px;
        Image { x:0px; y:(parent.height - 28px)/2; source:root.logo; width:28px; height:28px; image-fit:contain; }
        TouchArea { clicked=>{root.clicked();} }
    }
    Rectangle { width:brand-name.preferred-width;
        brand-name := Text { text:root.name; color:ActorsTheme.text; font-size:19px; font-weight:650; vertical-alignment:center; }
        TouchArea { clicked=>{root.clicked();} }
    }
}
component Fact inherits HorizontalLayout {
    in property <string> label;
    in property <string> value;
    spacing:10px;
    Note { text:root.label; width:38px; wrap:no-wrap; }
    Text { text:root.value=="" ? "—" : root.value; color:ActorsTheme.text; font-size:13px; horizontal-alignment:right; overflow:elide; horizontal-stretch:1; min-width:0px; }
}
component Tag inherits Rectangle {
    in property <string> text;
    height:20px; min-width:0px; preferred-width:label.preferred-width + 12px; horizontal-stretch:0;
    background:ActorsTheme.tag; border-color:ActorsTheme.tag-line; border-width:1px; border-radius:4px;
    label := Text { x:6px; width:Math.max(0px,parent.width - 12px); height:parent.height; text:root.text; color:ActorsTheme.tag-text; font-size:11px; overflow:elide; vertical-alignment:center; }
}
component HeadingSlider inherits Rectangle {
    in-out property <float> value;
    in property <bool> enabled;
    callback changed();
    height:20px;
    property <float> portion:Math.clamp((root.value + 180) / 360,0,1);
    Rectangle { x:6px; y:8px; width:parent.width - 12px; height:4px; border-radius:2px; background:ActorsTheme.line; }
    Rectangle { x:6px; y:8px; width:(parent.width - 12px)*root.portion; height:4px; border-radius:2px; background:root.enabled ? ActorsTheme.accent : ActorsTheme.muted; }
    Rectangle { x:(parent.width - 12px)*root.portion; y:4px; width:12px; height:12px; border-radius:6px; background:root.enabled ? ActorsTheme.accent : ActorsTheme.muted; border-width:native.has-focus ? 2px : 0px; border-color:ActorsTheme.text; }
    native := Slider { x:0px; y:0px; width:parent.width; height:parent.height; opacity:0.001; minimum:-180; maximum:180; step:0.1; value<=>root.value; enabled:root.enabled; accessible-label:"人物朝向"; changed=>{root.changed();} }
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
    out property <int> columns: Math.max(1, Math.floor(self.usable-width / 110px));
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
component Disclosure inherits Rectangle {
    in property <bool> expanded;
    Path {
        x:(parent.width - 10px)/2; y:(parent.height - 10px)/2; width:10px; height:10px;
        viewbox-width:10; viewbox-height:10;
        commands:root.expanded ? "M2 4 L5 7 L8 4" : "M4 2 L7 5 L4 8";
        stroke:ActorsTheme.muted; stroke-width:1.2px; fill:transparent;
    }
}
component SceneAssetRow inherits Rectangle {
    in property <SceneAsset> item;
    in property <bool> editable;
    callback action(string,string);
    height:root.item.group-header ? 27px : root.item.prop-group ? 36px : 33px;
    border-radius:5px;
    border-width:root.item.prop-group ? 1px : 0px;
    border-color:ActorsTheme.line;
    clip:true;
    background:root.item.selected ? ActorsTheme.selection : hover.has-hover ? ActorsTheme.soft : root.item.prop-group ? ActorsTheme.panel : transparent;
    if root.item.group-header: Divider { x:0px; y:0px; width:parent.width; }
    if root.item.selected: SelectionMark { height:parent.height; }
    hover := TouchArea { clicked=>{root.action(root.item.group-header ? "tree-toggle" : "resource",root.item.key);} }
    HorizontalLayout { padding-left:8px; padding-right:8px; spacing:8px;
        if root.item.depth>0: Text { width:Math.min(6,root.item.depth)*12px; text:"└"; color:#566281; horizontal-alignment:right; vertical-alignment:center; }
        if root.item.expandable: Disclosure { width:14px; expanded:root.item.expanded;
            accessible-role:button; accessible-label:(root.item.expanded ? "收起 " : "展开 ")+root.item.label;
            TouchArea { clicked=>{root.action("tree-toggle",root.item.key);} }
        }
        Text { text:root.item.label; horizontal-stretch:1; min-width:0px; overflow:elide; vertical-alignment:center;
            font-size:root.item.group-header ? 12px : 13px; color:root.item.group-header ? ActorsTheme.muted : ActorsTheme.text; font-weight:root.item.group-header||root.item.prop-group ? 600 : 400; }
        if !root.item.group-header: HorizontalLayout { width:100px; alignment:start;
            Tag { text:root.item.tags; max-width:100px; }
        }
        if !root.item.group-header: Text { text:root.item.locked ? (root.editable ? "受保护" : "只读") : root.item.hidden ? "已隐藏" : "显示"; width:47px;
            color:root.item.hidden||root.item.locked ? ActorsTheme.muted : ActorsTheme.success; font-size:12px; vertical-alignment:center; }
        if !root.item.group-header: NightCheck { cross-axis-self-alignment:center; width:root.item.prop-group ? 86px : 72px; text:root.item.prop-group ? "隐藏整组" : "隐藏";
            accessible-label:self.text+" "+root.item.label; checked:root.item.hidden; enabled:root.editable&&!root.item.locked;
            toggled=>{root.action(self.checked ? "hide-row-on" : "hide-row-off",root.item.key);} }
    }
}
component SceneAssetSection inherits Panel {
    in property <string> title;
    in property <string> count;
    in property <string> placeholder;
    in property <[SceneAsset]> rows;
    in property <bool> editable;
    in-out property <string> search;
    in-out property <bool> open:true;
    callback selected(string,string);
    callback filtered();
    background:ActorsTheme.inset;
    border-radius:8px;
    VerticalLayout { padding:8px; spacing:5px; alignment:start;
        Rectangle { height:26px; border-radius:5px; background:section-hit.has-hover ? ActorsTheme.soft : transparent;
            HorizontalLayout { padding-left:5px; padding-right:5px; spacing:8px;
                Disclosure { width:12px; expanded:root.open; }
                Caption { text:root.title; font-size:14px; vertical-alignment:center; }
                Note { text:root.count; horizontal-stretch:1; horizontal-alignment:right; vertical-alignment:center; font-size:12px; }
            }
            accessible-role:button; accessible-label:(root.open ? "收起 " : "展开 ")+root.title;
            section-focus := FocusScope { key-pressed(event)=>{if event.text=="\n" || event.text==" " {root.open=!root.open;return accept;}return reject;}
                section-hit := TouchArea { clicked=>{section-focus.focus();root.open=!root.open;} }
            }
        }
        if root.open: LineEdit { height:32px; text<=>root.search; placeholder-text:root.placeholder; edited=>{root.filtered();} }
        if root.open && root.rows.length==0: Note { text:root.search=="" ? "暂无资产，请扫描当前场景。" : "没有匹配的资产。"; }
        if root.open && root.rows.length>0: assets := ListView {
            height:Math.min(288px, root.rows.length*36px);
            horizontal-scrollbar-policy:always-off;
            for row in root.rows: SceneAssetRow {
                width:assets.visible-width;
                item:row; editable:root.editable; action(a,b)=>{root.selected(a,b);}
            }
        }
    }
}
export component Manager inherits Window {
    init=>{Palette.color-scheme=ColorScheme.dark;}
    icon: @image-url("../../assets/scene-manager-window.png");
    in property <string> desktop-version;
    in property <string> character-name;
    in property <int> character-count;
    in property <int> actor-count;
    in property <string> actor-body;
    in property <string> actor-face;
    in property <string> actor-hair;
    in property <bool> actor-hidden;
    title: "GakuActors " + root.desktop-version + " · 场景资源管理"; preferred-width: 1320px; preferred-height: 960px; min-width: 880px; min-height: 440px;
    background: ActorsTheme.paper; default-font-family: "Microsoft YaHei UI"; default-font-size: 14px;
    in-out property <int> page: 0;
    in-out property <bool> settings: false;
    in property <bool> in-live; in property <bool> live-pause-available; in property <bool> live-paused; in property <bool> live-pause-owned; in property <string> live-pause-status;
    in property <bool> admitted; in property <bool> ready; in property <bool> writable; in property <bool> creation; in property <bool> resource-enabled; in property <bool> resource-pending; in property <bool> camera-active;
    in property <bool> can-scan-resources; in property <string> scene-scan-status;
    in property <string> scene-name; in property <string> workspace-feedback; in property <string> asset-name; in property <string> asset-tag;
    in property <string> asset-state; in property <string> asset-path;
    in property <string> fixed-count; in property <string> moving-count;
    in-out property <bool> fixed-open:true; in-out property <bool> moving-open:true;
    in property <bool> motion-present; in property <bool> can-scan-catalog; in property <bool> can-undo; in property <bool> actor-owned; in property <bool> pose-supported; in property <bool> save-ready; in property <bool> can-delete; in property <bool> can-hide;
    in property <bool> catalog-failed; in property <int> connection-state: 0; in property <string> connection-label: "正在检测游戏";
    in property <string> status: "等待游戏插件连接"; in property <string> notice; in property <string> detail; in property <string> selection;
    in-out property <string> search; in-out property <string> resource-search; in-out property <string> fixed-search; in-out property <string> moving-search; in-out property <string> preset-name;
    in-out property <float> x-pos; in-out property <float> y-pos; in-out property <float> z-pos; in-out property <float> yaw-pos;
    in property <brush> color-preview: #00ff00; in-out property <string> resource-note; in property <bool> note-writable;
    in-out property <string> x-value; in-out property <string> y-value; in-out property <string> z-value; in-out property <string> yaw-value: "0";
    in-out property <bool> cross; in-out property <bool> raw-mode;
    in property <[Entry]> characters; in property <[Entry]> presets; in property <[Entry]> bodies; in property <[Entry]> heads; in property <[Entry]> actors; in property <[StageMarker]> stage-markers; in property <[SceneAsset]> fixed-assets; in property <[SceneAsset]> moving-assets;
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
    property <int> brand-taps:0;
    out property <bool> libraries-unlocked:root.brand-taps>=10;
    out property <string> unlock-hint:root.brand-taps>=5 && root.brand-taps<10 ?
        "再点击 GakuActors " + (10-root.brand-taps) + " 次解锁资源库和乐库" : "";
    unlock-timeout := Timer { interval:6s; running:false;
        triggered=>{root.brand-taps=0; self.running=false;}
    }
    function tap-brand() {
        if root.brand-taps < 10 {
            root.brand-taps += 1;
            unlock-timeout.running=false;
            if root.brand-taps < 10 {unlock-timeout.running=true;}
        }
    }
    property <bool> short-window:root.height < 570px;
    property <length> workspace-height:Math.max(190px,root.height - (root.short-window ? 220px : root.notice!="" && root.page!=2 ? 250px : 190px));
    property <length> map-height:root.short-window
        ? Math.min(123px, Math.max(92px,root.workspace-height - 142px))
        : Math.min(253px, Math.max(132px,root.workspace-height - 172px));
    property <length> actor-list-height:Math.min(Math.max(66px,root.workspace-height - (root.stage-markers.length>0 ? root.map-height + 67px : 57px)), Math.max(64px,root.actors.length*62px+10px));
    property <length> fixed-section-height:root.fixed-open ? 94px + Math.min(288px,root.fixed-assets.length*36px) : 42px;
    property <length> moving-section-height:root.moving-open ? 94px + Math.min(288px,root.moving-assets.length*36px) : 42px;
    property <length> measured-nav-width;
    property <length> measured-home-directory-width;
    property <length> measured-header-action-y;
    property <length> measured-last-nav-y;
    out property <length> review-nav-width:root.measured-nav-width;
    out property <length> review-home-directory-width:root.page==0 && !root.library-mode ? root.measured-home-directory-width : 0px;
    out property <length> review-header-action-y:root.measured-header-action-y;
    out property <length> review-last-nav-y:root.measured-last-nav-y;
    property <bool> compact:root.width < 1050px;
    property <length> nav-width:root.compact ? 152px : ActorsTheme.sidebar-width;
    property <length> gutter:root.compact ? 16px : ActorsTheme.content-gutter;
    property <string> scene-summary:root.scene-name!="" ? (root.in-live ? "Live · " : "")+root.scene-name : root.admitted ? "当前场景" : "进入拍摄或 Live 后查看当前场景";
    if !root.library-mode: VerticalLayout { x:0px; y:0px; width:root.width; height:root.height;
        Rectangle { height:ActorsTheme.header-height; background:ActorsTheme.header;
            Divider { x:0px; y:parent.height - 1px; width:parent.width; }
            HorizontalLayout { x:22px; y:(parent.height - 34px)/2; width:parent.width - 44px; height:34px; spacing:10px;
                Brand { logo:@image-url("../../assets/scene-manager-window.png"); clicked=>{root.tap-brand();} }
                Rectangle { horizontal-stretch:1; }
                HorizontalLayout { spacing:8px; horizontal-stretch:0; alignment:center;
                    Rectangle { width:7px;
                        Rectangle { x:0px; y:(parent.height - 7px)/2; width:7px; height:7px; border-radius:4px; background:root.connection-state==2 ? ActorsTheme.success : root.connection-state==1 ? ActorsTheme.warning : #dc747e; }
                    }
                    Text { text:root.connection-label; min-width:0px; max-width:175px; overflow:elide; font-size:13px; color:root.connection-state==2 ? ActorsTheme.success : root.connection-state==1 ? ActorsTheme.warning : ActorsTheme.muted; vertical-alignment:center; }
                }
                Button {
                    text:"手动连接";
                    changed absolute-position => { root.measured-header-action-y = self.absolute-position.y; }
                    clicked=>{root.action("reconnect","");}
                }
                if root.catalog-failed && root.page!=0: Button { text:"重试目录"; enabled:root.can-scan-catalog; clicked=>{root.action("scan","");} }
                if root.page!=2 && root.admitted && root.motion-present: Button { text:root.resource-pending?"等待 Motion 交接":root.resource-enabled?"交还 Motion 资源控制":"启用资源控制"; enabled:!root.resource-pending; clicked=>{root.action("resources","");} }
                Button { text:root.camera-active ? "关闭自由视角" : "自由视角"; enabled:root.admitted; clicked=>{root.action("camera","");} }
            }
        }
        HorizontalLayout {
            Rectangle { width:root.nav-width; background:ActorsTheme.aside; changed width => { root.measured-nav-width = self.width; }
                Rectangle { x:parent.width - 1px; y:0px; width:1px; height:parent.height; background:ActorsTheme.line; }
                VerticalLayout { padding-left:12px; padding-right:12px; padding-top:24px; padding-bottom:15px; spacing:5px;
                    Note { text:"工作区"; height:24px; }
                    NavButton { text:"人物与预设"; icon:"catalog"; selected:root.page==0 || root.page==4; clicked=>{root.action("nav","0");} }
                    NavButton { text:"场景人物"; icon:"people"; selected:root.page==1; clicked=>{root.action("nav","1");} }
                    NavButton { text:"场景与道具"; icon:"scene"; selected:root.page==2; clicked=>{root.action("nav","2");} }
                    NavButton { text:"舞台导入"; icon:"stage"; selected:root.page==5; clicked=>{root.action("nav","5");} }
                    Rectangle { vertical-stretch:1; }
                    if root.libraries-unlocked: NavButton { text:root.compact ? "资源库" : "资源库与导出"; icon:"library"; clicked=>{root.library-mode=true; root.library-action("refresh","");} }
                    if root.libraries-unlocked: NavButton { text:"打开乐库"; icon:"music";
                        changed absolute-position => { root.measured-last-nav-y = self.absolute-position.y; }
                        clicked=>{root.action("music","");}
                    }
                }
            }
            VerticalLayout { padding-left:root.gutter; padding-right:root.gutter; padding-top:root.short-window ? 16px : 22px; padding-bottom:root.short-window ? 10px : 16px; spacing:root.short-window ? 10px : 16px;
                PageHeading {
                    title:root.page==4 ? "人物预设编辑" : root.page==0 ? "人物与预设" : root.page==1 ? "场景人物" : root.page==5 ? "舞台导入" : "场景与道具";
                    subtitle:root.page==1 || root.page==2 ? root.scene-summary : root.page==5 ? "设置下次载入摄影背景时使用的舞台。" : root.page==4 ? "选择部件，预览并保存造型。" :
                        root.admitted ? "选择人物与造型预设，可将造型加入当前场景。" : root.characters.length==0 ? "读取人物目录后即可浏览人物并管理造型预设。" : "可浏览本地人物目录。加入场景需进入拍摄或 Live。";
                    if root.page==0: Button { cross-axis-self-alignment:center; text:"读取人物目录"; enabled:root.can-scan-catalog; clicked=>{root.action("scan","");} }
                    if (root.page==0 || root.page==1) && root.admitted: Button { text:root.creation ? "关闭人物加载" : "启用人物加载"; enabled:root.ready; clicked=>{root.action("creation","");} }
                    if root.page==1: Button { text:"⟳  刷新人物"; primary:true; enabled:root.ready; clicked=>{root.action("scan-scene","");} }
                    if root.page==2: Button { text:"⟳  扫描当前场景"; primary:true; enabled:root.can-scan-resources; clicked=>{root.action("scan-resources","");} }
                    if root.page==4: Button { text:"返回预设列表"; clicked=>{root.action("nav","0");} }
                }
            if root.page==0: HorizontalLayout { spacing:ActorsTheme.workspace-gap; vertical-stretch:0;
                Panel {
                    width:root.compact ? 180px : 220px;
                    changed width => { root.measured-home-directory-width = self.width; }
                    height:Math.min(root.workspace-height, Math.max(158px, 106px + root.characters.length*48px));
                    VerticalLayout { padding:12px; spacing:8px;
                        HorizontalLayout { spacing:5px;
                            Caption { text:"人物目录"; horizontal-stretch:1; }
                            Note { text:root.character-count+" 位"; }
                        }
                        Divider {}
                        LineEdit { height:32px; vertical-stretch:0; placeholder-text:"搜索人物"; text<=>root.search; edited=>{root.changed("search");} }
                        if root.characters.length==0: Note { text:root.search!="" ? "没有匹配的人物。" : "目录尚未读取。"; }
                        if root.characters.length>0: character-list := ListView { min-height:0px; vertical-stretch:1; horizontal-scrollbar-policy:always-off;
                            for e in root.characters: Rectangle {
                                width:Math.max(1px, character-list.visible-width - 8px); height:48px; clip:true;
                                border-radius:6px; background:e.selected ? ActorsTheme.selection : character-hit.has-hover ? ActorsTheme.soft : transparent;
                                if e.selected: SelectionMark { height:parent.height; }
                                VerticalLayout { padding-left:10px; padding-right:6px; spacing:2px; alignment:center;
                                    Text { text:e.label; overflow:elide; color:ActorsTheme.text; font-size:14px; height:18px; vertical-stretch:0; }
                                    if e.selected && (e.body-count>0 || e.head-count>0 || e.face-count>0 || e.hair-count>0): Note {
                                        text:(e.body-count>0 ? "身体 "+e.body-count+"  " : "") + (e.head-count>0 ? "头部 "+e.head-count+"  " : "") +
                                            (e.face-count>0 ? "脸 "+e.face-count+"  " : "") + (e.hair-count>0 ? "发 "+e.hair-count : "");
                                        height:15px; wrap:no-wrap; overflow:elide; font-size:11px;
                                    }
                                }
                                accessible-role:button; accessible-label:e.label;
                                character-hit := TouchArea { clicked=>{root.action("character",e.key);} }
                            }
                        }
                    }
                }
                Panel {
                    min-width:0px; horizontal-stretch:1;
                    height:root.presets.length==0 ? 164px : Math.min(root.workspace-height, Math.max(256px, 116px + Math.ceil(root.presets.length/gallery.columns)*146px));
                    VerticalLayout { padding:12px; spacing:10px;
                        HorizontalLayout { spacing:8px; height:36px;
                            VerticalLayout { spacing:1px; horizontal-stretch:1;
                                Caption { text:root.character-name=="" ? "造型预设" : root.character-name; overflow:elide; wrap:no-wrap; }
                                if root.character-name!="": Note { text:"造型预设"; font-size:12px; }
                            }
                            if root.can-undo: Button { text:"撤销删除"; clicked=>{root.action("undo","");} }
                            Button { text:"＋ 新建预设"; primary:true; enabled:root.writable; clicked=>{root.action("new","");} }
                        }
                        Divider {}
                        if root.presets.length==0: Rectangle { height:76px; border-radius:7px; background:ActorsTheme.inset; border-width:1px; border-color:ActorsTheme.line;
                            VerticalLayout { padding-left:14px; padding-right:14px; spacing:6px; alignment:center;
                                Caption { text:root.character-name=="" ? "等待人物目录" : "这个人物还没有预设"; }
                                Note { text:root.character-name=="" ? "读取目录后，选择人物即可查看和新建造型预设。" : "新建预设后，选择身体与头部并保存。"; }
                            }
                        }
                        gallery := Gallery { entries:root.presets; pair:true; visible:root.presets.length>0; min-height:0px; vertical-stretch:1;
                            picked(key)=>{root.action("preset",key);}
                        }
                        if root.presets.length>0: HorizontalLayout { height:34px; vertical-stretch:0; spacing:8px;
                            Button { text:"上一页"; clicked=>{root.action("preset-page","-1");} }
                            Note { text:root.preset-pager; horizontal-stretch:1; horizontal-alignment:center; vertical-alignment:center; }
                            Button { text:"下一页"; clicked=>{root.action("preset-page","1");} }
                        }
                    }
                }
                if root.selection!="": Panel {
                    width:root.compact ? 225px : 290px;
                    height:Math.min(root.workspace-height, root.admitted ? 494px : 333px);
                    clip:true;
                    ScrollView { content-width:Math.max(1px,self.visible-width - 10px); horizontal-scrollbar-policy:always-off;
                        VerticalLayout { padding:13px; spacing:10px; alignment:start;
                            Caption { text:"已选造型"; }
                            if root.selected-body-ready || root.selected-head-ready: HorizontalLayout { height:120px; spacing:8px;
                                Picture { source:root.selected-body; ready:root.selected-body-ready; placeholder:root.body-placeholder; TouchArea { clicked=>{root.action("zoom","body");} } }
                                Picture { source:root.selected-head; ready:root.selected-head-ready; placeholder:root.head-placeholder; TouchArea { clicked=>{root.action("zoom","head");} } }
                            }
                            Caption { text:root.selection; }
                            if root.detail!="": Note { text:root.detail; }
                            HorizontalLayout { spacing:8px;
                                Button { text:"编辑预设"; primary:true; enabled:root.writable; clicked=>{root.action("edit","");} }
                                Button { text:"删除预设"; enabled:root.can-delete; clicked=>{root.action("delete","");} }
                            }
                            if !root.selected-body-ready || !root.selected-head-ready: Button { text:"重试当前图片"; clicked=>{root.action("retry","");} }
                            if root.admitted: VerticalLayout { spacing:8px;
                                Divider {}
                                Caption { text:"加入场景"; }
                                if root.references.length>0: ThemeComboBox { model:root.references; current-index<=>root.reference-index; selected=>{root.action("reference","");} }
                                if root.references.length==0: Note { text:"当前没有参考人物，可在场景人物页面刷新。"; }
                                Button { text:"加入人物"; primary:true; enabled:root.ready && root.creation && root.reference-index>=0; clicked=>{root.action("spawn","");} }
                            }
                        }
                    }
                }
            }
            if root.page == 4: VerticalLayout { spacing: 12px;
                Panel { background:ActorsTheme.inset; height:44px; HorizontalLayout { padding-left:12px; padding-right:12px; spacing:18px; NightCheck {cross-axis-self-alignment:center;text:"跨人物部件";checked <=> root.cross;toggled=>{root.changed("cross");}} NightCheck {cross-axis-self-alignment:center;text:"原始身体 / 脸 / 头发";checked <=> root.raw-mode;toggled=>{root.changed("raw");}} Rectangle {horizontal-stretch:1;} } }
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
                        Button {text:"保存预设";primary:true;enabled:root.save-ready;clicked=>{root.action("save","");}}
                    } } }
                }
            }
            if root.page==2: VerticalLayout { spacing:12px; vertical-stretch:0; height:Math.min(root.workspace-height + 18px, 148px + fixed-section-height + moving-section-height);
                Panel { background:ActorsTheme.inset; height:43px; HorizontalLayout { padding-left:12px; padding-right:12px; spacing:10px;
                    NightCheck { cross-axis-self-alignment:center; text:"资源控制"; checked:root.resource-enabled; enabled:root.admitted&&root.motion-present&&!root.resource-pending;
                        toggled=>{root.action("resources","");} }
                    Note { text:root.resource-pending?"等待 Motion 交接":root.resource-enabled?"Actors 已接管显隐":"只读 · 可查看和扫描"; vertical-alignment:center; }
                    Rectangle { horizontal-stretch:1; }
                    Note { text:root.scene-scan-status; vertical-alignment:center; overflow:elide; }
                } }
                HorizontalLayout { spacing:ActorsTheme.workspace-gap; vertical-stretch:1;
                    Panel { horizontal-stretch:1; min-width:0px; clip:true;
                        VerticalLayout { padding:10px; spacing:8px;
                            HorizontalLayout { padding-left:3px; padding-right:3px; height:28px;
                                Caption { text:"高级场景编辑"; horizontal-stretch:1; vertical-alignment:center; }
                                Note { text:"当前场景"; vertical-alignment:center; }
                            }
                            Divider {}
                            scene-scroll := ScrollView { horizontal-scrollbar-policy:always-off; content-width:Math.max(1px,self.visible-width - 8px);
                                VerticalLayout { spacing:8px; alignment:start;
                                    SceneAssetSection { title:"固定资产"; count:root.fixed-count; placeholder:"搜索场景节点或标签";
                                        rows:root.fixed-assets; search<=>root.fixed-search; open<=>root.fixed-open; editable:root.ready&&root.resource-enabled;
                                        selected(a,b)=>{root.action(a,b);} filtered=>{root.changed("fixed-search");} }
                                    SceneAssetSection { title:"移动资产"; count:root.moving-count; placeholder:"搜索道具、部件或所属人物";
                                        rows:root.moving-assets; search<=>root.moving-search; open<=>root.moving-open; editable:root.ready&&root.resource-enabled;
                                        selected(a,b)=>{root.action(a,b);} filtered=>{root.changed("moving-search");} }
                                }
                            }
                        }
                    }
                    ScrollView { width:root.compact ? 225px : 290px; horizontal-scrollbar-policy:always-off;
                        content-width:Math.max(1px,self.visible-width - 8px);
                        VerticalLayout { spacing:10px; alignment:start;
                            Panel { VerticalLayout { padding:13px; spacing:9px; alignment:start;
                                Caption { text:"节点详情"; font-size:15px; }
                                if root.asset-name=="": Note { text:"选择一个场景节点或道具。"; }
                                if root.asset-name!="": VerticalLayout { spacing:8px;
                                    Caption { text:root.asset-name; font-size:14px; }
                                    Fact { label:"分类"; value:root.asset-tag; }
                                    Fact { label:"显隐"; value:root.asset-state; }
                                    if root.asset-path!="": Panel { background:ActorsTheme.inset; border-width:0px; VerticalLayout { padding:8px; Note {text:root.asset-path; font-family:"Consolas"; font-size:12px;} } }
                                    Divider {}
                                    Caption { text:"我的备注"; font-size:13px; }
                                    TextEdit { height:58px; text<=>root.resource-note; enabled:root.note-writable; }
                                    HorizontalLayout { alignment:end; Button {text:"保存备注";enabled:root.note-writable;clicked=>{root.action("save-note","");}} }
                                }
                            } }
                            Panel { VerticalLayout { padding:13px; spacing:8px;
                                Caption { text:"纯色背景"; font-size:15px; }
                                HorizontalLayout { spacing:8px;
                                    Rectangle {width:25px;height:25px;background:root.color-preview;border-radius:5px;border-width:1px;border-color:ActorsTheme.line;}
                                    Button {text:"选择颜色…";clicked=>{root.action("choose-color","");}}
                                }
                                HorizontalLayout { spacing:8px;
                                    Button {text:"启用纯色";enabled:root.ready&&root.resource-enabled;clicked=>{root.action("color","");}}
                                    Button {text:"恢复背景";enabled:root.ready&&root.resource-enabled;clicked=>{root.action("color-off","");}}
                                }
                            } }
                        }
                    }
                }
            }
            if root.page==1: HorizontalLayout { spacing:ActorsTheme.workspace-gap; vertical-stretch:0;
                Panel {
                    horizontal-stretch:1; min-width:0px;
                    height:57px + actor-list-height + (root.stage-markers.length>0 ? root.map-height + 10px : 0px);
                    VerticalLayout { spacing:0px;
                        HorizontalLayout { height:53px; padding-left:13px; padding-right:13px; spacing:9px;
                            Caption { text:"场景中的人物"; horizontal-stretch:1; vertical-alignment:center; }
                            Note { text:root.actor-count+" 位"; vertical-alignment:center; }
                            Rectangle { width:root.compact ? 105px : 176px;
                                LineEdit { x:0px; y:(parent.height - 32px)/2; width:parent.width; height:32px; text<=>root.resource-search; placeholder-text:"搜索人物"; edited=>{root.changed("resource-search");} }
                            }
                        }
                        Divider {}
                        if root.actors.length==0: VerticalLayout { height:actor-list-height; padding:12px; alignment:start;
                            Note { text:root.resource-search!="" ? "没有匹配的人物。" : root.admitted ? "当前场景没有人物，可刷新列表。" : "连接游戏并进入拍摄或 Live 后查看人物。"; }
                        }
                        if root.actors.length>0: actor-list := ListView { height:actor-list-height; horizontal-scrollbar-policy:always-off;
                            for e[index] in root.actors: Rectangle { width:actor-list.visible-width; height:62px;
                                Rectangle { x:10px; y:5px; width:parent.width - 20px; height:56px; clip:true; border-radius:7px;
                                    background:e.selected ? ActorsTheme.selection : hit.has-hover ? ActorsTheme.soft : transparent;
                                    if e.selected: SelectionMark { height:parent.height; }
                                    HorizontalLayout { padding-left:13px; padding-right:12px; spacing:12px;
                                        Rectangle { width:36px;
                                    Rectangle { x:0px; y:(parent.height - 36px)/2; width:36px; height:36px; border-radius:8px;
                                            background:Math.mod(index,3)==1 ? #233c46 : Math.mod(index,3)==2 ? #473924 : #342743;
                                            Text { text:e.avatar=="" ? "♙" : e.avatar; color:Math.mod(index,3)==1 ? #a6dde0 : Math.mod(index,3)==2 ? #f5d49f : #f2b7d7; font-size:16px; horizontal-alignment:center; vertical-alignment:center; }
                                    } }
                                        VerticalLayout { min-width:0px; horizontal-stretch:1; alignment:center; spacing:3px;
                                            Text { text:e.label; overflow:elide; color:ActorsTheme.text; min-width:0px; }
                                            Note { text:e.detail; wrap:no-wrap; overflow:elide; font-size:12px; }
                                        }
                                        if e.selected: Tag { cross-axis-self-alignment:center; text:"已选中"; }
                                        if !e.selected && e.hidden-known: Note { text:e.hidden ? "已隐藏" : "显示"; vertical-alignment:center; }
                                    }
                                    accessible-role:button; accessible-label:e.label;
                                    hit := TouchArea { clicked=>{root.action("actor",e.key);} }
                                }
                            }
                        }
                        if root.stage-markers.length>0: HorizontalLayout { padding:10px; height:root.map-height + 10px;
                            stage-map := Panel { height:root.map-height; background:#151926; clip:true;
                                for line[index] in Math.ceil(stage-map.width/24px): Rectangle { x:(index + 1)*24px; y:0px; width:1px; height:parent.height; background:#222a3b; }
                                for line[index] in Math.ceil(root.map-height/24px): Rectangle { x:0px; y:(index + 1)*24px; height:1px; width:parent.width; background:#222a3b; }
                                Rectangle { x:parent.width*0.18; y:parent.height*0.12; width:parent.width*0.64; height:parent.height*0.76;
                                    border-width:1px; border-color:#454e66; border-radius:12px; background:#21263c77;
                                    Note { x:0px; y:10px; width:parent.width; text:"舞台后方"; horizontal-alignment:center; }
                                }
                                for marker in root.stage-markers: Rectangle {
                                    width:34px; height:34px;
                                    x:parent.width*0.18 + marker.x*(parent.width*0.64 - self.width);
                                    y:parent.height*0.18 + marker.z*(parent.height*0.75 - self.height);
                                    if marker.selected: Rectangle { x:-6px; y:-6px; width:46px; height:46px; border-radius:23px; background:#a59bff30; }
                                    Rectangle {
                                        x:0px; y:0px; width:34px; height:34px; border-radius:17px; border-width:1px; border-color:#918be6;
                                        background:marker.selected ? ActorsTheme.accent : #302e50;
                                        Text { text:marker.label; color:marker.selected ? ActorsTheme.on-accent : ActorsTheme.text; horizontal-alignment:center; vertical-alignment:center; }
                                    }
                                    accessible-label:marker.name; accessible-role:button;
                                    TouchArea { clicked=>{root.action("actor",marker.key);} }
                                }
                            }
                        }
                    }
                }
                if root.selection!="": ScrollView { width:root.compact ? 260px : 335px; height:Math.min(root.workspace-height, root.selection=="" ? 82px : root.in-live && root.live-pause-available ? 553px : 469px); horizontal-scrollbar-policy:always-off;
                    content-width:Math.max(1px,self.visible-width - 8px);
                    VerticalLayout { alignment:start; spacing:10px;
                        Panel { VerticalLayout { padding:14px; spacing:9px; alignment:start;
                            Caption { text:root.selection=="" ? "人物详情" : root.selection; }
                            if root.selection=="": Note { text:"从列表或站位图选择人物。"; }
                            if root.selection!="": VerticalLayout { spacing:9px;
                                Note { text:root.actor-owned ? "本插件新增" : "场景原有人物"; }
                                Fact { label:"身体"; value:root.actor-body; }
                                Fact { label:"脸部"; value:root.actor-face; }
                                Fact { label:"发型"; value:root.actor-hair; }
                                Divider {}
                                Caption { text:"位置"; font-size:13px; }
                                HorizontalLayout { spacing:6px;
                                    DragNumber { axis:"X"; value:root.x-value; enabled:root.ready && root.pose-supported; dragged(axis,delta)=>{root.drag-position(axis,delta);} }
                                    DragNumber { axis:"Y"; value:root.y-value; enabled:root.ready && root.pose-supported; dragged(axis,delta)=>{root.drag-position(axis,delta);} }
                                    DragNumber { axis:"Z"; value:root.z-value; enabled:root.ready && root.pose-supported; dragged(axis,delta)=>{root.drag-position(axis,delta);} }
                                }
                                HorizontalLayout { alignment:start; Button { text:"重置位置"; enabled:root.ready&&root.pose-supported; clicked=>{root.action("reset","");} } }
                                HorizontalLayout { Caption { text:"朝向"; font-size:13px; horizontal-stretch:1; } Note { text:root.yaw-value+"°"; } }
                                HeadingSlider { value<=>root.yaw-pos; enabled:root.ready&&root.pose-supported; changed=>{root.action("slider-yaw","");} }
                                HorizontalLayout { alignment:start; Button { text:"重置朝向"; enabled:root.ready&&root.pose-supported; clicked=>{root.action("reset-yaw","");} } }
                                Divider {}
                                HorizontalLayout { spacing:6px;
                                    NightCheck { cross-axis-self-alignment:center; text:"隐藏人物"; checked:root.actor-hidden; enabled:root.can-hide; toggled=>{root.action("hide-actor","");} }
                                    Rectangle { horizontal-stretch:1; }
                                    Button { text:"移除人物"; enabled:root.ready&&root.actor-owned; clicked=>{root.action("remove","");} }
                                }
                                if root.in-live && (root.live-pause-available || root.live-paused || root.live-pause-status!=""): VerticalLayout { spacing:6px;
                                    Divider {}
                                    HorizontalLayout { alignment:start; Button { text:root.live-pause-owned ? "恢复 Live 播放" : root.live-paused ? "Live 已暂停" : "暂停 Live 播放"; enabled:root.live-pause-available && (!root.live-paused || root.live-pause-owned); clicked=>{root.action("live-pause","");} } }
                                    Note { text:root.live-pause-status=="" ? "暂停会同时暂停 Live 音乐。" : root.live-pause-status; }
                                }
                            }
                        } }
                        if root.selection!="": Rectangle { background:ActorsTheme.hint; border-radius:6px; VerticalLayout { padding:9px;
                            Note { text:"可拖动坐标数值。移除仅适用于 Actors 新增的人物。"; color:ActorsTheme.selected-text; font-size:12px; }
                        } }
                    }
                }
            }
            if root.page==5: HorizontalLayout { vertical-stretch:0; height:166px; spacing:ActorsTheme.workspace-gap; Panel { width:Math.min(620px, root.width - root.nav-width - 2*root.gutter); VerticalLayout {padding:16px;spacing:10px;alignment:start;
                Caption {text:"摄影背景";}
                Note {text:"摄影背景舞台将在下次背景载入时应用。";}
                ThemeComboBox {height:40px;min-width:0px;model:["游戏原生摄影场景","通用 Live 舞台（实验）","初星舞台（实验）","中庭舞台（日中）"];current-index <=> root.stage-index;}
                HorizontalLayout { alignment:start; Button {text:"应用到下次背景载入";primary:true;enabled:root.ready;clicked=>{root.action("stage","");}} }
            } } Rectangle { horizontal-stretch:1; } }
            if root.page==2: Note {text:root.workspace-feedback!="" ? root.workspace-feedback : "扫描范围为当前场景。显隐控制需场景授权。";}
            if root.page!=2 && root.notice!="": Rectangle { height:root.short-window ? 28px : 40px; vertical-stretch:0; background:ActorsTheme.inset; border-radius:6px;
                ScrollView { horizontal-scrollbar-policy:always-off; content-width:Math.max(1px,self.visible-width - 8px); VerticalLayout { padding:8px; Note { text:root.notice; } } }
            }
            if root.page==0 || root.page==1 || root.page==2 || root.page==5: Rectangle { vertical-stretch:1; }
          }
        }
    }
    if root.library-mode: VerticalLayout { x:0px; y:0px; width:root.width; height:root.height; spacing:0px;
        Rectangle { height:ActorsTheme.header-height; background:ActorsTheme.header;
            Divider { x:0px; y:parent.height - 1px; width:parent.width; }
            HorizontalLayout { x:22px; y:(parent.height - 34px)/2; width:parent.width - 44px; height:34px; spacing:10px;
                Brand { logo:@image-url("../../assets/scene-manager-window.png"); }
                Rectangle { horizontal-stretch:1; }
                Note { text:root.library-status; max-width:360px; wrap:no-wrap; overflow:elide; vertical-alignment:center; }
                Button { text:"返回场景管理"; clicked=>{root.library-mode=false;} }
            }
        }
        HorizontalLayout {
            Rectangle { width:root.nav-width; background:ActorsTheme.aside; changed width => { root.measured-nav-width = self.width; }
                Rectangle { x:parent.width - 1px; y:0px; width:1px; height:parent.height; background:ActorsTheme.line; }
                VerticalLayout { padding-left:12px; padding-right:12px; padding-top:24px; padding-bottom:15px; spacing:4px;
                    Note { text:"资源分类"; height:22px; }
                    for label[index] in ["全部资源", "模型部件", "图片与贴图", "原生动作", "舞台与场景", "音频", "其他资源"]: NavButton {
                        text:label; icon:index==0 ? "library" : index==1 ? "people" : index==2 ? "image" : index==3 ? "motion" : index==4 ? "scene" : index==5 ? "music" : "more";
                        row-height:root.height<530px ? 30px : 38px;
                        selected:!root.library-show-tasks && root.library-category==index;
                        clicked=>{root.library-show-tasks=false;root.library-category=index;root.library-action("category","");}
                    }
                    Rectangle { vertical-stretch:1; }
                    NavButton { text:"任务中心"; icon:"tasks"; row-height:root.height<530px ? 30px : 38px; selected:root.library-show-tasks; clicked=>{root.library-show-tasks=true;root.library-action("jobs","");} }
                }
            }
            VerticalLayout { padding-left:root.gutter; padding-right:root.gutter; padding-top:22px; padding-bottom:16px; spacing:12px;
                PageHeading { title:root.library-show-tasks ? "任务中心" : "资源库与导出"; subtitle:root.library-show-tasks ? "查看下载和导出进度。" : root.library-manifest;
                    if !root.library-show-tasks: Button { text:"导入清单…"; clicked=>{root.library-action("import","");} }
                    Button { text:"刷新"; clicked=>{root.library-action(root.library-show-tasks ? "jobs" : "refresh","");} }
                }
            if !root.library-show-tasks: HorizontalLayout {height:36px;vertical-stretch:0;spacing:8px;
              LineEdit {horizontal-stretch:1;placeholder-text:"搜索文件名或标签，例如 花海咲季 发型、Live 舞台";text <=> root.library-query;accepted=>{root.library-action("search", "");}}
              Button {text:"搜索";primary:true;clicked=>{root.library-action("search", "");}}
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
            if !root.library-show-tasks: Panel { height:root.height<500px ? 56px : 75px; vertical-stretch:0;VerticalLayout {padding:10px;spacing:4px;
              HorizontalLayout {height:34px;vertical-stretch:0;Caption {text:"当前任务";horizontal-stretch:1;vertical-alignment:center;} Button {text:"查看全部任务";clicked=>{root.library-show-tasks=true;root.library-action("jobs", "");}}}
              Note {text:root.library-job-summary;}
            }}
            if root.library-show-tasks: Panel {vertical-stretch:1;VerticalLayout {padding:12px;spacing:8px;
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
    if root.unlock-hint!="": Panel { x:22px; y:ActorsTheme.header-height + 8px; width:355px; height:44px;
        border-color:ActorsTheme.accent;
        Text { x:12px; width:parent.width - 24px; height:parent.height; text:root.unlock-hint;
            color:ActorsTheme.text; vertical-alignment:center; font-size:13px; }
    }
    if !root.library-mode && root.zoom-open: Rectangle {background:#17253dcc;TouchArea {} VerticalLayout {padding:36px;spacing:12px;HorizontalLayout {Caption {text:root.zoom-title;color:white;} Button {text:"关闭预览";clicked=>{root.zoom-open=false;}}} Image {source:root.zoom-image;image-fit:contain;} } }
    if !root.library-mode && root.confirmation!="": Rectangle {background:#17253d88;TouchArea {} Rectangle {width:460px;height:220px;x:(parent.width - self.width)/2;y:(parent.height - self.height)/2;background:ActorsTheme.panel;border-radius:12px;VerticalLayout {padding:24px;spacing:18px;Caption {text:"确认操作";} Text {text:root.confirmation;wrap:word-wrap;color:ActorsTheme.text;} HorizontalLayout {Button {text:"取消";clicked=>{root.action("cancel","");}} Button {text:"确认";clicked=>{root.action("confirm","");}}}} } }
}
}
