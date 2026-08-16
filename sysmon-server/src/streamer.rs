use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicU8, AtomicU16, AtomicU32, AtomicUsize};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StreamSource {
    pub id: usize,
    pub kind: String, // "monitor", "window", "os_picker"
    pub name: String,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct StreamStatus {
    pub is_sharing_enabled: bool,
    pub is_active: bool,
    pub port: u16,
    pub source_name: String,
    pub mode: u8,
    pub zoom: u8,
}

#[cfg(not(feature = "cam"))]
mod imp {
    use super::*;

    pub struct StreamerState {
        pub stream_port: AtomicU16,
        pub is_sharing_enabled: AtomicBool,
        pub is_active: AtomicBool,
        pub source_idx: AtomicUsize,
        pub source_version: AtomicU32,
        pub active_pw_node: AtomicU32,
        pub active_source_name: std::sync::Mutex<String>,
        pub mode: AtomicU8,
        pub zoom: AtomicU8,
        pub format: AtomicU8,
    }

    impl StreamerState {
        pub fn new(default_port: u16) -> Self {
            Self {
                stream_port: AtomicU16::new(default_port),
                is_sharing_enabled: AtomicBool::new(false),
                is_active: AtomicBool::new(false),
                source_idx: AtomicUsize::new(0),
                source_version: AtomicU32::new(1),
                active_pw_node: AtomicU32::new(0),
                active_source_name: std::sync::Mutex::new("Disabled (No-Cam build)".to_string()),
                mode: AtomicU8::new(0),
                zoom: AtomicU8::new(0),
                format: AtomicU8::new(0),
            }
        }
    }

    pub async fn trigger_os_screencast_picker(_state: Arc<StreamerState>) -> Result<String, String> {
        Err("Screen streaming is disabled in this server build.".to_string())
    }

    pub fn list_sources() -> Vec<StreamSource> {
        Vec::new()
    }

    #[allow(dead_code)]
    pub async fn run_streamer(_state: Arc<StreamerState>, _preferred_port: u16) {
        // No-op for nocam build
    }
}

#[cfg(feature = "cam")]
mod imp {
    use super::*;
    use std::sync::atomic::Ordering;
    use std::time::Duration;
    use std::os::fd::{AsRawFd, OwnedFd};
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::{TcpListener, TcpStream};
    use xcap::Monitor;
    use image::imageops::FilterType;

    pub struct StreamerState {
        pub stream_port: AtomicU16,
        pub is_sharing_enabled: AtomicBool, // Default false: Standby until user picks source
        pub is_active: AtomicBool,
        pub source_idx: AtomicUsize,
        pub source_version: AtomicU32,
        pub active_pw_node: AtomicU32,
        pub active_pw_fd: std::sync::Mutex<Option<OwnedFd>>,
        pub active_source_name: std::sync::Mutex<String>,
        pub mode: AtomicU8, // 0 = Bottom Tab (240x160), 1 = Top Screen (400x240)
        pub zoom: AtomicU8, // 0 = Fit (Area Averaged), 1 = 1:1 Native Pixel Crop
        pub format: AtomicU8, // 0 = RGB565, 1 = JPEG, 2 = Standby
        pub portal_session: tokio::sync::Mutex<Option<ashpd::desktop::Session<ashpd::desktop::screencast::Screencast>>>,
    }

    impl StreamerState {
    pub fn new(default_port: u16) -> Self {
        let sources = list_sources();
        let default_name = sources.first().map(|s| s.name.clone()).unwrap_or_else(|| "Display 1".to_string());
        Self {
            stream_port: AtomicU16::new(default_port),
            is_sharing_enabled: AtomicBool::new(true), // Enabled by default (Primary Display)
            is_active: AtomicBool::new(false),
            source_idx: AtomicUsize::new(0),
            source_version: AtomicU32::new(1),
            active_pw_node: AtomicU32::new(0),
            active_pw_fd: std::sync::Mutex::new(None),
            active_source_name: std::sync::Mutex::new(default_name),
            mode: AtomicU8::new(0),
            zoom: AtomicU8::new(0),
            format: AtomicU8::new(3), // Delta 8x8 Morton Tiles
            portal_session: tokio::sync::Mutex::new(None),
        }
    }
}

/// Triggers the native Linux OS Screen / Window Sharing dialog (OBS / Chrome style)
/// and passes the authenticated PipeWire file descriptor to GStreamer
pub async fn trigger_os_screencast_picker(state: Arc<StreamerState>) -> Result<String, String> {
    #[cfg(target_os = "linux")]
    {
        use ashpd::desktop::screencast::{CursorMode, Screencast, SourceType, SelectSourcesOptions, StartCastOptions};
        use ashpd::desktop::PersistMode;

        println!("Invoking XDG Desktop Portal ScreenCast dialog...");

        let proxy = Screencast::new().await.map_err(|e| e.to_string())?;
        let session = proxy.create_session(Default::default()).await.map_err(|e| e.to_string())?;

        let select_opts = SelectSourcesOptions::default()
            .set_cursor_mode(CursorMode::Hidden)
            .set_sources(SourceType::Monitor | SourceType::Window)
            .set_multiple(false)
            .set_persist_mode(PersistMode::Application);

        proxy.select_sources(&session, select_opts).await.map_err(|e| e.to_string())?;

        let request = proxy.start(&session, None, StartCastOptions::default()).await.map_err(|e| e.to_string())?;
        match request.response() {
            Ok(streams_response) => {
                if let Some(stream) = streams_response.streams().first() {
                    let node_id = stream.pipe_wire_node_id();
                    let desc = format!("OS Window/Screen (#{})", node_id);
                    println!("User selected OS stream node: {}", node_id);

                    // Request authenticated PipeWire file descriptor from portal
                    let pw_fd = proxy.open_pipe_wire_remote(&session, Default::default()).await.map_err(|e| e.to_string())?;
                    println!("Obtained authenticated PipeWire remote FD: {}", pw_fd.as_raw_fd());

                    // Clear FD_CLOEXEC so child gst-launch-1.0 process inherits this file descriptor
                    unsafe {
                        libc::fcntl(pw_fd.as_raw_fd(), libc::F_SETFD, 0);
                    }

                    // Close previous portal session and FD
                    {
                        let mut sess_lock = state.portal_session.lock().await;
                        *sess_lock = None;
                    }
                    if let Ok(mut fd_lock) = state.active_pw_fd.lock() {
                        *fd_lock = None;
                    }

                    // Store active node ID and FD and hold the portal session alive
                    state.active_pw_node.store(node_id, Ordering::SeqCst);
                    if let Ok(mut fd_lock) = state.active_pw_fd.lock() {
                        *fd_lock = Some(pw_fd);
                    }
                    if let Ok(mut name) = state.active_source_name.lock() {
                        *name = desc.clone();
                    }
                    let mut sess_lock = state.portal_session.lock().await;
                    *sess_lock = Some(session);

                    // Automatically enable sharing and bump source version so active clients switch instantly
                    state.is_sharing_enabled.store(true, Ordering::SeqCst);
                    state.source_version.fetch_add(1, Ordering::SeqCst);

                    return Ok(desc);
                } else {
                    return Err("No stream selected (User cancelled picker dialog)".to_string());
                }
            }
            Err(e) => {
                println!("OS screencast picker cancelled: {}", e);
                return Err(format!("Picker cancelled: {}", e));
            }
        }
    }

    #[cfg(not(target_os = "linux"))]
    Err("OS ScreenCast portal is only supported on Linux".to_string())
}

pub fn list_sources() -> Vec<StreamSource> {
    let mut list = Vec::new();
    let mut id = 0;

    if let Ok(monitors) = Monitor::all() {
        for (i, m) in monitors.iter().enumerate() {
            let name = m.name().unwrap_or_else(|_| format!("Display {}", i + 1));
            list.push(StreamSource {
                id,
                kind: "monitor".to_string(),
                name: if name.is_empty() { format!("Display {}", i + 1) } else { name },
                width: m.width().unwrap_or(0),
                height: m.height().unwrap_or(0),
            });
            id += 1;
        }
    }

    if list.is_empty() {
        list.push(StreamSource {
            id: 0,
            kind: "monitor".to_string(),
            name: "Primary Display".to_string(),
            width: 1920,
            height: 1080,
        });
    }

    list
}

pub async fn run_streamer(state: Arc<StreamerState>, preferred_port: u16) {
    let (listener, bound_port) = bind_stream_listener(preferred_port).await;
    state.stream_port.store(bound_port, Ordering::SeqCst);

    loop {
        if let Ok((socket, addr)) = listener.accept().await {
            let _ = socket.set_nodelay(true);
            println!("Stream client connected from {}", addr);
            let state_clone = state.clone();
            tokio::spawn(async move {
                handle_stream_client(socket, state_clone).await;
            });
        }
    }
}

async fn bind_stream_listener(preferred_port: u16) -> (TcpListener, u16) {
    for p in preferred_port..preferred_port.saturating_add(50) {
        let addr = std::net::SocketAddr::from(([0, 0, 0, 0], p));
        if let Ok(listener) = TcpListener::bind(addr).await {
            println!("Screen Stream TCP server listening on {}", addr);
            return (listener, p);
        }
    }

    let listener = TcpListener::bind("0.0.0.0:0").await.expect("Failed to bind any stream port");
    let actual_port = listener.local_addr().unwrap().port();
    println!("Screen Stream TCP server fallback listening on 0.0.0.0:{}", actual_port);
    (listener, actual_port)
}

async fn handle_stream_client(socket: TcpStream, state: Arc<StreamerState>) {
    let (mut reader, mut writer) = socket.into_split();
    let is_streaming = Arc::new(AtomicBool::new(true));
    state.is_active.store(true, Ordering::SeqCst);

    let is_streaming_read = is_streaming.clone();
    let state_read = state.clone();
    let request_keyframe = Arc::new(AtomicBool::new(true)); // Initial frame is always a keyframe
    let request_keyframe_cmd = request_keyframe.clone();

    // Command listener from 3DS
    tokio::spawn(async move {
        let mut buf = [0u8; 64];
        loop {
            match reader.read(&mut buf).await {
                Ok(0) => {
                    is_streaming_read.store(false, Ordering::SeqCst);
                    break;
                }
                Ok(n) => {
                    for &b in &buf[..n] {
                        match b {
                            b'S' => {
                                is_streaming_read.store(true, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'p' => is_streaming_read.store(false, Ordering::SeqCst),
                            b'B' => {
                                state_read.mode.store(0, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'T' => {
                                state_read.mode.store(1, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'Z' => {
                                let cur = state_read.zoom.load(Ordering::SeqCst);
                                state_read.zoom.store(if cur == 0 { 1 } else { 0 }, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'D' => {
                                state_read.format.store(3, Ordering::SeqCst); // Delta 8x8 Morton Tiles
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'K' => {
                                // Explicit Keyframe / Resync request from client
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'J' => {
                                state_read.format.store(1, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                            }
                            b'R' => {
                                state_read.format.store(0, Ordering::SeqCst);
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                            }
                            b'O' => {
                                // Trigger OS Screencast dialog remotely from 3DS
                                let st_pick = state_read.clone();
                                let kf_pick = request_keyframe_cmd.clone();
                                tokio::spawn(async move {
                                    if trigger_os_screencast_picker(st_pick).await.is_ok() {
                                        kf_pick.store(true, Ordering::SeqCst);
                                    }
                                });
                            }
                            b'N' => {
                                // Cycle to next display
                                state_read.active_pw_node.store(0, Ordering::SeqCst);
                                let cur = state_read.source_idx.load(Ordering::SeqCst);
                                let sources = list_sources();
                                let next_idx = (cur + 1) % sources.len().max(1);
                                state_read.source_idx.store(next_idx, Ordering::SeqCst);
                                if let Ok(mut name) = state_read.active_source_name.lock() {
                                    *name = sources.get(next_idx).map(|s| s.name.clone()).unwrap_or_else(|| format!("Display {}", next_idx + 1));
                                }
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                state_read.source_version.fetch_add(1, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            b'P' => {
                                // Cycle to previous display
                                state_read.active_pw_node.store(0, Ordering::SeqCst);
                                let cur = state_read.source_idx.load(Ordering::SeqCst);
                                let sources = list_sources();
                                let prev_idx = if cur == 0 { sources.len().max(1) - 1 } else { cur - 1 };
                                state_read.source_idx.store(prev_idx, Ordering::SeqCst);
                                if let Ok(mut name) = state_read.active_source_name.lock() {
                                    *name = sources.get(prev_idx).map(|s| s.name.clone()).unwrap_or_else(|| format!("Display {}", prev_idx + 1));
                                }
                                state_read.is_sharing_enabled.store(true, Ordering::SeqCst);
                                state_read.source_version.fetch_add(1, Ordering::SeqCst);
                                request_keyframe_cmd.store(true, Ordering::SeqCst);
                            }
                            _ => {}
                        }
                    }
                }
                Err(_) => {
                    is_streaming_read.store(false, Ordering::SeqCst);
                    break;
                }
            }
        }
    });

    // Frame capture and send loop with deadline interval pacing (20 FPS nominal = 50ms)
    let mut interval = tokio::time::interval(Duration::from_millis(50));
    interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

    let mut pw_child: Option<tokio::process::Child> = None;
    let mut pw_stdout: Option<tokio::process::ChildStdout> = None;
    let mut current_pw_node = 0;
    let mut current_src_ver = 0;
    let mut current_target_w = 0;
    let mut current_target_h = 0;
    let mut current_zoom = 0;

    let mut prev_frame: Vec<u8> = Vec::new();
    let mut last_heartbeat = tokio::time::Instant::now();

    while is_streaming.load(Ordering::SeqCst) {
        interval.tick().await;

        let mode = state.mode.load(Ordering::SeqCst);
        let zoom = state.zoom.load(Ordering::SeqCst);
        let format = state.format.load(Ordering::SeqCst);
        let src_idx = state.source_idx.load(Ordering::SeqCst);
        let src_ver = state.source_version.load(Ordering::SeqCst);
        let pw_node = state.active_pw_node.load(Ordering::SeqCst);

        // Standby Mode Heartbeat (when sharing is paused or disabled)
        if !state.is_sharing_enabled.load(Ordering::SeqCst) {
            if let Some(mut old_child) = pw_child.take() {
                let _ = old_child.kill().await;
            }
            pw_stdout = None;
            current_pw_node = 0;
            prev_frame.clear();

            let mut header = [0u8; 46];
            header[0..4].copy_from_slice(b"SCAM");
            header[4..6].copy_from_slice(&(240u16).to_be_bytes());
            header[6..8].copy_from_slice(&(160u16).to_be_bytes());
            header[8] = 2; // format 2 = STANDBY / PAUSED
            header[9] = mode;
            header[10] = zoom;
            header[11] = 0;
            let standby_txt = b"STANDBY";
            header[12..12 + standby_txt.len()].copy_from_slice(standby_txt);
            header[42..46].copy_from_slice(&0u32.to_be_bytes());

            if writer.write_all(&header).await.is_err() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(400)).await;
            continue;
        }

        let (target_w, target_h): (u32, u32) = if mode == 1 {
            (400, 240)
        } else {
            (240, 160)
        };

        let frame_opt = if pw_node > 0 {
            // Check if source version changed, node changed, dimensions changed, or zoom changed
            if current_src_ver != src_ver || current_pw_node != pw_node || current_target_w != target_w || current_target_h != target_h || current_zoom != zoom || pw_stdout.is_none() {
                if let Some(mut old_child) = pw_child.take() {
                    let _ = old_child.kill().await;
                }
                pw_stdout = None;
                prev_frame.clear();
                request_keyframe.store(true, Ordering::SeqCst);

                let opt_fd = state.active_pw_fd.lock().ok().and_then(|f| f.as_ref().map(|x| x.as_raw_fd()));

                let mut cmd = tokio::process::Command::new("gst-launch-1.0");
                let mut args = vec!["-q".to_string(), "pipewiresrc".to_string()];

                if let Some(fd_num) = opt_fd {
                    args.push(format!("fd={}", fd_num));
                    args.push(format!("path={}", pw_node));
                } else {
                    args.push(format!("target-object={}", pw_node));
                }

                args.push("!".to_string());
                args.push("videorate".to_string());
                args.push("!".to_string());
                args.push("video/x-raw,framerate=20/1".to_string());
                args.push("!".to_string());

                if zoom == 1 {
                    // 1:1 Native Pixel Center Crop
                    args.push("videobox".to_string());
                    args.push("autocrop=true".to_string());
                } else {
                    // Fit (Scale to target dimensions)
                    args.push("videoscale".to_string());
                }

                args.push("!".to_string());
                args.push(format!("video/x-raw,width={},height={}", target_w, target_h));
                args.push("!".to_string());
                args.push("videoconvert".to_string());
                args.push("!".to_string());
                args.push("video/x-raw,format=RGB16".to_string());
                args.push("!".to_string());
                args.push("fdsink".to_string());

                cmd.args(&args);
                cmd.stdout(std::process::Stdio::piped());
                cmd.stderr(std::process::Stdio::null());

                if let Ok(mut child) = cmd.spawn() {
                    pw_stdout = child.stdout.take();
                    pw_child = Some(child);
                    current_pw_node = pw_node;
                    current_src_ver = src_ver;
                    current_target_w = target_w;
                    current_target_h = target_h;
                    current_zoom = zoom;
                }
            }

            let frame_size = (target_w * target_h * 2) as usize;
            let mut payload = vec![0u8; frame_size];
            let mut read_ok = false;

            if let Some(ref mut out) = pw_stdout {
                if out.read_exact(&mut payload).await.is_ok() {
                    read_ok = true;
                }
            }

            if read_ok {
                let name = state.active_source_name.lock().map(|s| s.clone()).unwrap_or_else(|_| "OS Window".to_string());
                Some((target_w, target_h, 0, name, payload))
            } else {
                // If PipeWire stream ended, clean up and fall back
                if let Some(mut old_child) = pw_child.take() {
                    let _ = old_child.kill().await;
                }
                pw_stdout = None;
                current_pw_node = 0;
                None
            }
        } else {
            // Direct Monitor capture (Fast SIMD integer sampling, <0.5ms)
            if let Some(mut old_child) = pw_child.take() {
                let _ = old_child.kill().await;
                pw_stdout = None;
                current_pw_node = 0;
            }

            tokio::task::spawn_blocking(move || {
                capture_and_process(src_idx, target_w, target_h, format, zoom)
            }).await.unwrap_or(None)
        };

        if let Some((w, h, fmt_orig, src_name, raw_payload)) = frame_opt {
            let (out_fmt, send_payload, is_dirty) = if format == 3 {
                // 100% Event-driven keyframe (only when triggered by connect, resync, or source/mode change)
                let force_keyframe = request_keyframe.swap(false, Ordering::SeqCst) || (prev_frame.len() != (w * h * 2) as usize);
                let (dfmt, dpayload, dirty) = encode_delta_tiles(&raw_payload, &mut prev_frame, w, h, force_keyframe);
                (dfmt, dpayload, dirty)
            } else {
                (fmt_orig, raw_payload, true)
            };

            // If no change occurred (heartbeat mode)
            if !is_dirty && out_fmt == 5 {
                if last_heartbeat.elapsed() < Duration::from_millis(400) {
                    // Suppress excessive heartbeats when completely static
                    continue;
                }
                last_heartbeat = tokio::time::Instant::now();
            }

            let mut header = [0u8; 46];
            header[0..4].copy_from_slice(b"SCAM");
            header[4..6].copy_from_slice(&(w as u16).to_be_bytes());
            header[6..8].copy_from_slice(&(h as u16).to_be_bytes());
            header[8] = out_fmt;
            header[9] = mode;
            header[10] = zoom;
            header[11] = 0;

            let name_bytes = src_name.as_bytes();
            let copy_len = name_bytes.len().min(30);
            header[12..12 + copy_len].copy_from_slice(&name_bytes[..copy_len]);

            header[42..46].copy_from_slice(&(send_payload.len() as u32).to_be_bytes());

            if writer.write_all(&header).await.is_err() {
                break;
            }
            if !send_payload.is_empty() {
                if writer.write_all(&send_payload).await.is_err() {
                    break;
                }
            }
        } else if last_heartbeat.elapsed() >= Duration::from_millis(400) {
            // Keepalive / Standby heartbeat when waiting for pipeline source frames
            let mut header = [0u8; 46];
            header[0..4].copy_from_slice(b"SCAM");
            header[4..6].copy_from_slice(&(target_w as u16).to_be_bytes());
            header[6..8].copy_from_slice(&(target_h as u16).to_be_bytes());
            header[8] = 5; // Heartbeat
            header[9] = mode;
            header[10] = zoom;
            let standby_txt = b"STANDBY";
            header[12..12 + standby_txt.len()].copy_from_slice(standby_txt);
            header[42..46].copy_from_slice(&0u32.to_be_bytes());
            if writer.write_all(&header).await.is_err() {
                break;
            }
            last_heartbeat = tokio::time::Instant::now();
        }
    }

    if let Some(mut old_child) = pw_child.take() {
        let _ = old_child.kill().await;
    }
    state.is_active.store(false, Ordering::SeqCst);
}

const MORTON_LUT: [[usize; 8]; 8] = {
    let mut lut = [[0usize; 8]; 8];
    let mut y = 0;
    while y < 8 {
        let mut x = 0;
        while x < 8 {
            lut[y][x] = (x & 1) | ((y & 1) << 1) |
                        ((x & 2) << 1) | ((y & 2) << 2) |
                        ((x & 4) << 2) | ((y & 4) << 3);
            x += 1;
        }
        y += 1;
    }
    lut
};

/// Encodes dirty 8x8 tiles from current raw RGB565 frame against prev_frame
/// Returns (fmt, payload, is_dirty)
fn encode_delta_tiles(
    current_frame: &[u8],
    prev_frame: &mut Vec<u8>,
    w: u32,
    h: u32,
    force_keyframe: bool,
) -> (u8, Vec<u8>, bool) {
    let expected_len = (w * h * 2) as usize;
    if current_frame.len() != expected_len {
        return (0, current_frame.to_vec(), true);
    }

    if prev_frame.len() != expected_len {
        *prev_frame = vec![0u8; expected_len];
    }

    let tiles_w = (w as usize + 7) / 8;
    let tiles_h = (h as usize + 7) / 8;

    let mut dirty_tiles = Vec::new();

    for ty in 0..tiles_h {
        let y_start = ty * 8;
        for tx in 0..tiles_w {
            let x_start = tx * 8;
            let mut tile_dirty = force_keyframe;

            if !tile_dirty {
                // Check if any pixel row in this 8x8 tile changed
                for iy in 0..8 {
                    let y = y_start + iy;
                    if y >= h as usize { break; }
                    let row_start = (y * w as usize + x_start) * 2;
                    let row_end = row_start + (8.min(w as usize - x_start)) * 2;
                    if current_frame[row_start..row_end] != prev_frame[row_start..row_end] {
                        tile_dirty = true;
                        break;
                    }
                }
            }

            if tile_dirty {
                let mut tile_data = [0u8; 128];
                for iy in 0..8 {
                    let y = (y_start + iy).min(h as usize - 1);
                    let row_start = (y * w as usize + x_start) * 2;
                    for ix in 0..8 {
                        let px_offset = row_start + ix * 2;
                        let morton_idx = MORTON_LUT[iy][ix] * 2;
                        tile_data[morton_idx] = current_frame[px_offset];
                        tile_data[morton_idx + 1] = current_frame[px_offset + 1];
                    }
                }
                dirty_tiles.push((tx as u8, ty as u8, tile_data));
            }
        }
    }

    // Update prev_frame with current_frame
    prev_frame.copy_from_slice(current_frame);

    let dirty_count = dirty_tiles.len();

    if dirty_count == 0 {
        // No changes: format 5 = HEARTBEAT / NO_CHANGE
        (5, Vec::new(), false)
    } else {
        // Delta tiles: format 3 = TILE_DELTA_RAW
        let mut payload = Vec::with_capacity(2 + dirty_count * 130);
        payload.extend_from_slice(&(dirty_count as u16).to_be_bytes());
        for (tx, ty, data) in dirty_tiles {
            payload.push(tx);
            payload.push(ty);
            payload.extend_from_slice(&data);
        }
        (3, payload, true)
    }
}

fn capture_and_process(
    source_idx: usize,
    target_w: u32,
    target_h: u32,
    format: u8,
    zoom: u8,
) -> Option<(u32, u32, u8, String, Vec<u8>)> {
    let sources = list_sources();
    if sources.is_empty() { return None; }
    let active_src = sources.get(source_idx % sources.len())?;
    let source_name = active_src.name.clone();

    let monitors = Monitor::all().ok()?;
    let mon = monitors.get(source_idx % monitors.len().max(1)).unwrap_or(&monitors[0]);
    let raw_img = mon.capture_image().ok()?;

    let (src_w, src_h) = (raw_img.width(), raw_img.height());
    if src_w == 0 || src_h == 0 { return None; }

    let raw_slice = raw_img.as_raw();

    if format == 0 || format == 3 {
        let mut rgb565_buf = Vec::with_capacity((target_w * target_h * 2) as usize);

        if zoom == 1 {
            // 1:1 Native Pixel Center Crop
            let crop_w = target_w.min(src_w);
            let crop_h = target_h.min(src_h);
            let start_x = (src_w.saturating_sub(crop_w)) / 2;
            let start_y = (src_h.saturating_sub(crop_h)) / 2;

            for ty in 0..target_h {
                let sy = start_y + ty.min(crop_h - 1);
                let row_idx = (sy * src_w) as usize * 4;
                for tx in 0..target_w {
                    let sx = start_x + tx.min(crop_w - 1);
                    let px_idx = row_idx + (sx as usize * 4);
                    let r = (raw_slice[px_idx] >> 3) as u16;
                    let g = (raw_slice[px_idx + 1] >> 2) as u16;
                    let b = (raw_slice[px_idx + 2] >> 3) as u16;
                    let val = (r << 11) | (g << 5) | b;
                    rgb565_buf.extend_from_slice(&val.to_le_bytes());
                }
            }
        } else {
            // Fast single-pass decimation (zero float allocation, <0.5ms)
            let step_x = src_w as f32 / target_w as f32;
            let step_y = src_h as f32 / target_h as f32;

            for ty in 0..target_h {
                let sy = ((ty as f32 * step_y) as u32).min(src_h - 1);
                let row_idx = (sy * src_w) as usize * 4;
                for tx in 0..target_w {
                    let sx = ((tx as f32 * step_x) as u32).min(src_w - 1);
                    let px_idx = row_idx + (sx as usize * 4);
                    let r = (raw_slice[px_idx] >> 3) as u16;
                    let g = (raw_slice[px_idx + 1] >> 2) as u16;
                    let b = (raw_slice[px_idx + 2] >> 3) as u16;
                    let val = (r << 11) | (g << 5) | b;
                    rgb565_buf.extend_from_slice(&val.to_le_bytes());
                }
            }
        }

        Some((target_w, target_h, 0, source_name, rgb565_buf))
    } else {
        let processed_img = image::imageops::resize(&raw_img, target_w, target_h, FilterType::Nearest);
        let mut jpeg_buf = Vec::new();
        let mut encoder = image::codecs::jpeg::JpegEncoder::new_with_quality(&mut jpeg_buf, 65);
        let rgb_img = image::DynamicImage::ImageRgba8(processed_img).to_rgb8();
        if encoder.encode(rgb_img.as_raw(), target_w, target_h, image::ExtendedColorType::Rgb8).is_ok() {
            Some((target_w, target_h, 1, source_name, jpeg_buf))
        } else {
            None
        }
    }
}

#[cfg(all(test, feature = "cam"))]
mod tests {
    use super::*;

    #[test]
    fn test_delta_tile_encoding() {
        let (w, h) = (240u32, 160u32);
        let frame_size = (w * h * 2) as usize;
        let mut prev_frame = Vec::new();

        // 1. Initial keyframe: all 600 tiles should be dirty
        let initial_frame = vec![0xAAu8; frame_size];
        let (fmt, payload, is_dirty) = encode_delta_tiles(&initial_frame, &mut prev_frame, w, h, true);
        assert_eq!(fmt, 3);
        assert!(is_dirty);
        let dirty_count = u16::from_be_bytes([payload[0], payload[1]]);
        assert_eq!(dirty_count, 600);
        assert_eq!(payload.len(), 2 + 600 * 130);

        // 2. Unchanged frame: should yield fmt=5 (Heartbeat / No change) with 0 payload
        let (fmt, payload, is_dirty) = encode_delta_tiles(&initial_frame, &mut prev_frame, w, h, false);
        assert_eq!(fmt, 5);
        assert!(!is_dirty);
        assert_eq!(payload.len(), 0);

        // 3. Single tile change at (0, 0): should yield fmt=3 with dirty_count = 1
        let mut modified_frame = initial_frame.clone();
        modified_frame[0] = 0xBB; // Change first pixel
        let (fmt, payload, is_dirty) = encode_delta_tiles(&modified_frame, &mut prev_frame, w, h, false);
        assert_eq!(fmt, 3);
        assert!(is_dirty);
        let dirty_count = u16::from_be_bytes([payload[0], payload[1]]);
        assert_eq!(dirty_count, 1);
        assert_eq!(payload[2], 0); // tx = 0
        assert_eq!(payload[3], 0); // ty = 0
        assert_eq!(payload.len(), 2 + 130);

        // 4. Sustained unchanged frames: should consistently yield fmt=5 with 0 payload indefinitely
        for _ in 0..100 {
            let (fmt, payload, is_dirty) = encode_delta_tiles(&modified_frame, &mut prev_frame, w, h, false);
            assert_eq!(fmt, 5);
            assert!(!is_dirty);
            assert_eq!(payload.len(), 0);
        }
    }

    #[test]
    fn test_monitor_capture() {
        println!("Listing sources via list_sources()...");
        let srcs = list_sources();
        for s in &srcs {
            println!("Found source: id={}, name={}, {}x{}", s.id, s.name, s.width, s.height);
        }
        if let Ok(monitors) = Monitor::all() {
            println!("Found {} monitors via xcap", monitors.len());
            for (i, m) in monitors.iter().enumerate() {
                match m.capture_image() {
                    Ok(img) => println!("Monitor {} capture success: {}x{}", i, img.width(), img.height()),
                    Err(e) => println!("Monitor {} capture FAILED: {}", i, e),
                }
            }
        } else {
            println!("Monitor::all() returned Err");
        }
    }
}
}

pub use imp::*;

