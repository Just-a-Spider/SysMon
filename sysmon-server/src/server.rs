use std::sync::Arc;
use tokio::sync::Mutex;
use crate::AppState;
use std::net::SocketAddr;
use crate::sys::{SysPoller, trigger_macro, kill_proc, trigger_notify};
use std::time::Duration;
use tokio::net::TcpListener;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use serde::Deserialize;
use std::collections::HashSet;

#[derive(Deserialize)]
struct ClientMessage {
    action: String,
    btn: Option<String>,
    pid: Option<u32>,
    pin: Option<String>,
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

    // Weather task
    let weather_state = state.clone();
    tokio::spawn(async move {
        loop {
            if let Ok(res) = reqwest::get("https://wttr.in/?format=\"%t+%C\"").await {
                if let Ok(text) = res.text().await {
                    let text = text.trim().replace("\"", "");
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

    let addr = SocketAddr::from(([0, 0, 0, 0], port));
    println!("TCP Server listening on {}", addr);
    let listener = TcpListener::bind(addr).await.unwrap();

    loop {
        if let Ok((socket, _)) = listener.accept().await {
            let _ = socket.set_nodelay(true);
            let srv = srv_state.clone();
            tokio::spawn(async move {
                let (mut reader, mut writer) = socket.into_split();
                
                // Spawn writer task for telemetry
                let srv_clone = srv.clone();
                let interval_ms = {
                    let s = srv.app_state.lock().await;
                    s.config.update_interval_ms
                };
                let mut interval = tokio::time::interval(Duration::from_millis(interval_ms));
                
                let (tx, mut rx) = tokio::sync::mpsc::channel::<String>(10);
                
                tokio::spawn(async move {
                    loop {
                        interval.tick().await;
                        let (weather, is_running) = {
                            let mut s = srv_clone.app_state.lock().await;
                            s.last_ping = std::time::Instant::now();
                            (s.weather_string.clone(), s.is_server_running)
                        };
                        
                        if !is_running {
                            break;
                        }
                        let data = {
                            let mut poller = srv_clone.sys_poller.lock().await;
                            poller.poll_data(weather)
                        };
                        {
                            let mut s = srv_clone.app_state.lock().await;
                            s.latest_telemetry = Some(data.clone());
                        }
                        if let Ok(json) = serde_json::to_string(&data) {
                            let msg = format!("{}\n", json);
                            if tx.send(msg).await.is_err() {
                                break;
                            }
                        }
                    }
                });

                let srv_write = srv.clone();
                let write_task = tokio::spawn(async move {
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
                let read_task = tokio::spawn(async move {
                    let mut buf = [0; 1024];
                    loop {
                        match reader.read(&mut buf).await {
                            Ok(0) => break, // Connection closed
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

                // Watchdog to kill connection if server stops
                let srv_watch = srv.clone();
                tokio::spawn(async move {
                    loop {
                        tokio::time::sleep(Duration::from_millis(500)).await;
                        if !srv_watch.app_state.lock().await.is_server_running {
                            read_task.abort();
                            write_task.abort();
                            break;
                        }
                        if write_task.is_finished() || read_task.is_finished() {
                            read_task.abort();
                            write_task.abort();
                            break;
                        }
                    }
                });
            });
        }
    }
}

async fn handle_message(srv: &ServerState, msg: ClientMessage) {
    let pin = {
        let s = srv.app_state.lock().await;
        s.config.pin.clone()
    };
    
    if msg.pin.as_deref() != Some(&pin) {
        return; // Auth failed
    }

    match msg.action.as_str() {
        "button" => {
            if let Some(btn) = msg.btn {
                let macro_opt = {
                    let s = srv.app_state.lock().await;
                    s.macros.iter().find(|m| m.button.as_deref() == Some(&btn)).cloned()
                };
                
                if let Some(mac) = macro_opt {
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
                }
            }
        },
        "media" => {
            if let Some(btn) = msg.btn {
                crate::sys::media_command(&btn).await;
            }
        },
        "kill" => {
            if let Some(pid) = msg.pid {
                kill_proc(pid);
            }
        },
        "notify" => {
            trigger_notify();
        }
        _ => {}
    }
}
