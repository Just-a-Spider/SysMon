use std::sync::Arc;
use tokio::sync::Mutex;
use crate::AppState;
use std::net::SocketAddr;
use crate::sys::{SysPoller, trigger_macro, kill_proc, trigger_notify, set_brightness_level, set_volume_level};
use std::time::Duration;
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::task::JoinSet;
use serde::Deserialize;
use std::collections::HashSet;

#[derive(Deserialize)]
struct ClientMessage {
    action: String,
    btn: Option<String>,
    pid: Option<u32>,
    pin: Option<String>,
    target: Option<String>,
    value: Option<u8>,
}

#[derive(Clone)]
struct ServerState {
    app_state: Arc<Mutex<AppState>>,
    sys_poller: Arc<Mutex<SysPoller>>,
    running_macros: Arc<Mutex<HashSet<String>>>,
    enigo: Arc<tokio::sync::Mutex<enigo::Enigo>>,
}

pub async fn run_server(state: Arc<Mutex<AppState>>, port: u16) {
    let sys_poller = Arc::new(Mutex::new(SysPoller::new()));
    let running_macros = Arc::new(Mutex::new(HashSet::new()));

    let mut tasks = JoinSet::new();

    // Weather task
    let weather_state = state.clone();
    tasks.spawn(async move {
        loop {
            if let Ok(res) = reqwest::get("https://wttr.in/?format=\"%t+%C\"").await {
                if let Ok(text) = res.text().await {
                    let text = text.trim().replace("\"", "").replace("°", "");
                    if let Ok(mut s) = weather_state.try_lock() {
                        s.weather_string = text;
                    }
                }
            }
            tokio::time::sleep(Duration::from_secs(600)).await;
        }
    });

    let srv_state = ServerState {
        app_state: state,
        sys_poller,
        running_macros,
        enigo: Arc::new(tokio::sync::Mutex::new(enigo::Enigo::new(&enigo::Settings::default()).unwrap())),
    };

    let poller_srv = srv_state.clone();
    tasks.spawn(async move {
        loop {
            let interval_ms = {
                let s = poller_srv.app_state.lock().await;
                s.config.update_interval_ms
            };
            tokio::time::sleep(Duration::from_millis(interval_ms)).await;
            
            let (weather, is_running, sort_by_ram, stream_port) = {
                let s = poller_srv.app_state.lock().await;
                (s.weather_string.clone(), s.is_server_running, s.config.sort_by_ram, s.active_stream_port)
            };
            
            if !is_running {
                break;
            }
            
            let data = {
                let mut poller = poller_srv.sys_poller.lock().await;
                poller.poll_data(weather, sort_by_ram, stream_port)
            };
            
            {
                let mut s = poller_srv.app_state.lock().await;
                s.latest_telemetry = Some(data);
            }
        }
    });

    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    println!("TCP Server listening on {}", addr);
    let listener = TcpListener::bind(addr).await.unwrap();

    loop {
        tokio::select! {
            accept_res = listener.accept() => {
                if let Ok((socket, _)) = accept_res {
                    let _ = socket.set_nodelay(true);
                    let srv = srv_state.clone();
                    tasks.spawn(async move {
                        handle_connection(socket, srv).await;
                    });
                }
            }
            Some(_) = tasks.join_next() => {
                // Task finished, ignore. We just wait so dead connection tasks are cleaned up
            }
        }
    }
}

async fn handle_connection(socket: TcpStream, srv: ServerState) {
    let (mut reader, mut writer) = socket.into_split();
    let mut conn_tasks = JoinSet::new();

    let (tx, mut rx) = tokio::sync::mpsc::channel::<String>(10);
    
    let interval_ms = {
        let s = srv.app_state.lock().await;
        s.config.update_interval_ms
    };

    let srv_clone = srv.clone();
    conn_tasks.spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_millis(interval_ms));
        loop {
            interval.tick().await;
            let (is_running, data_opt) = {
                let mut s = srv_clone.app_state.lock().await;
                s.last_ping = std::time::Instant::now();
                (s.is_server_running, s.latest_telemetry.clone())
            };
            
            if !is_running {
                break;
            }
            
            if let Some(data) = data_opt {
                if let Ok(json) = serde_json::to_string(&data) {
                    let msg = format!("{}\n", json);
                    if tx.send(msg).await.is_err() {
                        break;
                    }
                }
            }
        }
    });

    let srv_write = srv.clone();
    conn_tasks.spawn(async move {
        let macros_json = {
            let s = srv_write.app_state.lock().await;
            serde_json::to_string(&s.macros).unwrap_or_else(|_| "[]".to_string())
        };
        let init_msg = format!("{{\"type\":\"macros\",\"data\":{}}}\n", macros_json);
        let _ = writer.write_all(init_msg.as_bytes()).await;

        while let Some(msg) = rx.recv().await {
            if writer.write_all(msg.as_bytes()).await.is_err() {
                break;
            }
        }
    });

    let srv_read = srv.clone();
    conn_tasks.spawn(async move {
        let mut buf = [0; 1024];
        loop {
            match reader.read(&mut buf).await {
                Ok(0) => break,
                Ok(n) => {
                    let msg_str = String::from_utf8_lossy(&buf[..n]);
                    let stream = serde_json::Deserializer::from_str(&msg_str).into_iter::<ClientMessage>();
                    for result in stream {
                        if let Ok(msg) = result {
                            handle_message(&srv_read, msg).await;
                        }
                    }
                }
                Err(_) => break,
            }
        }
    });

    while let Some(_) = conn_tasks.join_next().await {
        break; // Stop other tasks if any of them finish (e.g., read socket closes)
    }
}

async fn handle_message(srv: &ServerState, msg: ClientMessage) {
    let pin = {
        let s = srv.app_state.lock().await;
        s.config.pin.clone()
    };
    
    if msg.pin.as_deref() != Some(&pin) {
        eprintln!("[SysMon Server] Auth failed: received PIN {:?}, expected PIN {:?}", msg.pin, pin);
        return; // Auth failed
    }

    println!("[SysMon Server] Action received: '{}', btn: {:?}, pid: {:?}", msg.action, msg.btn, msg.pid);

    match msg.action.as_str() {
        "button" => {
            if let Some(btn) = msg.btn {
                let macro_opt = {
                    let s = srv.app_state.lock().await;
                    s.macros.iter().find(|m| m.button.as_deref() == Some(&btn)).cloned()
                };
                
                if let Some(mac) = macro_opt {
                    println!("[SysMon Server] Triggering macro '{}' ({}): {:?}", mac.label, mac.kind, mac.value);
                    let mut rm = srv.running_macros.lock().await;
                    if rm.insert(btn.clone()) {
                        let rm_clone = srv.running_macros.clone();
                        let enigo_clone = srv.enigo.clone();
                        tokio::spawn(async move {
                            trigger_macro(&mac, enigo_clone).await;
                            let mut rm = rm_clone.lock().await;
                            rm.remove(&btn);
                        });
                    }
                } else {
                    println!("[SysMon Server] No macro found mapped to button '{}'", btn);
                }
            }
        },
        "media" => {
            if let Some(btn) = msg.btn {
                println!("[SysMon Server] Dispatching media command: '{}'", btn);
                crate::sys::media_command(&btn, Some(srv.enigo.clone())).await;
            }
        },
        "kill" => {
            if let Some(pid) = msg.pid {
                println!("[SysMon Server] Killing process PID: {}", pid);
                kill_proc(pid);
            }
        },
        "notify" => {
            trigger_notify();
        }
        "set_level" => {
            if let (Some(target), Some(value)) = (msg.target.as_deref(), msg.value) {
                match target {
                    "volume" => set_volume_level(value),
                    "brightness" => set_brightness_level(value),
                    _ => {}
                }
            }
        }
        _ => {}
    }
}
