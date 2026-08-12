use sysinfo::{Components, System};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Clone)]
pub struct ProcessInfo {
    pub pid: u32,
    pub name: String,
    pub cpu_percent: f32,
}

#[derive(Serialize, Clone)]
pub struct TelemetryData {
    pub cpu_fan: i32,
    pub gpu_fan: i32,
    pub cpu_temp: f32,
    pub gpu_temp: f32,
    pub free_ram: f32,
    pub cpu_usage: String,
    pub top_procs: Vec<ProcessInfo>,
    pub has_notification: bool,
    pub weather: String,
    pub now_playing: String,
    pub volume: u8,
    pub brightness: u8,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct MacroDef {
    pub button: Option<String>,
    #[serde(rename = "type")]
    pub kind: String,
    pub label: String,
    pub value: String,
    #[serde(default)]
    pub color: String,
    #[serde(default)]
    pub icon: String,
}

pub struct SysPoller {
    sys: System,
    components: Components,
    cpu_fan_path: Option<PathBuf>,
    gpu_fan_path: Option<PathBuf>,
}

impl SysPoller {
    pub fn new() -> Self {
        let mut sys = System::new_all();
        sys.refresh_all();
        
        let components = Components::new_with_refreshed_list();
        
        // Cache fan paths once to avoid iterating directories every 1.5s
        let mut cpu_fan_path = None;
        let mut gpu_fan_path = None;
        
        if let Ok(entries) = fs::read_dir("/sys/class/hwmon/") {
            for entry in entries.flatten() {
                let path = entry.path();
                if let Ok(name) = fs::read_to_string(path.join("name")) {
                    let name = name.trim().to_lowercase();
                    
                    if name == "amdgpu" || name.contains("gpu") {
                        if let Some(p) = Self::find_first_file(&path, "fan", "_input") {
                            gpu_fan_path = Some(p);
                        }
                    } 
                    
                    if name == "nct6775" || name == "it87" || name == "nct6798" || name.contains("asus") {
                        let fans = Self::find_all_files(&path, "fan", "_input");
                        if fans.len() > 0 {
                            cpu_fan_path = Some(fans[0].clone());
                        }
                        if fans.len() > 1 && gpu_fan_path.is_none() {
                            gpu_fan_path = Some(fans[1].clone());
                        }
                    }
                }
            }
        }

        Self { 
            sys, 
            components, 
            cpu_fan_path, 
            gpu_fan_path 
        }
    }

    pub fn poll_data(&mut self, weather: String, sort_by_ram: bool) -> TelemetryData {
        self.sys.refresh_cpu_usage();
        self.sys.refresh_memory();
        self.sys.refresh_processes(sysinfo::ProcessesToUpdate::All, true);
        self.components.refresh(true);

        let cpu_usage = self.sys.global_cpu_usage();
        let free_ram = self.sys.available_memory() as f32 / 1_000_000_000.0;

        let total_mem = self.sys.total_memory() as f32;
        let mut procs = Vec::new();
        for (pid, process) in self.sys.processes() {
            let cpu_val = if sort_by_ram {
                (process.memory() as f32 / total_mem) * 100.0
            } else {
                process.cpu_usage()
            };
            
            procs.push(ProcessInfo {
                pid: pid.as_u32(),
                name: process.name().to_string_lossy().to_string(),
                cpu_percent: cpu_val,
            });
        }
        procs.sort_by(|a, b| b.cpu_percent.partial_cmp(&a.cpu_percent).unwrap_or(std::cmp::Ordering::Equal));
        procs.truncate(5);

        let mut cpu_temp = 0.0;
        let mut gpu_temp = 0.0;
        
        for component in self.components.list() {
            let label = component.label().to_lowercase();
            if label.contains("cpu") || label.contains("core") || label.contains("tctl") || label.contains("package") {
                if cpu_temp == 0.0 {
                    cpu_temp = component.temperature().unwrap_or(0.0);
                }
            } else if label.contains("gpu") || label.contains("edge") || label.contains("junction") {
                if gpu_temp == 0.0 {
                    gpu_temp = component.temperature().unwrap_or(0.0);
                }
            }
        }

        let mut cpu_fan = 0;
        let mut gpu_fan = 0;
        
        if let Some(ref path) = self.cpu_fan_path {
            if let Ok(content) = fs::read_to_string(path) {
                cpu_fan = content.trim().parse::<i32>().unwrap_or(0);
            }
        }
        if let Some(ref path) = self.gpu_fan_path {
            if let Ok(content) = fs::read_to_string(path) {
                gpu_fan = content.trim().parse::<i32>().unwrap_or(0);
            }
        }

        let mut now_playing = "".to_string();
        if let Ok(player_finder) = mpris::PlayerFinder::new() {
            if let Ok(player) = player_finder.find_active() {
                if let Ok(metadata) = player.get_metadata() {
                    let title = metadata.title().unwrap_or("Unknown");
                    let artist = metadata.artists().unwrap_or(Vec::new()).join(", ");
                    if artist.is_empty() {
                        now_playing = title.to_string();
                    } else {
                        now_playing = format!("{} - {}", title, artist);
                    }
                }
            }
        }

        TelemetryData {
            cpu_fan,
            gpu_fan,
            cpu_temp,
            gpu_temp,
            free_ram,
            cpu_usage: format!("{:.1}", cpu_usage),
            top_procs: procs,
            has_notification: false,
            weather,
            now_playing,
            volume: get_volume_level(),
            brightness: get_brightness_level(),
        }
    }

    fn find_first_file(path: &Path, prefix: &str, suffix: &str) -> Option<PathBuf> {
        if let Ok(entries) = fs::read_dir(path) {
            for entry in entries.flatten() {
                let file_name = entry.file_name();
                let name = file_name.to_string_lossy();
                if name.starts_with(prefix) && name.ends_with(suffix) {
                    return Some(entry.path());
                }
            }
        }
        None
    }

    fn find_all_files(path: &Path, prefix: &str, suffix: &str) -> Vec<PathBuf> {
        let mut vals = Vec::new();
        if let Ok(entries) = fs::read_dir(path) {
            let mut paths: Vec<_> = entries.flatten().map(|e| e.path()).collect();
            paths.sort();
            for p in paths {
                let name = p.file_name().unwrap_or_default().to_string_lossy();
                if name.starts_with(prefix) && name.ends_with(suffix) {
                    vals.push(p);
                }
            }
        }
        vals
    }
}

fn get_volume_level() -> u8 {
    if let Ok(output) = Command::new("wpctl").args(&["get-volume", "@DEFAULT_AUDIO_SINK@"]).output() {
        let s = String::from_utf8_lossy(&output.stdout);
        if let Some(vol) = s.split_whitespace().nth(1) {
            if let Ok(v) = vol.parse::<f32>() {
                return (v * 100.0) as u8;
            }
        }
    }
    if let Ok(output) = Command::new("amixer").args(&["sget", "Master"]).output() {
        let s = String::from_utf8_lossy(&output.stdout);
        if let Some(start) = s.find('[') {
            if let Some(end) = s[start..].find('%') {
                if let Ok(v) = s[start+1..start+end].parse::<u8>() {
                    return v;
                }
            }
        }
    }
    72
}

fn get_brightness_level() -> u8 {
    if let Ok(output) = Command::new("brightnessctl").arg("get").output() {
        let val_s = String::from_utf8_lossy(&output.stdout);
        if let Ok(val) = val_s.trim().parse::<f32>() {
            if let Ok(max_output) = Command::new("brightnessctl").arg("m").output() {
                let max_s = String::from_utf8_lossy(&max_output.stdout);
                if let Ok(max_val) = max_s.trim().parse::<f32>() {
                    if max_val > 0.0 {
                        return ((val / max_val) * 100.0) as u8;
                    }
                }
            }
        }
    }
    if let Ok(entries) = std::fs::read_dir("/sys/class/backlight") {
        for entry in entries.flatten() {
            let path = entry.path();
            if let (Ok(val_str), Ok(max_str)) = (
                std::fs::read_to_string(path.join("brightness")),
                std::fs::read_to_string(path.join("max_brightness")),
            ) {
                if let (Ok(val), Ok(max)) = (val_str.trim().parse::<f32>(), max_str.trim().parse::<f32>()) {
                    if max > 0.0 {
                        return ((val / max) * 100.0) as u8;
                    }
                }
            }
        }
    }
    48
}

pub fn trigger_notify() {
    let _ = Command::new("notify-send")
        .args(&["3DS Pomodoro", "Time to stretch!"])
        .spawn();
}

pub fn kill_proc(pid: u32) {
    // Replaced massive /proc traversal with a direct native O(1) signal
    let _ = Command::new("kill").arg("-9").arg(pid.to_string()).spawn();
}

fn clamp_percent(value: u8) -> u8 {
    value.min(100)
}

fn run_first_available(commands: &[(&str, Vec<String>)]) {
    for (program, args) in commands {
        if Command::new(program).args(args).spawn().is_ok() {
            return;
        }
    }
}

pub fn set_volume_level(level: u8) {
    let level = clamp_percent(level);
    let level_str = format!("{}%", level);
    run_first_available(&[
        ("wpctl", vec!["set-volume".into(), "@DEFAULT_AUDIO_SINK@".into(), level_str.clone()]),
        ("pactl", vec!["set-sink-volume".into(), "@DEFAULT_SINK@".into(), level_str.clone()]),
        ("amixer", vec!["-D".into(), "pulse".into(), "sset".into(), "Master".into(), level_str]),
    ]);
}

pub fn set_brightness_level(level: u8) {
    let level = clamp_percent(level);
    let level_str = format!("{}%", level);
    
    if Command::new("brightnessctl").args(&["set", &level_str]).spawn().is_ok() { return; }
    if Command::new("xbacklight").args(&["-set", &level_str]).spawn().is_ok() { return; }
    
    if let Ok(entries) = std::fs::read_dir("/sys/class/backlight") {
        for entry in entries.flatten() {
            let path = entry.path();
            let name = path.file_name().unwrap_or_default().to_string_lossy();
            if let Ok(max_str) = std::fs::read_to_string(path.join("max_brightness")) {
                if let Ok(max) = max_str.trim().parse::<f32>() {
                    let target_val = ((level as f32 / 100.0) * max) as u32;
                    let _ = Command::new("busctl")
                        .args(&[
                            "call", "org.freedesktop.login1",
                            "/org/freedesktop/login1/session/auto",
                            "org.freedesktop.login1.Session",
                            "SetBrightness", "ssu", "backlight", &name,
                            &target_val.to_string()
                        ])
                        .spawn();
                    return;
                }
            }
        }
    }
}

use enigo::{Enigo, Keyboard, Key, Direction};
use std::sync::Arc;

fn parse_key(k: &str) -> Option<Key> {
    match k.to_lowercase().as_str() {
        "return" | "enter" => Some(Key::Return),
        "escape" | "esc" => Some(Key::Escape),
        "space" => Some(Key::Space),
        "backspace" => Some(Key::Backspace),
        "up" => Some(Key::UpArrow),
        "down" => Some(Key::DownArrow),
        "left" => Some(Key::LeftArrow),
        "right" => Some(Key::RightArrow),
        "tab" => Some(Key::Tab),
        "f1" => Some(Key::F1),
        "f2" => Some(Key::F2),
        "f3" => Some(Key::F3),
        "f4" => Some(Key::F4),
        "f5" => Some(Key::F5),
        "f6" => Some(Key::F6),
        "f7" => Some(Key::F7),
        "f8" => Some(Key::F8),
        "f9" => Some(Key::F9),
        "f10" => Some(Key::F10),
        "f11" => Some(Key::F11),
        "f12" => Some(Key::F12),
        _ => {
            if k.len() == 1 {
                Some(Key::Unicode(k.chars().next().unwrap()))
            } else {
                None
            }
        }
    }
}

fn press_key_combo(combo: &str, enigo: &mut Enigo) {
    let parts: Vec<&str> = combo.split('+').collect();
    let mut held = Vec::new();
    
    for (i, p) in parts.iter().enumerate() {
        let p = p.trim();
        let key = match p.to_lowercase().as_str() {
            "ctrl" | "control" => Some(Key::Control),
            "alt" => Some(Key::Option), // Option acts as Alt across platforms
            "shift" => Some(Key::Shift),
            "super" | "meta" | "windows" => Some(Key::Meta),
            _ => parse_key(p),
        };
        
        if let Some(k) = key {
            if i == parts.len() - 1 {
                let _ = enigo.key(k, Direction::Click);
            } else {
                let _ = enigo.key(k, Direction::Press);
                held.push(k);
                std::thread::sleep(std::time::Duration::from_millis(20));
            }
        }
    }
    
    for k in held.into_iter().rev() {
        let _ = enigo.key(k, Direction::Release);
    }
}

pub async fn trigger_macro(mac: &MacroDef, enigo: Arc<tokio::sync::Mutex<Enigo>>) {
    match mac.kind.as_str() {
        "cmd" => {
            let _ = Command::new("sh").arg("-c").arg(&mac.value).spawn();
        }
        "keys" => {
            let mut e = enigo.lock().await;
            press_key_combo(&mac.value, &mut e);
        }
        "open" => {
            let _ = open::that(&mac.value);
        }
        "text" | "chat" => {
            let mut e = enigo.lock().await;
            let _ = e.text(&mac.value);
            let _ = e.key(Key::Return, Direction::Click);
        }
        _ => {}
    }
}

pub async fn media_command(btn: &str) {
    if let Ok(player_finder) = mpris::PlayerFinder::new() {
        if let Ok(player) = player_finder.find_active() {
            match btn {
                "playpause" => { let _ = player.play_pause(); },
                "play" => { let _ = player.play(); },
                "pause" => { let _ = player.pause(); },
                "next" => { let _ = player.next(); },
                "prev" => { let _ = player.previous(); },
                _ => {}
            }
        }
    }
}
