mod server;
mod streamer;
mod sys;
mod web_api;
mod controller;

use std::sync::Arc;
use tokio::sync::Mutex;
use serde::{Deserialize, Serialize};
use std::time::Instant;
use tray_icon::{TrayIconBuilder, Icon, TrayIconEvent};
use muda::{Menu, MenuItem, PredefinedMenuItem, MenuEvent};
use tao::event_loop::{ControlFlow, EventLoopBuilder};

fn default_web_port() -> u16 { 7342 }
fn default_stream_port() -> u16 { 7340 }
fn default_controller_port() -> u16 { 7339 }

#[derive(Serialize, Deserialize, Clone)]
pub struct AppConfig {
    pub pin: String,
    pub port: u16,
    pub update_interval_ms: u64,
    #[serde(default = "default_web_port")]
    pub web_port: u16,
    #[serde(default = "default_stream_port")]
    pub stream_port: u16,
    #[serde(default = "default_controller_port")]
    pub controller_port: u16,
    #[serde(default)]
    pub sort_by_ram: bool,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            pin: "1234".to_string(),
            port: 7341,
            stream_port: 7340,
            controller_port: 7339,
            update_interval_ms: 1500,
            web_port: 7342,
            sort_by_ram: false,
        }
    }
}

pub struct AppState {
    pub config: AppConfig,
    pub is_server_running: bool,
    pub should_restart_server: bool,
    pub weather_string: String,
    pub last_ping: Instant,
    pub macros: Vec<crate::sys::MacroDef>,
    pub latest_telemetry: Option<crate::sys::TelemetryData>,
    pub active_stream_port: u16,
    pub streamer_state: Arc<crate::streamer::StreamerState>,
}

impl AppState {
    pub fn save_config(&self) {
        let mut path = std::path::PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".to_string()));
        path.push(".config");
        path.push("sysmon-server");
        let _ = std::fs::create_dir_all(&path);
        path.push("config.json");

        if let Ok(json) = serde_json::to_string_pretty(&self.config) {
            let _ = std::fs::write(path, json);
        }
    }
    
    pub fn save_macros(&self) {
        let mut path = std::path::PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".to_string()));
        path.push(".config");
        path.push("sysmon-server");
        let _ = std::fs::create_dir_all(&path);
        path.push("macros.json");

        if let Ok(json) = serde_json::to_string_pretty(&self.macros) {
            let _ = std::fs::write(path, json);
        }
    }
}

fn create_tray_icon() -> Icon {
    let icon_data = include_bytes!("../../sysmon-3ds/icon.png");
    let img = image::load_from_memory(icon_data).unwrap().into_rgba8();
    let width = img.width();
    let height = img.height();
    Icon::from_rgba(img.into_raw(), width, height).unwrap()
}

fn main() {
    #[cfg(target_os = "linux")]
    if let Err(e) = gtk::init() {
        eprintln!("Failed to initialize GTK: {}", e);
    }

    let mut path = std::path::PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".to_string()));
    path.push(".config");
    path.push("sysmon-server");
    let _ = std::fs::create_dir_all(&path);
    path.push("config.json");

    let mut config = AppConfig::default();
    if let Ok(content) = std::fs::read_to_string(path) {
        if let Ok(parsed) = serde_json::from_str::<AppConfig>(&content) {
            config = parsed;
        }
    }

    let mut macros_path = std::path::PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".to_string()));
    macros_path.push(".config");
    macros_path.push("sysmon-server");
    macros_path.push("macros.json");

    let macros = if let Ok(content) = std::fs::read_to_string(macros_path) {
        serde_json::from_str(&content).unwrap_or_default()
    } else {
        Vec::new()
    };

    let initial_stream_port = config.stream_port;
    let streamer_state = Arc::new(streamer::StreamerState::new(initial_stream_port));

    let state = Arc::new(Mutex::new(AppState {
        config,
        is_server_running: true,
        should_restart_server: false,
        weather_string: "--°C".to_string(),
        last_ping: Instant::now() - std::time::Duration::from_secs(100),
        macros,
        latest_telemetry: None,
        active_stream_port: initial_stream_port,
        streamer_state: streamer_state.clone(),
    }));

    let server_state = state.clone();
    
    // We need to keep this thread alive, and we must clone the state to pass to web server
    let web_state = state.clone();
    let streamer_state_clone = streamer_state.clone();
    let streamer_app_state = state.clone();

    std::thread::spawn(move || {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            // Spawn the web config server
            let web_port = web_state.lock().await.config.web_port;
            tokio::spawn(async move {
                web_api::run_web_server(web_state, web_port).await;
            });

            // Spawn the screen streamer server
            let stream_port = streamer_app_state.lock().await.config.stream_port;
            let streamer_st = streamer_state_clone.clone();
            let st_app = streamer_app_state.clone();
            tokio::spawn(async move {
                let st_clone = streamer_st.clone();
                let bind_task = tokio::spawn(async move {
                    streamer::run_streamer(st_clone, stream_port).await;
                });
                
                // Wait briefly for port binding, then sync active_stream_port
                tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
                let actual_port = streamer_st.stream_port.load(std::sync::atomic::Ordering::SeqCst);
                {
                    let mut s = st_app.lock().await;
                    s.active_stream_port = actual_port;
                }
                let _ = bind_task.await;
            });

            // Spawn the UDP Controller server (port 7339)
            let (ctrl_port, ctrl_pin) = {
                let s = streamer_app_state.lock().await;
                (s.config.controller_port, s.config.pin.parse::<u32>().unwrap_or(1234))
            };
            tokio::spawn(async move {
                controller::run_controller_server(ctrl_port, ctrl_pin).await;
            });

            // TCP Server loop
            let mut server_task: Option<tokio::task::JoinHandle<()>> = None;
            loop {
                let (should_run, port, should_restart) = {
                    let mut s = server_state.lock().await;
                    let restart = s.should_restart_server;
                    if restart {
                        s.should_restart_server = false;
                    }
                    (s.is_server_running, s.config.port, restart)
                };

                if should_restart {
                    if let Some(task) = server_task.take() {
                        task.abort();
                    }
                }

                if should_run && server_task.is_none() {
                    let st = server_state.clone();
                    server_task = Some(tokio::spawn(async move {
                        server::run_server(st, port).await;
                    }));
                } else if !should_run && server_task.is_some() {
                    if let Some(task) = server_task.take() {
                        task.abort();
                    }
                }

                tokio::time::sleep(tokio::time::Duration::from_millis(200)).await;
            }
        });
    });

    let event_loop = EventLoopBuilder::new().build();

    let tray_menu = Menu::new();
    let status_i = MenuItem::new("○ Stream: STANDBY", false, None);
    let source_i = MenuItem::new("📺 Source: None Selected", false, None);
    let pick_os_i = MenuItem::new("🖥️ Choose Window / Screen (OS Dialog)...", true, None);
    let toggle_stream_i = MenuItem::new("▶️ Start Screen Sharing", true, None);
    let zoom_i = MenuItem::new("🔍 Zoom: FIT (Triangle)", true, None);
    let next_src_i = MenuItem::new("Stream: Next Monitor", true, None);
    let prev_src_i = MenuItem::new("Stream: Prev Monitor", true, None);
    let web_i = MenuItem::new("Open Web Config", true, None);
    let start_i = MenuItem::new("Start Server", true, None);
    let stop_i = MenuItem::new("Stop Server", true, None);
    let quit_i = MenuItem::new("Quit", true, None);
    
    let _ = tray_menu.append(&status_i);
    let _ = tray_menu.append(&source_i);
    let _ = tray_menu.append(&PredefinedMenuItem::separator());
    let _ = tray_menu.append(&pick_os_i);
    let _ = tray_menu.append(&toggle_stream_i);
    let _ = tray_menu.append(&zoom_i);
    let _ = tray_menu.append(&next_src_i);
    let _ = tray_menu.append(&prev_src_i);
    let _ = tray_menu.append(&PredefinedMenuItem::separator());
    let _ = tray_menu.append(&web_i);
    let _ = tray_menu.append(&start_i);
    let _ = tray_menu.append(&stop_i);
    let _ = tray_menu.append(&PredefinedMenuItem::separator());
    let _ = tray_menu.append(&quit_i);
    
    let _tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(tray_menu))
        .with_tooltip("SysMon Companion")
        .with_icon(create_tray_icon())
        .build()
        .unwrap();

    let menu_channel = MenuEvent::receiver();
    let _tray_channel = TrayIconEvent::receiver();

    let tray_streamer = streamer_state.clone();
    let mut last_tray_update = std::time::Instant::now();

    event_loop.run(move |_, _, control_flow| {
        *control_flow = ControlFlow::WaitUntil(std::time::Instant::now() + std::time::Duration::from_millis(100));

        // Periodic dynamic tray update (every 250ms)
        if last_tray_update.elapsed() >= std::time::Duration::from_millis(250) {
            last_tray_update = std::time::Instant::now();
            let is_sharing = tray_streamer.is_sharing_enabled.load(std::sync::atomic::Ordering::SeqCst);
            let is_active = tray_streamer.is_active.load(std::sync::atomic::Ordering::SeqCst);
            let zoom = tray_streamer.zoom.load(std::sync::atomic::Ordering::SeqCst);
            let src_name = tray_streamer.active_source_name.lock().map(|s| s.clone()).unwrap_or_else(|_| "Standby".to_string());

            let status_txt = if is_sharing && is_active {
                "● Stream: ACTIVE (Sending to 3DS)"
            } else if is_sharing {
                "○ Stream: READY (Waiting for 3DS)"
            } else {
                "⏸ Stream: PAUSED (Standby)"
            };

            let toggle_txt = if is_sharing {
                "⏸️ Pause Screen Sharing"
            } else {
                "▶️ Start Screen Sharing"
            };

            let zoom_txt = if zoom == 1 {
                "🔍 Zoom: 1:1 PIXEL CROP"
            } else {
                "🔍 Zoom: FIT (Triangle)"
            };

            let source_txt = format!("📺 Source: {}", src_name);

            status_i.set_text(status_txt);
            source_i.set_text(source_txt);
            toggle_stream_i.set_text(toggle_txt);
            zoom_i.set_text(zoom_txt);
        }

        if let Ok(event) = menu_channel.try_recv() {
            if event.id == quit_i.id() {
                *control_flow = ControlFlow::Exit;
            } else if event.id == web_i.id() {
                let web_port = state.blocking_lock().config.web_port;
                let _ = open::that(format!("http://127.0.0.1:{}/config", web_port));
            } else if event.id == start_i.id() {
                state.blocking_lock().is_server_running = true;
            } else if event.id == stop_i.id() {
                state.blocking_lock().is_server_running = false;
            } else if event.id == pick_os_i.id() {
                let st_pick = tray_streamer.clone();
                std::thread::spawn(move || {
                    let rt = tokio::runtime::Runtime::new().unwrap();
                    rt.block_on(async {
                        let _ = streamer::trigger_os_screencast_picker(st_pick).await;
                    });
                });
            } else if event.id == toggle_stream_i.id() {
                let cur = tray_streamer.is_sharing_enabled.load(std::sync::atomic::Ordering::SeqCst);
                tray_streamer.is_sharing_enabled.store(!cur, std::sync::atomic::Ordering::SeqCst);
            } else if event.id == next_src_i.id() {
                tray_streamer.active_pw_node.store(0, std::sync::atomic::Ordering::SeqCst);
                let count = streamer::list_sources().len().max(1);
                let cur = tray_streamer.source_idx.load(std::sync::atomic::Ordering::SeqCst);
                let next_idx = (cur + 1) % count;
                tray_streamer.source_idx.store(next_idx, std::sync::atomic::Ordering::SeqCst);
                let sources = streamer::list_sources();
                if let Ok(mut name) = tray_streamer.active_source_name.lock() {
                    *name = sources.get(next_idx).map(|s| s.name.clone()).unwrap_or_else(|| format!("Display {}", next_idx + 1));
                }
                tray_streamer.is_sharing_enabled.store(true, std::sync::atomic::Ordering::SeqCst);
                tray_streamer.source_version.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            } else if event.id == prev_src_i.id() {
                tray_streamer.active_pw_node.store(0, std::sync::atomic::Ordering::SeqCst);
                let count = streamer::list_sources().len().max(1);
                let cur = tray_streamer.source_idx.load(std::sync::atomic::Ordering::SeqCst);
                let prev_idx = if cur == 0 { count - 1 } else { cur - 1 };
                tray_streamer.source_idx.store(prev_idx, std::sync::atomic::Ordering::SeqCst);
                let sources = streamer::list_sources();
                if let Ok(mut name) = tray_streamer.active_source_name.lock() {
                    *name = sources.get(prev_idx).map(|s| s.name.clone()).unwrap_or_else(|| format!("Display {}", prev_idx + 1));
                }
                tray_streamer.is_sharing_enabled.store(true, std::sync::atomic::Ordering::SeqCst);
                tray_streamer.source_version.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            } else if event.id == zoom_i.id() {
                let cur = tray_streamer.zoom.load(std::sync::atomic::Ordering::SeqCst);
                tray_streamer.zoom.store(if cur == 0 { 1 } else { 0 }, std::sync::atomic::Ordering::SeqCst);
            }
        }
    });
}
