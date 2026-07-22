import sys

content = """use std::sync::Arc;
use tokio::sync::Mutex;
use crate::AppState;
use crate::sys::MacroDef;
use eframe::egui;

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
}

impl SysMonApp {
    pub fn new(_cc: &eframe::CreationContext<'_>, state: Arc<Mutex<AppState>>) -> Self {
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
        // Setup modern styling
        let mut style = (*ctx.style()).clone();
        let mut visuals = egui::Visuals::dark();
        
        // Rounded corners
        visuals.widgets.noninteractive.rounding = egui::Rounding::same(8.0);
        visuals.widgets.inactive.rounding = egui::Rounding::same(8.0);
        visuals.widgets.hovered.rounding = egui::Rounding::same(8.0);
        visuals.widgets.active.rounding = egui::Rounding::same(8.0);
        visuals.window_rounding = egui::Rounding::same(12.0);
        
        // Colors
        visuals.selection.bg_fill = egui::Color32::from_rgb(0, 180, 255); // Blue accent
        
        // Fonts spacing
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

                            // 3DS Connection Card
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

                            // Server Settings Card
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
                        // Existing Macros Card
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

                        // New Macro Card
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
        
        // Optimization: Request repaint 2 times a second (500ms) rather than instantly!
        // This stops Egui from wasting 100% of a CPU core while idle.
        ctx.request_repaint_after(std::time::Duration::from_millis(500));
    }
}
"""

with open("/home/andre/Desktop/Projects/SysMon/sysmon-server/src/gui.rs", "w") as f:
    f.write(content)

