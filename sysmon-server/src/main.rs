mod server;
mod sys;
mod web_api;

use std::sync::Arc;
use tokio::sync::Mutex;
use serde::{Deserialize, Serialize};
use std::time::Instant;
use tray_icon::{TrayIconBuilder, Icon, TrayIconEvent};
use muda::{Menu, MenuItem, PredefinedMenuItem, MenuEvent};
use tao::event_loop::{ControlFlow, EventLoopBuilder};

fn default_web_port() -> u16 { 7342 }

#[derive(Serialize, Deserialize, Clone)]
pub struct AppConfig {
    pub pin: String,
    pub port: u16,
    pub update_interval_ms: u64,
    #[serde(default = "default_web_port")]
    pub web_port: u16,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self { pin: "1234".to_string(), port: 7341, update_interval_ms: 1500, web_port: 7342 }
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

    let state = Arc::new(Mutex::new(AppState {
        config,
        is_server_running: true,
        should_restart_server: false,
        weather_string: "--°C".to_string(),
        last_ping: Instant::now() - std::time::Duration::from_secs(100),
        macros,
        latest_telemetry: None,
    }));

    let server_state = state.clone();
    
    // We need to keep this thread alive, and we must clone the state to pass to web server
    let web_state = state.clone();
    
    std::thread::spawn(move || {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            // Spawn the web config server
            let web_port = web_state.lock().await.config.web_port;
            tokio::spawn(async move {
                web_api::run_web_server(web_state, web_port).await;
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
    let web_i = MenuItem::new("Open Web Config", true, None);
    let start_i = MenuItem::new("Start Server", true, None);
    let stop_i = MenuItem::new("Stop Server", true, None);
    let quit_i = MenuItem::new("Quit", true, None);
    
    let _ = tray_menu.append(&web_i);
    let _ = tray_menu.append(&PredefinedMenuItem::separator());
    let _ = tray_menu.append(&start_i);
    let _ = tray_menu.append(&stop_i);
    let _ = tray_menu.append(&PredefinedMenuItem::separator());
    let _ = tray_menu.append(&quit_i);
    
    let _tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(tray_menu))
        .with_tooltip("SysMon Server")
        .with_icon(create_tray_icon())
        .build()
        .unwrap();

    let menu_channel = MenuEvent::receiver();
    let _tray_channel = TrayIconEvent::receiver();

    event_loop.run(move |_, _, control_flow| {
        *control_flow = ControlFlow::WaitUntil(std::time::Instant::now() + std::time::Duration::from_millis(100));

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
            }
        }
    });
}
