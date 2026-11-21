slint::slint! {
#[edition = "1.19"]
import { ComboBox, LineEdit, ListView, ScrollView, Slider, Palette } from "std-widgets.slint";
import { ActorsTheme, Panel, Divider, PageTitle, SelectionMark, NightCheck } from "night_theme.slint";
export struct GroupRow { key:string, label:string, count:int, selected:bool }
export struct SongRow { name:string, title:string, subtitle:string, variant:string, cached:string, compiled:string, selected:bool, checked:bool }
component Title inherits Text { color:ActorsTheme.text; font-size:13px; font-weight:600; min-width:0px; overflow:elide; }
component Note inherits Text { color:ActorsTheme.muted; font-size:11px; min-width:0px; overflow:elide; }
component Action inherits Rectangle {
    in property <string> text; in property <image> icon; in property <bool> enabled:true; in property <bool> primary:false; in property <bool> chosen:false; in property <bool> selection-tab:false;
    property <bool> filled:root.primary || (root.selection-tab && root.chosen);
    callback clicked(); height:34px; min-width:64px; horizontal-stretch:0;
    background:root.filled && root.enabled ? ActorsTheme.accent : root.chosen ? ActorsTheme.selection : touch.has-hover || root.primary ? ActorsTheme.soft : ActorsTheme.control;
    border-radius:6px; border-width:root.chosen ? 2px : 1px; border-color:root.enabled && (root.primary || root.chosen) ? ActorsTheme.accent : ActorsTheme.control-line;
    accessible-role:button; accessible-label:root.text; accessible-enabled:root.enabled;
    HorizontalLayout {padding-left:8px;padding-right:8px;spacing:5px;alignment:center;
        if root.icon.width > 0: Rectangle {width:15px;background:transparent;Image {x:0px;y:(parent.height - 15px)/2;source:root.icon;width:15px;height:15px;colorize:!root.enabled ? ActorsTheme.muted : root.filled ? ActorsTheme.on-accent : ActorsTheme.text;}}
        Text {text:root.text;font-size:12px;font-weight:root.chosen ? 600 : 400;color:!root.enabled ? ActorsTheme.muted.with-alpha(0.55) : root.filled ? ActorsTheme.on-accent : root.chosen ? ActorsTheme.accent : ActorsTheme.text;vertical-alignment:center;}
    }
    if focus.has-focus: Rectangle { x:2px; y:2px; width:parent.width - 4px; height:parent.height - 4px; border-width:1px; border-radius:5px; border-color:ActorsTheme.accent; }
    focus := FocusScope {enabled:root.enabled; key-pressed(event)=>{if event.text == "\n" || event.text == " " {root.clicked();return accept;}return reject;}
        touch := TouchArea {enabled:root.enabled;clicked=>{focus.focus();root.clicked();}}
    }
}
component Range inherits Rectangle {
    in-out property <float> value:0; in property <float> maximum:100; in property <bool> enabled:true; callback changed(float); callback released();
    min-width:60px; height:24px; horizontal-stretch:1;
    property <float> portion:Math.clamp(root.value / Math.max(1,root.maximum),0,1);
    Rectangle {x:10px;y:(parent.height - 4px)/2;width:parent.width - 20px;height:4px;border-radius:2px;background:ActorsTheme.line;}
    Rectangle {x:10px;y:(parent.height - 4px)/2;width:(parent.width - 20px)*root.portion;height:4px;border-radius:2px;background:root.enabled ? ActorsTheme.accent : ActorsTheme.muted;}
    Rectangle {x:2px+(parent.width - 20px)*root.portion;y:(parent.height - 16px)/2;width:16px;height:16px;border-radius:8px;background:root.enabled ? ActorsTheme.accent : ActorsTheme.muted; border-width:native.has-focus ? 2px : 0px;border-color:ActorsTheme.text;}
    native := Slider {x:0px;y:0px;width:parent.width;height:parent.height;opacity:0.001;minimum:0;maximum:root.maximum;value <=> root.value;enabled:root.enabled;changed=>{root.changed(self.value);}released=>{root.released();}}
}
export component MusicWindow inherits Window {
    icon: @image-url("../../assets/music-library-window.png");
    title:"GakuActors · 乐库"; preferred-width:1280px; preferred-height:800px; min-width:1040px; min-height:660px;
    default-font-family:"Microsoft YaHei UI"; default-font-size:13px;background:ActorsTheme.paper;
    in property <[SongRow]> songs; in property <[GroupRow]> groups; in property <[string]> artist-options; in property <[string]> title-options;
    in-out property <string> search; in-out property <int> artist-index:0; in-out property <int> title-index:0; in-out property <int> kind-index:0; in-out property <int> status-index:0;
    init=>{Palette.color-scheme=ColorScheme.dark;}
    in property <int> group-mode:0; in property <string> result-label; in property <string> scope-title:"全部歌曲";
    in property <string> detail-title:"选择一首歌曲";in property <string> detail-subtitle;in property <string> resource;
    in property <string> cache-label;in property <string> compile-label;in property <string> notice;
    in property <bool> busy;in property <bool> has-song;in property <bool> cached;in property <bool> own-cached;in property <bool> compiled;
    in property <bool> playing;in property <bool> paused;in property <bool> selected-current;
    in property <string> now-title:"尚未播放";in property <string> now-artist:"选择版本后点击播放";
    in-out property <float> volume:100;in-out property <float> seek-value:0;in-out property <bool> seeking;
    in property <string> current-time:"00:00";in property <string> total-time:"00:00";
    in-out property <bool> filter-open:false;
    in property <bool> multi-mode;in property <int> checked-count;in property <int> visible-count;in property <int> visible-checked;
    in property <string> bulk-summary;in property <bool> can-bulk-download;in property <bool> can-bulk-compile;in property <bool> can-bulk-raw;in property <bool> can-bulk-wave;
    out property <bool> filter-active:root.title-index > 0 || root.kind-index > 0 || root.status-index > 0;
    out property <length> layout-search-center:search-box.x + search-box.width / 2;
    out property <length> layout-progress-center:progress-slider.y + progress-slider.height / 2;
    out property <length> layout-play-center:play-action.y + play-action.height / 2;
    out property <length> layout-left-height:left-card.height;out property <length> layout-list-height:list-card.height;
    out property <length> layout-footer-bottom:player-card.y + player-card.height;
    out property <length> layout-progress-width:progress-slider.width;
    out property <bool> search-focused:search-box.has-focus;
    public function dismiss-search() { search-box.clear-selection(); search-box.clear-focus(); }
    // Native pointer events arrive before controls handle the click. Releasing
    // the input here preserves that same click for lists, buttons and scrollbars.
    callback pointer-down(length,length);
    pointer-down(x,y)=>{if search-box.has-focus && (x < search-box.x || x >= search-box.x + search-box.width || y < search-box.y || y >= search-box.y + search-box.height) {root.dismiss-search();}}
    callback action(string,string);callback search-changed();callback seek(float);callback volume-changed(float);callback volume-save();
    header := Rectangle {x:0px;y:0px;width:root.width;height:ActorsTheme.header-height;background:ActorsTheme.header;
        Divider { x:0px; y:parent.height - 1px; width:parent.width; }
        Image { x:22px; y:(parent.height - 28px)/2; width:28px; height:28px; source:@image-url("../../assets/music-library-window.png"); image-fit:contain; }
        Text { x:62px; y:0px; height:parent.height; text:"GakuActors · 乐库"; color:ActorsTheme.text; font-size:19px; font-weight:650; vertical-alignment:center; }
        search-box := LineEdit {x:(parent.width - self.width)/2;y:17px;width:300px;height:34px;text <=> root.search;placeholder-text:"搜索歌名、演唱者或资源名";edited=>{root.search-changed();}accepted=>{root.dismiss-search();}key-pressed(event)=>{if event.text == Key.Escape {root.dismiss-search();return accept;}return reject;}changed has-focus=>{if !self.has-focus {self.clear-selection();}}}
        Action {x:parent.width - 94px;y:17px;width:78px;text:"刷新";enabled:!root.busy;clicked=>{root.action("scan","");}}
    }
    body := HorizontalLayout {x:12px;y:80px;width:root.width - 24px;height:root.height - 204px;spacing:ActorsTheme.workspace-gap;
        left-card := Panel {width:ActorsTheme.sidebar-width;background:ActorsTheme.aside;
            VerticalLayout {padding:12px;spacing:10px;
                Title {text:"资料库";}
                Action {text:"全部歌曲";clicked=>{root.action("reset","");}}
                HorizontalLayout {height:32px;spacing:5px;
                    Action {text:"演唱者";selection-tab:true;chosen:root.group-mode == 0;clicked=>{root.action("group-mode","0");}}
                    Action {text:"歌曲";selection-tab:true;chosen:root.group-mode == 1;clicked=>{root.action("group-mode","1");}}
                }
                Note {text:root.group-mode == 0 ? "演唱者目录" : "歌曲目录";}
                group-list := ListView {vertical-stretch:1;horizontal-scrollbar-policy:always-off;
                    for group in root.groups: Rectangle {width:group-list.visible-width;height:36px;
                        Rectangle {x:0px;y:2px;width:parent.width - 18px;height:32px;border-radius:5px;background:group.selected ? ActorsTheme.selection : group-touch.has-hover ? ActorsTheme.soft : transparent;border-width:group.selected ? 1px : 0px;border-color:ActorsTheme.accent;
                            if group.selected: SelectionMark { height:parent.height; }
                            HorizontalLayout {padding-left:10px;padding-right:7px;spacing:4px;
                                Text {text:group.label;min-width:0px;horizontal-stretch:1;overflow:elide;vertical-alignment:center;font-size:12px;font-weight:group.selected ? 600 : 400;color:group.selected ? ActorsTheme.accent : ActorsTheme.text;}
                                Note {text:group.count;width:25px;horizontal-alignment:right;vertical-alignment:center;color:group.selected ? ActorsTheme.accent : ActorsTheme.muted;}
                            }
                            group-touch := TouchArea {clicked=>{root.action("group",group.key);}}
                        }
                    }
                }
            }
        }
        list-card := Panel {horizontal-stretch:1;min-width:420px;
            VerticalLayout {padding:14px;spacing:10px;
                PageTitle { text:root.scope-title; }
                Note {text:root.result-label;}
                HorizontalLayout {height:34px;spacing:7px;
                    Action {width:84px;text:root.multi-mode ? "退出多选" : "多选";enabled:!root.busy;chosen:root.multi-mode;clicked=>{root.action("multi-toggle","");}}
                    Action {width:84px;text:"筛选";chosen:root.filter-active || root.filter-open;clicked=>{root.filter-open = !root.filter-open;}}
                    if root.filter-active: Action {width:84px;text:"取消筛选";clicked=>{root.action("clear-filter","");}}
                    Rectangle {horizontal-stretch:1;background:transparent;}
                }
                if root.filter-open: Rectangle {height:66px;background:ActorsTheme.aside;border-radius:6px;border-width:1px;border-color:ActorsTheme.line;
                    HorizontalLayout {padding:8px;spacing:8px;
                        VerticalLayout {spacing:3px;Note {text:"歌曲";}ComboBox {min-width:0px;model:root.title-options;current-index <=> root.title-index;selected=>{root.action("filter","");}}}
                        VerticalLayout {spacing:3px;Note {text:"版本";}ComboBox {model:["全部版本","通用","Live"];current-index <=> root.kind-index;selected=>{root.action("filter","");}}}
                        VerticalLayout {spacing:3px;Note {text:"状态";}ComboBox {model:["全部状态","已缓存","未缓存","已编译","未编译"];current-index <=> root.status-index;selected=>{root.action("filter","");}}}
                    }
                }
                HorizontalLayout {height:24px;padding-right:18px;spacing:7px;
                    if root.multi-mode: NightCheck {cross-axis-self-alignment:center;width:24px;checked:root.visible-count > 0 && root.visible-checked == root.visible-count;enabled:!root.busy && root.visible-count > 0;toggled=>{root.action("check-visible",self.checked ? "1" : "0");}}
                    Note {text:"歌曲 / 演唱者";horizontal-stretch:1;}
                    Note {text:"版本";width:80px;}
                    Note {text:"缓存";width:52px;horizontal-alignment:center;}
                    Note {text:"编译";width:52px;horizontal-alignment:center;}
                }
                versions := ListView {vertical-stretch:1;min-height:0px;horizontal-scrollbar-policy:always-off;
                    for row in root.songs: Rectangle {height:54px;width:versions.visible-width;
                        Rectangle {x:0px;y:2px;width:parent.width - 18px;height:parent.height - 4px;clip:true;border-radius:5px;background:row.selected ? ActorsTheme.selection : row.checked ? ActorsTheme.soft : ActorsTheme.panel;border-width:row.selected ? 1px : 0px;border-color:ActorsTheme.accent;
                            if row.selected: SelectionMark { height:parent.height; }
                            TouchArea {clicked=>{root.action("select",row.name);}}
                            HorizontalLayout {padding-left:10px;padding-right:7px;spacing:7px;
                                if root.multi-mode: NightCheck {cross-axis-self-alignment:center;width:24px;checked:row.checked;enabled:!root.busy;toggled=>{root.action("check",row.name);}}
                                Rectangle {horizontal-stretch:1;min-width:0px;background:transparent;
                                    VerticalLayout {spacing:2px;alignment:center;
                                        Title {text:row.title; color:row.selected ? ActorsTheme.accent : ActorsTheme.text;}
                                        Note {text:row.subtitle;}
                                    }
                                    TouchArea {clicked=>{root.action("select",row.name);}}
                                }
                                Note {text:row.variant;width:80px;vertical-alignment:center;}
                                Note {text:row.cached;width:52px;horizontal-alignment:center;vertical-alignment:center;color:row.cached == "已缓存" ? ActorsTheme.success : ActorsTheme.muted;}
                                Note {text:row.compiled;width:52px;horizontal-alignment:center;vertical-alignment:center;color:row.compiled == "已编译" ? ActorsTheme.success : ActorsTheme.muted;}
                            }
                            if !row.selected: Rectangle {x:0px;y:parent.height - 1px;width:parent.width;height:1px;background:ActorsTheme.line;}
                        }
                    }
                }
            }
        }
        right-column := Rectangle {width:244px;background:transparent;
            right-card := Panel {x:0px;y:0px;width:parent.width;height:Math.min(parent.height,root.multi-mode ? 390px : root.has-song ? 490px : 94px);
                ScrollView {x:11px;y:14px;width:parent.width - 22px;height:parent.height - 28px;horizontal-scrollbar-policy:always-off;
                    VerticalLayout {padding-right:16px;spacing:11px;alignment:start;
                        Note {text:root.multi-mode ? "多选资源 · 批量操作" : "所选版本 · 资源操作";}
                        if root.multi-mode: VerticalLayout {spacing:10px;
                            Text {text:"已勾选 " + root.checked-count + " 项";color:ActorsTheme.text;font-size:19px;font-weight:600;}
                            Note {text:"当前列表 " + root.visible-count + " 项，其中已勾选 " + root.visible-checked + " 项。";wrap:word-wrap;}
                            Action {text:"取消多选";enabled:!root.busy;clicked=>{root.action("clear-checks","");}}
                            Note {text:root.bulk-summary;wrap:word-wrap;}
                            Action {text:"下载所选缓存";enabled:root.can-bulk-download && !root.busy;clicked=>{root.action("bulk-download","");}}
                            Action {text:"编译所选歌曲";enabled:root.can-bulk-compile && !root.busy;clicked=>{root.action("bulk-compile","");}}
                            Action {text:"删除所选编译";enabled:root.can-bulk-wave && !root.busy;clicked=>{root.action("bulk-wave","");}}
                            Action {text:"删除所选缓存";enabled:root.can-bulk-raw && !root.busy;clicked=>{root.action("bulk-raw","");}}
                            Note {text:"编译只处理已缓存版本。删除缓存和删除编译互不影响。";wrap:word-wrap;}
                        }
                        if !root.multi-mode && !root.has-song: Note {text:"选择歌曲以查看资源。";wrap:word-wrap;}
                        if !root.multi-mode && root.has-song: VerticalLayout {spacing:10px;
                            Text {text:root.detail-title;color:ActorsTheme.text;font-size:20px;font-weight:600;wrap:word-wrap;}
                            Note {text:root.detail-subtitle;wrap:word-wrap;}
                            Divider {}
                            Title {text:"原始资源";}
                            Note {text:root.cache-label;wrap:word-wrap;color:root.cached ? ActorsTheme.success : ActorsTheme.muted;}
                            HorizontalLayout {height:34px;spacing:7px;
                                Action {text:"下载缓存";enabled:!root.cached && !root.busy;clicked=>{root.action("download","");}}
                                Action {text:"删除缓存";enabled:root.own-cached && !root.busy;clicked=>{root.action("delete-cache","");}}
                            }
                            Divider {}
                            Title {text:"编译音频";}
                            Note {text:root.compile-label;wrap:word-wrap;color:root.compiled ? ActorsTheme.success : ActorsTheme.muted;}
                            Note {text:root.compiled ? "WAV · 可直接播放和导出" : root.cached ? "原始缓存就绪，可提前编译" : "请先下载原始资源";wrap:word-wrap;}
                            HorizontalLayout {height:34px;spacing:7px;
                                Action {text:"提前编译";enabled:root.cached && !root.compiled && !root.busy;clicked=>{root.action("compile","");}}
                                Action {text:"删除编译";enabled:root.compiled && !root.busy;clicked=>{root.action("delete-compiled","");}}
                            }
                            export-button := Action {text:"导出所选歌曲";primary:true;enabled:!root.busy;clicked=>{root.action("export","");}}
                            Divider {}
                            Note {text:"资源文件名";}
                            Text {text:root.resource;color:ActorsTheme.text;font-size:11px;wrap:word-wrap;min-width:0px;}
                        }
                    }
                }
            }
        }
    }
    player-card := Panel {x:12px;y:root.height - 112px;width:root.width - 24px;height:82px;
        now := Rectangle {x:14px;y:15px;width:190px;height:52px;
            VerticalLayout {spacing:3px;alignment:center;Title {text:root.now-title;}Note {text:root.now-artist;}}
        }
        play-action := Action {x:218px;y:24px;width:64px;height:34px;text:"播放";primary:true;icon:@image-url("../../assets/transport-play.svg");enabled:root.has-song && !root.busy && (!root.selected-current || root.paused);clicked=>{root.action("play","");}}
        Action {x:289px;y:24px;width:64px;height:34px;text:"暂停";icon:@image-url("../../assets/transport-pause.svg");enabled:root.playing && !root.paused;clicked=>{root.action("pause","");}}
        Action {x:360px;y:24px;width:64px;height:34px;text:"停止";icon:@image-url("../../assets/transport-stop.svg");enabled:root.playing;clicked=>{root.action("stop","");}}
        progress-slider := Range {x:444px;y:29px;width:parent.width - 604px;maximum:1000;value <=> root.seek-value;enabled:root.playing;changed(value)=>{root.seeking=true;root.seek(value);}released=>{root.seeking=false;}}
        Note {x:454px;y:56px;text:root.current-time;}
        Note {x:progress-slider.x + progress-slider.width - 42px;y:56px;text:root.total-time;}
        Note {x:parent.width - 134px;y:13px;text:"音量 " + Math.round(root.volume) + "%";}
        Range {x:parent.width - 144px;y:29px;width:130px;value <=> root.volume;changed(value)=>{root.volume-changed(value);}released=>{root.volume-save();}}
    }
    Note {x:16px;y:root.height - 23px;width:root.width - 32px;height:18px;text:root.notice;}
}
}
