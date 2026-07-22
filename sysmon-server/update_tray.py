import sys

main_rs = """mod gui;
mod server;
mod sys;

use std::sync::Arc;
use tokio::sync::Mutex;
use eframe::egui;
use serde::{Deserialize, Serialize};
use std::time::Instant;
use tray_icon::{TrayIconBuilder, Icon};
use muda::{Menu, MenuItem, PredefinedMenuItem};

#[derive(Serialize, Deserialize, Clone)]
pub struct AppConfig {
    pub pin: String,
    pub port: u16,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self { pin: "1234".to_string(), port: 4201 }
    }
}

pub struct AppState {
    pub config: AppConfig,
    pub is_server_running: bool,
    pub should_restart_server: bool,
    pub weather_string: String,
    pub last_ping: Instant,
}

impl AppState {
    pub fn save_config(&self) {
        if let Ok(json) = serde_json::to_string_pretty(&self.config) {
            let _ = std::fs::write("config.json", json);
        }
    }
}

fn create_tray_icon() -> Icon {
    let rgba = vec![0, 200, 0, 255].repeat(32 * 32);
    Icon::from_rgba(rgba, 32, 32).unwrap()
}

fn main() -> eframe::Result<()> {
    let mut config = AppConfig::default();
    if let Ok(content) = std::fs::read_to_string("config.json") {
        if let Ok(parsed) = serde_json::from_str::<AppConfig>(&content) {
            config = parsed;
        }
    }

    let state = Arc::new(Mutex::new(AppState {
        config,
        is_server_running: true,
        should_restart_server: false,
        weather_string: "⛅ --°C".to_string(),
        last_ping: Instant::now() - std::time::Duration::from_secs(100),
    }));

    let server_state = state.clone();
    std::thread::spawn(move || {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
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

    // Hidden by default, intercepts close button
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([600.0, 450.0])
            .with_title("SysMon Server Dashboard")
            .with_visible(false)
            .with_close_button(false), 
        ..Default::default()
    };

    eframe::run_native(
        "SysMon Server",
        native_options,
        Box::new(|cc| {
            let tray_menu = Menu::new();
            let show_i = MenuItem::new("Show Dashboard", true, None);
            let start_i = MenuItem::new("Start Server", true, None);
            let stop_i = MenuItem::new("Stop Server", true, None);
            let quit_i = MenuItem::new("Quit", true, None);
            
            let _ = tray_menu.append(&show_i);
            let _ = tray_menu.append(&PredefinedMenuItem::separator());
            let _ = tray_menu.append(&start_i);
            let _ = tray_menu.append(&stop_i);
            let _ = tray_menu.append(&PredefinedMenuItem::separator());
            let _ = tray_menu.append(&quit_i);
            
            let tray_icon = TrayIconBuilder::new()
                .with_menu(Box::new(tray_menu))
                .with_tooltip("SysMon Server")
                .with_icon(create_tray_icon())
                .build()
                .unwrap();

            Box::new(gui::SysMonApp::new(cc, state, tray_icon, show_i, start_i, stop_i, quit_i))
        }),
    )
}
"""

gui_rs = """use std::sync::Arc;
use tokio::sync::Mutex;
use crate::AppState;
use crate::sys::MacroDef;
use eframe::egui;
use tray_icon::{TrayIcon, TrayIconEvent};
use muda::{MenuEvent, MenuItem};

#[derive(PartialEq)]
enum Tab {
    Dashboard,
    Macros,
}

pub struct SysMonApp {
    state: Arc<Mutex<AppState>>,
    current_tab: Tab,
    macros: Vec<MacroDef>,
    pin_input: String,
    new_macro: MacroDef,
    status_msg: String,
    initialized_pin: bool,
    _tray_icon: TrayIcon,
    menu_show: MenuItem,
    menu_start: MenuItem,
    menu_stop: MenuItem,
    menu_quit: MenuItem,
}

impl SysMonApp {
    pub fn new(
        _cc: &eframe::CreationContext<'_>, 
        state: Arc<Mutex<AppState>>, 
        tray_icon: TrayIcon,
        menu_show: MenuItem,
        menu_start: MenuItem,
        menu_stop: MenuItem,
        menu_quit: MenuItem,
    ) -> Self {
        let macros = if let Ok(content) = std::fs::read_to_string("macros.json") {
            serde_json::from_str(&content).unwrap_or_default()
        } else {
            Vec::new()
        };
        
        Self { 
            state,
            current_tab: Tab::Dashboard,
            macros,
            pin_input: String::new(),
            new_macro: MacroDef {
                button: None,
                kind: "chat".to_string(),
                label: "".to_string(),
                value: "".to_string(),
                color: "#4CAF50".to_string(),
                icon: "".to_string(),
            },
            status_msg: "Ready".to_string(),
            initialized_pin: false,
            _tray_icon: tray_icon,
            menu_show,
            menu_start,
            menu_stop,
            menu_quit,
        }
    }

    fn save_macros(&mut self) {
        if let Ok(json) = serde_json::to_string_pretty(&self.macros) {
            if std::fs::write("macros.json", json).is_ok() {
                self.status_msg = "Macros saved successfully!".to_string();
            } else {
                self.status_msg = "Failed to save macros.".to_string();
            }
        }
    }
}

impl eframe::App for SysMonApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Handle window close requested (hide to tray instead)
        if ctx.input(|i| i.viewport().close_requested()) {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
        }

        // Handle Tray Icon Events
        if let Ok(event) = TrayIconEvent::receiver().try_recv() {
            if event.click_type == tray_icon::ClickType::Left {
                ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
            }
        }
        
        // Handle Menu Events
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == self.menu_show.id() {
                ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));
                ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
            } else if event.id == self.menu_quit.id() {
                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            } else if event.id == self.menu_start.id() || event.id == self.menu_stop.id() {
                if let Ok(mut state) = self.state.try_lock() {
                    if event.id == self.menu_start.id() {
                        state.is_server_running = true;
                        state.should_restart_server = true;
                    } else {
                        state.is_server_running = false;
                    }
                }
            }
        }

        // Setup modern styling
        let mut style = (*ctx.style()).clone();
        let mut visuals = egui::Visuals::dark();
        
        visuals.widgets.noninteractive.rounding = egui::Rounding::same(8.0);
        visuals.widgets.inactive.rounding = egui::Rounding::same(8.0);
        visuals.widgets.hovered.rounding = egui::Rounding::same(8.0);
        visuals.widgets.active.rounding = egui::Rounding::same(8.0);
        visuals.window_rounding = egui::Rounding::same(12.0);
        
        visuals.selection.bg_fill = egui::Color32::from_rgb(0, 180, 255);
        
        style.spacing.item_spacing = egui::vec2(10.0, 10.0);
        style.spacing.button_padding = egui::vec2(12.0, 6.0);
        
        ctx.set_style(style);
        ctx.set_visuals(visuals);

        egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
            ui.add_space(5.0);
            ui.horizontal(|ui| {
                ui.heading("SysMon");
                ui.add_space(20.0);
                ui.selectable_value(&mut self.current_tab, Tab::Dashboard, "🛡 Dashboard");
                ui.selectable_value(&mut self.current_tab, Tab::Macros, "⚙ Macros");
            });
            ui.add_space(5.0);
        });

        egui::TopBottomPanel::bottom("bottom_panel").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.label(egui::RichText::new(&self.status_msg).color(egui::Color32::GRAY));
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                ui.add_space(10.0);
                
                match self.current_tab {
                    Tab::Dashboard => {
                        if let Ok(mut state) = self.state.try_lock() {
                            if !self.initialized_pin {
                                self.pin_input = state.config.pin.clone();
                                self.initialized_pin = true;
                            }

                            egui::Frame::group(ui.style()).fill(egui::Color32::from_gray(35)).show(ui, |ui| {
                                ui.set_width(ui.available_width());
                                ui.heading("Connection Status");
                                ui.add_space(10.0);
                                
                                ui.horizontal(|ui| {
                                    if state.last_ping.elapsed().as_secs() < 5 {
                                        ui.label(egui::RichText::new("🟢 ONLINE").color(egui::Color32::GREEN).size(18.0));
                                        ui.label("3DS is connected and transmitting telemetry.");
                                    } else {
                                        ui.label(egui::RichText::new("🔴 OFFLINE").color(egui::Color32::RED).size(18.0));
                                        ui.label("Waiting for 3DS connection...");
                                    }
                                });
                            });
                            ui.add_space(15.0);

                            egui::Frame::group(ui.style()).fill(egui::Color32::from_gray(35)).show(ui, |ui| {
                                ui.set_width(ui.available_width());
                                ui.heading("Server Settings");
                                ui.add_space(10.0);
                                
                                egui::Grid::new("server_settings_grid").num_columns(2).spacing([40.0, 15.0]).show(ui, |ui| {
                                    ui.label("Service State:");
                                    ui.horizontal(|ui| {
                                        if state.is_server_running {
                                            ui.label(egui::RichText::new("Running").color(egui::Color32::GREEN));
                                            if ui.button("⏹ Stop").clicked() {
                                                state.is_server_running = false;
                                                self.status_msg = "Server stopped.".to_string();
                                            }
                                        } else {
                                            ui.label(egui::RichText::new("Stopped").color(egui::Color32::RED));
                                            if ui.button("▶ Start").clicked() {
                                                state.is_server_running = true;
                                                state.should_restart_server = true;
                                                self.status_msg = "Server starting...".to_string();
                                            }
                                        }
                                    });
                                    ui.end_row();

                                    ui.label("Security PIN:");
                                    ui.add(egui::TextEdit::singleline(&mut self.pin_input).password(true));
                                    ui.end_row();

                                    let mut current_port = state.config.port.to_string();
                                    ui.label("Listen Port:");
                                    if ui.add(egui::TextEdit::singleline(&mut current_port).desired_width(80.0)).changed() {
                                        if let Ok(p) = current_port.parse::<u16>() {
                                            state.config.port = p;
                                        }
                                    }
                                    ui.end_row();
                                });

                                ui.add_space(15.0);
                                if ui.button("💾 Apply Settings & Restart").clicked() {
                                    state.config.pin = self.pin_input.clone();
                                    state.save_config();
                                    if state.is_server_running {
                                        state.should_restart_server = true;
                                    }
                                    self.status_msg = "Settings applied and server restarted.".to_string();
                                }
                            });
                        } else {
                            ui.centered_and_justified(|ui| {
                                ui.spinner();
                                ui.label("Syncing state...");
                            });
                        }
                    }
                    Tab::Macros => {
                        egui::Frame::group(ui.style()).fill(egui::Color32::from_gray(35)).show(ui, |ui| {
                            ui.set_width(ui.available_width());
                            ui.horizontal(|ui| {
                                ui.heading("Configured Macros");
                                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                    if ui.button("💾 Save All").clicked() {
                                        self.save_macros();
                                    }
                                });
                            });
                            ui.add_space(10.0);

                            let mut to_remove = None;
                            for (i, mac) in self.macros.iter_mut().enumerate() {
                                egui::Frame::dark_canvas(ui.style()).inner_margin(10.0).show(ui, |ui| {
                                    ui.set_width(ui.available_width());
                                    ui.horizontal(|ui| {
                                        ui.add(egui::TextEdit::singleline(&mut mac.label).desired_width(120.0).hint_text("Label"));
                                        
                                        ui.label("Type:");
                                        egui::ComboBox::from_id_source(format!("type_{}", i))
                                            .selected_text(&mac.kind)
                                            .width(80.0)
                                            .show_ui(ui, |ui| {
                                                ui.selectable_value(&mut mac.kind, "chat".to_string(), "chat");
                                                ui.selectable_value(&mut mac.kind, "cmd".to_string(), "cmd");
                                                ui.selectable_value(&mut mac.kind, "keys".to_string(), "keys");
                                                ui.selectable_value(&mut mac.kind, "text".to_string(), "text");
                                            });
                                        
                                        ui.label("Value:");
                                        ui.add(egui::TextEdit::singleline(&mut mac.value).desired_width(200.0).hint_text("Command / Text"));

                                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                            if ui.button("🗑").clicked() {
                                                to_remove = Some(i);
                                            }
                                        });
                                    });
                                    
                                    ui.add_space(5.0);
                                    ui.horizontal(|ui| {
                                        ui.label("Trigger Button:");
                                        let mut b = mac.button.clone().unwrap_or_default();
                                        if ui.add(egui::TextEdit::singleline(&mut b).desired_width(60.0).hint_text("A, B, X...")).changed() {
                                            mac.button = if b.is_empty() { None } else { Some(b) };
                                        }
                                        
                                        ui.add_space(20.0);
                                        ui.label("Color:");
                                        ui.add(egui::TextEdit::singleline(&mut mac.color).desired_width(80.0).hint_text("#RRGGBB"));
                                        
                                        ui.add_space(20.0);
                                        ui.label("Icon:");
                                        ui.add(egui::TextEdit::singleline(&mut mac.icon).desired_width(80.0).hint_text("Name"));
                                    });
                                });
                                ui.add_space(10.0);
                            }
                            if let Some(i) = to_remove {
                                self.macros.remove(i);
                                self.status_msg = "Macro removed. Remember to Save All.".to_string();
                            }
                        });
                        
                        ui.add_space(15.0);

                        egui::Frame::group(ui.style()).fill(egui::Color32::from_gray(40)).show(ui, |ui| {
                            ui.set_width(ui.available_width());
                            ui.heading("Add New Macro");
                            ui.add_space(10.0);

                            egui::Grid::new("new_macro_grid").num_columns(2).spacing([40.0, 15.0]).show(ui, |ui| {
                                ui.label("Label:");
                                ui.text_edit_singleline(&mut self.new_macro.label);
                                ui.end_row();

                                ui.label("Type:");
                                egui::ComboBox::from_label("")
                                    .selected_text(&self.new_macro.kind)
                                    .show_ui(ui, |ui| {
                                        ui.selectable_value(&mut self.new_macro.kind, "chat".to_string(), "chat");
                                        ui.selectable_value(&mut self.new_macro.kind, "cmd".to_string(), "cmd");
                                        ui.selectable_value(&mut self.new_macro.kind, "keys".to_string(), "keys");
                                        ui.selectable_value(&mut self.new_macro.kind, "text".to_string(), "text");
                                    });
                                ui.end_row();

                                ui.label("Trigger Button:");
                                let mut b = self.new_macro.button.clone().unwrap_or_default();
                                if ui.text_edit_singleline(&mut b).changed() {
                                    self.new_macro.button = if b.is_empty() { None } else { Some(b) };
                                }
                                ui.end_row();

                                ui.label("Value (Command/Text):");
                                ui.text_edit_singleline(&mut self.new_macro.value);
                                ui.end_row();

                                ui.label("Color (Hex):");
                                ui.text_edit_singleline(&mut self.new_macro.color);
                                ui.end_row();

                                ui.label("Icon name:");
                                ui.text_edit_singleline(&mut self.new_macro.icon);
                                ui.end_row();
                            });

                            ui.add_space(15.0);
                            if ui.button("➕ Add Macro").clicked() {
                                self.macros.push(self.new_macro.clone());
                                self.new_macro.label.clear();
                                self.new_macro.value.clear();
                                self.status_msg = "Macro added to list. Remember to Save All.".to_string();
                            }
                        });
                    }
                }
            });
        });
        
        ctx.request_repaint_after(std::time::Duration::from_millis(500));
    }
}
"""

with open("/home/andre/Desktop/Projects/SysMon/sysmon-server/src/main.rs", "w") as f:
    f.write(main_rs)
with open("/home/andre/Desktop/Projects/SysMon/sysmon-server/src/gui.rs", "w") as f:
    f.write(gui_rs)

