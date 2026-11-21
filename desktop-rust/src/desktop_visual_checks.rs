//! Visual review for the same Slint window and snapshot projection as the shipped desktop.
use super::{key, model, rows, Entry, Manager, State};
use serde_json::{json, Value};
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
use slint::platform::{Platform, PlatformError, WindowAdapter, WindowEvent};
use slint::{ComponentHandle, Model, PhysicalSize, Rgb8Pixel};
use std::{fs, path::Path, path::PathBuf, rc::Rc, sync::mpsc};

struct ReviewPlatform(Rc<MinimalSoftwareWindow>);
impl Platform for ReviewPlatform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, PlatformError> {
        Ok(self.0.clone())
    }
}

const SIZES: [(&str, u32, u32, f32); 3] = [
    ("desktop", 1440, 940, 1.0),
    ("dpi150", 1980, 1485, 1.5),
    ("compact", 880, 440, 1.0),
];

fn verify_home_geometry(ui: &Manager, size: &str) {
    let (nav, directory) = if size == "compact" { (152.0, 180.0) } else { (186.0, 220.0) };
    assert!((ui.get_review_nav_width() - nav).abs() < 2.0, "{size}: navigation expanded");
    assert!((ui.get_review_home_directory_width() - directory).abs() < 2.0, "{size}: directory width changed");
    let header_y = ui.get_review_header_action_y();
    assert!((13.0..=18.0).contains(&header_y), "{size}: header buttons are off center (y = {header_y})");
}

// Measure the rendered surface itself. A fixed height never emits a Slint
// size-change callback on its first frame, but its painted bounds are visible.
fn verify_empty_panel(pixels: &[Rgb8Pixel], width: u32, height: u32, scale: f32, size: &str) {
    let x = width * 2 / 3;
    let rows = ((140.0 * scale) as u32..height).filter(|&y| {
        let p = pixels[(y * width + x) as usize];
        (p.r, p.g, p.b) == (26, 30, 41)
    });
    let (mut top, mut bottom) = (height, 0);
    for y in rows {
        top = top.min(y);
        bottom = bottom.max(y);
    }
    let panel_height = bottom.saturating_sub(top) as f32 / scale;
    assert!((145.0..=195.0).contains(&panel_height), "{size}: painted empty preset panel is {panel_height}px");
}

fn capture(window: &MinimalSoftwareWindow, output: Option<&Path>, state: &str, sizes: &[(&str, u32, u32, f32)], manager: Option<&Manager>) {
    for &(size_name, width, height, scale) in sizes {
        window.dispatch_event(WindowEvent::ScaleFactorChanged { scale_factor: scale });
        window.set_size(PhysicalSize::new(width, height));
        assert!((window.scale_factor() - scale).abs() < 0.001);
        window.request_redraw();
        slint::platform::update_timers_and_animations();
        let mut pixels = vec![Rgb8Pixel::default(); (width * height) as usize];
        assert!(window.draw_if_needed(|renderer| { renderer.render(&mut pixels, width as usize); }));
        let top = pixels[(12 * width + width / 2) as usize];
        let body = pixels[(height * width - width - 12) as usize];
        assert_ne!((top.r, top.g, top.b), (0, 0, 0), "{state}/{size_name}: blank frame");
        assert!(top.r < 65 && body.r < 65, "{state}/{size_name}: unexpected light background");
        let name = format!("{state}-{size_name}");
        if let Some(dir) = output {
            let file = fs::File::create(dir.join(format!("{name}.png"))).unwrap();
            let mut encoder = png::Encoder::new(file, width, height);
            encoder.set_color(png::ColorType::Rgb);
            let bytes: Vec<_> = pixels.iter().flat_map(|p| [p.r, p.g, p.b]).collect();
            encoder.write_header().unwrap().write_image_data(&bytes).unwrap();
        }
        if let Some(ui) = manager {
            let footer_start = height - (50.0 * scale) as u32;
            let music_icon_painted = (footer_start..height).any(|y|
                (((20.0 * scale) as u32)..((44.0 * scale) as u32)).any(|x| {
                    let p = pixels[(y*width+x) as usize];
                    p.r > 130 && p.g > 125 && p.b > 170
                }));
            assert_eq!(music_icon_painted, ui.get_libraries_unlocked(),
                "{state}/{size_name}: last navigation icon must be visible only after unlock");
            if state.starts_with("home-") {
                verify_home_geometry(ui, size_name);
                if state != "home-populated" {
                    verify_empty_panel(&pixels, width, height, scale, size_name);
                }
            }
        }
        println!("{name}: physical {width}x{height}, DPR {scale}");
    }
}

#[test]
fn render_native_review_matrix() {
    let output = std::env::var_os("GAKUACTORS_VISUAL_OUTPUT").map(PathBuf::from);
    if let Some(dir) = &output { fs::create_dir_all(dir).unwrap(); }
    let output = output.as_deref();
    let window = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(ReviewPlatform(window.clone()))).unwrap();
    crate::music_window::platform::configure_font_fallback();
    let ui = Manager::new().unwrap();
    ui.set_desktop_version(env!("CARGO_PKG_VERSION").into());
    assert_eq!(ui.get_page(), 0, "capture the actual startup page first");
    let fixture: Value = serde_json::from_str(include_str!("../../tests/visual-fixtures/night-workspace.json")).unwrap();
    let (tx, _rx) = mpsc::sync_channel(256);
    let mut state = State {
        snapshot: json!({"characters":fixture["characters"], "bodies":fixture["bodies"], "heads":fixture["heads"], "writable":true}),
        tx: Some(tx),
        connection_detail: "未检测到游戏启动".into(),
        game_configured: true,
        notes_path: Some(PathBuf::from("unused-visual-fixture.json")),
        ..Default::default()
    };
    state.render(&ui);
    ui.show().unwrap();
    assert_eq!(ui.get_presets().row_count(), 0);
    capture(&window, output, "home-offline", &SIZES, Some(&ui));
    state.snapshot = json!({"writable":true});
    state.render(&ui);
    assert_eq!(ui.get_characters().row_count(), 0);
    capture(&window, output, "home-startup-empty", &SIZES[..1], Some(&ui));
    state.act(&ui, "nav", "2");
    capture(&window, output, "scene-props-empty", &SIZES[..1], Some(&ui));
    state.act(&ui, "nav", "0");
    use slint::{LogicalPosition, platform::PointerEventButton};
    let position = LogicalPosition::new(92.0, 32.0);
    for count in 1..=10 {
        window.dispatch_event(WindowEvent::PointerPressed { position, button: PointerEventButton::Left });
        window.dispatch_event(WindowEvent::PointerReleased { position, button: PointerEventButton::Left });
        assert_eq!(ui.get_libraries_unlocked(), count == 10);
        if (5..10).contains(&count) {
            assert!(ui.get_unlock_hint().contains(&format!(" {} 次", 10 - count)));
        } else {
            assert!(ui.get_unlock_hint().is_empty());
        }
    }
    state.snapshot = fixture.clone();
    for (name, value) in [
        ("runtime", true), ("admitted", true), ("in_live", true), ("writable", true),
        ("resource_enabled", true), ("motion_handoff_available", true),
    ] {
        state.snapshot[name] = value.into();
    }
    state.snapshot["active_scene_name"] = "liv_workspace_demo".into();
    state.snapshot["resource_status"] = "已扫描当前场景".into();
    state.connected = true;
    state.render(&ui);
    assert_eq!(ui.get_presets().row_count(), 3);
    capture(&window, output, "home-populated", &SIZES, Some(&ui));
    state.act(&ui, "edit", "");
    state.render(&ui);
    assert_eq!(ui.get_page(), 4);
    capture(&window, output, "preset-edit", &SIZES[..1], Some(&ui));
    state.actor = key(&fixture["actors"][0]);
    state.act(&ui, "nav", "1");
    assert_eq!(ui.get_stage_markers().row_count(), 3);
    capture(&window, output, "scene-actors", &SIZES, Some(&ui));
    state.act(&ui, "nav", "2");
    let resources = rows(&state.snapshot,"scene_resources");
    state.act(&ui, "resource", &key(&resources[2]));
    let moving_box = resources.iter().find(|row| row["name"] == "移动箱").unwrap();
    state.act(&ui, "tree-toggle", &key(moving_box));
    assert!(ui.get_note_writable());
    ui.set_resource_note("副歌背景装饰".into());
    assert_eq!(ui.get_asset_name(), "背景飘带");
    assert!(ui.get_moving_assets().row_count() > 2);
    capture(&window, output, "scene-props", &SIZES, Some(&ui));
    let original = state.snapshot["scene_resources"].clone();
    let mut large = resources.clone();
    large.extend((20..3020).map(|id| json!({
        "kind":4,"scene":10,"instance":id,"name":format!("场景节点 {id}"),"generation":1,"active":true
    })));
    state.act(&ui, "nav", "0");
    state.snapshot["scene_resources"] = large.into();
    state.act(&ui, "nav", "2");
    assert!(ui.get_fixed_assets().row_count() >= 3000);
    capture(&window, None, "scene-props-large", &SIZES[..1], Some(&ui));
    state.snapshot["scene_resources"] = original;

    state.snapshot["resource_enabled"] = false.into();
    state.snapshot["in_live"] = false.into();
    state.snapshot["admitted"] = false.into();
    state.render(&ui);
    capture(&window, output, "scene-props-readonly", &SIZES[..1], Some(&ui));
    state.act(&ui, "nav", "5");
    capture(&window, output, "stage-import", &SIZES[..1], Some(&ui));
    ui.set_library_mode(true);
    ui.set_library_status("资源库已连接".into());
    ui.set_library_manifest("示例清单".into());
    ui.set_library_resources(model(vec![
        Entry { label:"初始演出服".into(), tags:"模型部件".into(), detail:"已缓存 · 2 个依赖".into(), ..Default::default() },
        Entry { label:"舞台背景".into(), tags:"舞台与场景".into(), detail:"1 个依赖".into(), ..Default::default() },
    ]));
    capture(&window, output, "resource-library", &SIZES[..1], None);
    ui.hide().unwrap();
    drop(ui);
    let music = crate::music_window::visual_fixture();
    music.show().unwrap();
    capture(&window, output, "music", &[("wide", 1280, 800, 1.0), ("compact-dpi150", 1560, 990, 1.5)], None);
    music.hide().unwrap();
}
