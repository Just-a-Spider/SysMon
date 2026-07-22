use axum::{
    routing::{get, post},
    Router, Json, extract::{State, ConnectInfo}, response::Html, http::StatusCode
};
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::AppState;
use std::net::SocketAddr;

pub async fn run_web_server(state: Arc<Mutex<AppState>>, port: u16) {
    let app = Router::new()
        .route("/", get(serve_index))
        .route("/config", get(serve_config))
        .route("/api/config", get(get_config).post(update_config))
        .route("/api/macros", get(get_macros).post(update_macros))
        .route("/api/telemetry", get(get_telemetry))
        .with_state(state);

    let addr = format!("0.0.0.0:{}", port);
    if let Ok(listener) = tokio::net::TcpListener::bind(&addr).await {
        println!("Web config dashboard listening on {}", addr);
        let _ = axum::serve(listener, app.into_make_service_with_connect_info::<SocketAddr>()).await;
    } else {
        eprintln!("Failed to bind web config to {}", addr);
    }
}

async fn serve_index() -> Html<&'static str> {
    Html(include_str!("../../sysmon-web/index.html"))
}

async fn serve_config(ConnectInfo(addr): ConnectInfo<SocketAddr>) -> Result<Html<&'static str>, StatusCode> {
    if addr.ip().is_loopback() {
        Ok(Html(include_str!("../../sysmon-web/config.html")))
    } else {
        Err(StatusCode::FORBIDDEN)
    }
}

async fn get_config(ConnectInfo(addr): ConnectInfo<SocketAddr>, State(state): State<Arc<Mutex<AppState>>>) -> Result<Json<crate::AppConfig>, StatusCode> {
    if !addr.ip().is_loopback() { return Err(StatusCode::FORBIDDEN); }
    let s = state.lock().await;
    Ok(Json(s.config.clone()))
}

async fn update_config(ConnectInfo(addr): ConnectInfo<SocketAddr>, State(state): State<Arc<Mutex<AppState>>>, Json(payload): Json<crate::AppConfig>) -> Result<Json<bool>, StatusCode> {
    if !addr.ip().is_loopback() { return Err(StatusCode::FORBIDDEN); }
    let mut s = state.lock().await;
    s.config = payload;
    s.should_restart_server = true;
    s.save_config();
    Ok(Json(true))
}

async fn get_macros(ConnectInfo(addr): ConnectInfo<SocketAddr>, State(state): State<Arc<Mutex<AppState>>>) -> Result<Json<Vec<crate::sys::MacroDef>>, StatusCode> {
    if !addr.ip().is_loopback() { return Err(StatusCode::FORBIDDEN); }
    let s = state.lock().await;
    Ok(Json(s.macros.clone()))
}

async fn update_macros(ConnectInfo(addr): ConnectInfo<SocketAddr>, State(state): State<Arc<Mutex<AppState>>>, Json(payload): Json<Vec<crate::sys::MacroDef>>) -> Result<Json<bool>, StatusCode> {
    if !addr.ip().is_loopback() { return Err(StatusCode::FORBIDDEN); }
    let mut s = state.lock().await;
    s.macros = payload;
    s.save_macros();
    Ok(Json(true))
}

async fn get_telemetry(State(state): State<Arc<Mutex<AppState>>>) -> Json<Option<crate::sys::TelemetryData>> {
    let s = state.lock().await;
    Json(s.latest_telemetry.clone())
}
