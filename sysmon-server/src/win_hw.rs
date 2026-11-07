use std::sync::Mutex;

#[allow(dead_code)]
static LHM_HTTP_CPU_TEMP: Mutex<Option<f32>> = Mutex::new(None);

#[allow(dead_code)]
pub fn get_lhm_http_cpu_temp() -> Option<f32> {
    LHM_HTTP_CPU_TEMP.lock().ok().and_then(|t| *t)
}

#[allow(dead_code)]
pub fn update_lhm_http_cpu_temp(temp: Option<f32>) {
    if let Ok(mut lock) = LHM_HTTP_CPU_TEMP.lock() {
        *lock = temp;
    }
}

#[allow(dead_code)]
pub fn parse_lhm_json(root: &serde_json::Value) -> Option<f32> {
    fn parse_val(s: &str) -> Option<f32> {
        let clean: String = s
            .chars()
            .map(|c| if c == ',' { '.' } else { c })
            .filter(|c| c.is_ascii_digit() || *c == '.' || *c == '-')
            .collect();
        clean.parse::<f32>().ok()
    }

    struct Candidate {
        score: i32,
        temp: f32,
    }

    fn collect_candidates(node: &serde_json::Value, in_cpu: bool, candidates: &mut Vec<Candidate>) {
        let text = node.get("Text").and_then(|v| v.as_str()).unwrap_or("");
        let tl = text.to_lowercase();
        let img = node.get("ImageURL").and_then(|v| v.as_str()).unwrap_or("").to_lowercase();

        // Computer container node should not be treated as a CPU hardware device
        let is_computer = img.ends_with("computer.png") || tl == "sensor";

        // Strong CPU markers (Ryzen, Core, legacy families, and APUs)
        let has_strong_cpu_marker = !is_computer && (
            tl.contains("cpu")
                || tl.contains("ryzen")
                || tl.contains("threadripper")
                || tl.contains("epyc")
                || tl.contains("xeon")
                || tl.contains("core i")
                || tl.contains("core ultra")
                || tl.contains("core 2")
                || tl.contains("core(tm)")
                || tl.contains("pentium")
                || tl.contains("celeron")
                || tl.contains("atom")
                || tl.contains("athlon")
                || tl.contains("sempron")
                || tl.contains("opteron")
                || tl.contains("compute cores")
                || tl.contains("processor")
                || img.ends_with("cpu.png")
        );

        // GPU markers (vetos CPU branch even if in_cpu is true)
        let is_gpu = tl.contains("gpu")
            || tl.contains("intel arc")
            || tl.contains("geforce")
            || tl.contains("rtx")
            || tl.contains("gtx")
            || img.ends_with("gpu.png")
            || img.ends_with("ati.png")
            || img.ends_with("nvidia.png")
            || ((tl.contains("radeon") || tl.contains("graphics")) && !has_strong_cpu_marker);

        let cpu_branch = if is_gpu {
            false
        } else if has_strong_cpu_marker {
            true
        } else {
            in_cpu
        };

        if cpu_branch {
            let is_distance = tl.contains("distance") || tl.contains("tjmax");
            let is_ccd = tl.contains("ccd");

            if !is_distance && !is_ccd {
                if let Some(val_str) = node.get("Value").and_then(|v| v.as_str()) {
                    let vl = val_str.to_lowercase();
                    let is_celsius = vl.contains("°c") || vl.contains("° c") || vl.ends_with(" c");
                    let is_fahrenheit = vl.contains("°f") || vl.contains("° f") || vl.ends_with(" f");

                    if is_celsius || is_fahrenheit {
                        if let Some(raw) = parse_val(val_str) {
                            let temp = if is_fahrenheit {
                                (raw - 32.0) * 5.0 / 9.0
                            } else {
                                raw
                            };
                            if temp > 0.0 && temp <= 125.0 {
                                // Priority score matching WMI:
                                // 4: Tctl (AMD preferred package control)
                                // 3: CPU Package (Intel / generic package)
                                // 2: Tdie
                                // 1: Core temp / Core #1 / Core Average
                                // 0: other CPU sensor
                                let score = if tl.contains("tctl") {
                                    4
                                } else if tl.contains("package") {
                                    3
                                } else if tl.contains("tdie") {
                                    2
                                } else if tl.contains("core") {
                                    1
                                } else {
                                    0
                                };
                                candidates.push(Candidate { score, temp });
                            }
                        }
                    }
                }
            }
        }

        if let Some(children) = node.get("Children").and_then(|c| c.as_array()) {
            for child in children {
                collect_candidates(child, cpu_branch, candidates);
            }
        }
    }

    let mut candidates = Vec::new();
    collect_candidates(root, false, &mut candidates);

    // Pick highest score, tiebreak with highest temperature
    candidates.sort_by(|a, b| {
        b.score.cmp(&a.score).then_with(|| b.temp.partial_cmp(&a.temp).unwrap_or(std::cmp::Ordering::Equal))
    });

    candidates.first().map(|c| c.temp)
}

#[cfg(target_os = "windows")]
pub use imp::*;

#[cfg(not(target_os = "windows"))]
#[allow(dead_code)]
pub fn get_dxgi_gpu_name() -> Option<String> {
    None
}

#[cfg(not(target_os = "windows"))]
#[allow(dead_code)]
pub fn get_wmi_cpu_temp() -> Option<f32> {
    None
}

#[cfg(target_os = "windows")]
mod imp {
    use std::cell::RefCell;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::OnceLock;
    use std::time::{Duration, Instant};
    use windows::Win32::Graphics::Dxgi::{CreateDXGIFactory1, IDXGIFactory1, DXGI_ADAPTER_DESC1};
    use windows::core::{BSTR, PCWSTR};
    use windows::Win32::System::Com::{
        CoCreateInstance, CoInitializeEx, CLSCTX_INPROC_SERVER,
        COINIT_MULTITHREADED,
    };
    use windows::Win32::System::Wmi::{
        IEnumWbemClassObject, IWbemLocator, IWbemServices, WbemLocator,
        WBEM_FLAG_FORWARD_ONLY, WBEM_FLAG_RETURN_IMMEDIATELY,
    };
    use windows::Win32::System::Variant::{VariantClear, VARIANT, VT_BSTR, VT_R4, VT_R8};

    static DXGI_GPU_NAME: OnceLock<Option<String>> = OnceLock::new();

    pub fn get_dxgi_gpu_name() -> Option<String> {
        DXGI_GPU_NAME.get_or_init(|| {
            unsafe {
                let factory: IDXGIFactory1 = CreateDXGIFactory1().ok()?;
                let mut adapter_idx = 0;
                let mut best_name = None;
                let mut best_vram: usize = 0;

                while let Ok(adapter) = factory.EnumAdapters1(adapter_idx) {
                    adapter_idx += 1;
                    let mut desc = DXGI_ADAPTER_DESC1::default();
                    if adapter.GetDesc1(&mut desc).is_ok() {
                        // Skip software renderers (DXGI_ADAPTER_FLAG_SOFTWARE = 2)
                        if (desc.Flags & 2) != 0 {
                            continue;
                        }

                        let len = desc.Description.iter().position(|&c| c == 0).unwrap_or(desc.Description.len());
                        let raw_name = String::from_utf16_lossy(&desc.Description[..len]).trim().to_string();

                        if desc.DedicatedVideoMemory >= best_vram && !raw_name.is_empty() {
                            best_vram = desc.DedicatedVideoMemory;
                            best_name = Some(raw_name);
                        }
                    }
                }

                best_name.map(|name| {
                    name.replace("NVIDIA GeForce ", "")
                        .replace("GeForce ", "")
                        .replace("AMD Radeon ", "Radeon ")
                        .replace("with Max-Q Design", "Max-Q")
                        .replace("Laptop GPU", "")
                        .trim()
                        .to_string()
                })
            }
        }).clone()
    }

    struct ThreadWmiState {
        services: Option<IWbemServices>,
        last_attempt: Option<Instant>,
        com_initialized: bool,
    }

    thread_local! {
        static THREAD_WMI: RefCell<ThreadWmiState> = RefCell::new(ThreadWmiState {
            services: None,
            last_attempt: None,
            com_initialized: false,
        });
    }

    const RETRY_INTERVAL: Duration = Duration::from_secs(30);

    /// Fallback non-invasive CPU temperature reader from LibreHardwareMonitor / OpenHardwareMonitor WMI provider
    pub fn get_wmi_cpu_temp() -> Option<f32> {
        THREAD_WMI.with(|cell| {
            let mut state = cell.borrow_mut();

            unsafe {
                if !state.com_initialized {
                    let _ = CoInitializeEx(None, COINIT_MULTITHREADED);
                    state.com_initialized = true;
                }

                if state.services.is_none() {
                    if let Some(last) = state.last_attempt {
                        if last.elapsed() < RETRY_INTERVAL {
                            return None;
                        }
                    }
                    state.last_attempt = Some(Instant::now());

                    let locator: IWbemLocator = match CoCreateInstance(&WbemLocator, None, CLSCTX_INPROC_SERVER) {
                        Ok(l) => l,
                        Err(_) => return None,
                    };

                    let namespaces = [
                        "ROOT\\LibreHardwareMonitor",
                        "ROOT\\OpenHardwareMonitor",
                    ];

                    let mut connected_ns = None;
                    for ns in namespaces {
                        if let Ok(svc) = locator.ConnectServer(&BSTR::from(ns), None, None, None, 0, None, None) {
                            connected_ns = Some(ns);
                            state.services = Some(svc);
                            break;
                        }
                    }

                    static CONNECT_FAIL_LOGGED: AtomicBool = AtomicBool::new(false);
                    static CONNECT_SUCCESS_LOGGED: AtomicBool = AtomicBool::new(false);

                    if state.services.is_none() {
                        if !CONNECT_FAIL_LOGGED.swap(true, Ordering::Relaxed) {
                            tracing::info!("WMI Hardware Monitor connect failed: neither ROOT\\LibreHardwareMonitor nor ROOT\\OpenHardwareMonitor is currently available");
                        }
                        return None;
                    } else if let Some(ns) = connected_ns {
                        if !CONNECT_SUCCESS_LOGGED.swap(true, Ordering::Relaxed) {
                            tracing::info!("WMI Hardware Monitor connected to namespace '{}'", ns);
                        }
                    }
                }

                let services = match state.services.as_ref() {
                    Some(s) => s,
                    None => return None,
                };

                let enumerator: IEnumWbemClassObject = match services.ExecQuery(
                    &BSTR::from("WQL"),
                    &BSTR::from("SELECT Identifier, Name, Value FROM Sensor WHERE SensorType = 'Temperature'"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    None,
                ) {
                    Ok(e) => e,
                    Err(_) => {
                        state.services = None;
                        state.last_attempt = Some(Instant::now());
                        return None;
                    }
                };

                let mut objects = [None; 1];
                let mut returned = 0u32;
                let mut best_score = -1;
                let mut best_cpu_temp: Option<f32> = None;
                let mut best_id = String::new();
                let mut best_name = String::new();

                let id_prop = windows::core::w!("Identifier");
                let name_prop = windows::core::w!("Name");
                let val_prop = windows::core::w!("Value");

                let mut total_sensors_seen = 0;
                loop {
                    let hr = enumerator.Next(0, &mut objects, &mut returned);
                    if hr.is_err() {
                        // WMI connection lost or provider terminated
                        state.services = None;
                        state.last_attempt = Some(Instant::now());
                        break;
                    }
                    if returned == 0 {
                        break;
                    }
                    total_sensors_seen += 1;

                    if let Some(ref obj) = objects[0] {
                        let mut var_id = VARIANT::default();
                        let mut var_name = VARIANT::default();
                        let mut var_val = VARIANT::default();

                        let got_id = obj.Get(PCWSTR(id_prop.as_ptr()), 0, &mut var_id, None, None).is_ok();
                        let got_name = obj.Get(PCWSTR(name_prop.as_ptr()), 0, &mut var_name, None, None).is_ok();
                        let got_val = obj.Get(PCWSTR(val_prop.as_ptr()), 0, &mut var_val, None, None).is_ok();

                        if got_id && got_name && got_val {
                            let id_str = if var_id.Anonymous.Anonymous.vt == VT_BSTR {
                                var_id.Anonymous.Anonymous.Anonymous.bstrVal.to_string().to_lowercase()
                            } else {
                                String::new()
                            };

                            let name_str = if var_name.Anonymous.Anonymous.vt == VT_BSTR {
                                var_name.Anonymous.Anonymous.Anonymous.bstrVal.to_string().to_lowercase()
                            } else {
                                String::new()
                            };

                            let val = if var_val.Anonymous.Anonymous.vt == VT_R4 {
                                Some(var_val.Anonymous.Anonymous.Anonymous.fltVal)
                            } else if var_val.Anonymous.Anonymous.vt == VT_R8 {
                                Some(var_val.Anonymous.Anonymous.Anonymous.dblVal as f32)
                            } else {
                                None
                            };

                            // Ensure sensor belongs to a CPU (not GPU Core or motherboard sensor)
                            let is_cpu = id_str.starts_with("/amdcpu/") || id_str.starts_with("/intelcpu/");
                            // Skip distance to TjMax and individual CCD dies (CCD1, CCD2)
                            let is_distance = name_str.contains("distance");
                            let is_ccd = name_str.contains("ccd");

                            if is_cpu && !is_distance && !is_ccd {
                                if let Some(temp) = val {
                                    if temp > 0.0 && temp < 125.0 {
                                        // Priority score:
                                        // 4: Tctl (AMD preferred package control)
                                        // 3: CPU Package (Intel / generic package)
                                        // 2: Tdie
                                        // 1: Core temp
                                        // 0: other CPU sensor
                                        let score = if name_str.contains("tctl") {
                                            4
                                        } else if name_str.contains("package") {
                                            3
                                        } else if name_str.contains("tdie") {
                                            2
                                        } else if name_str.contains("core") {
                                            1
                                        } else {
                                            0
                                        };

                                        if score > best_score {
                                            best_score = score;
                                            best_cpu_temp = Some(temp);
                                            best_id = id_str.clone();
                                            best_name = name_str.clone();
                                        } else if score == best_score && score > 0 {
                                            if temp > best_cpu_temp.unwrap_or(0.0) {
                                                best_cpu_temp = Some(temp);
                                                best_id = id_str.clone();
                                                best_name = name_str.clone();
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        let _ = VariantClear(&mut var_id);
                        let _ = VariantClear(&mut var_name);
                        let _ = VariantClear(&mut var_val);
                    }
                }

                static SENSOR_PICKED_LOGGED: AtomicBool = AtomicBool::new(false);
                static NONE_MATCHED_LOGGED: AtomicBool = AtomicBool::new(false);
                static ZERO_SENSORS_LOGGED: AtomicBool = AtomicBool::new(false);

                if let Some(temp) = best_cpu_temp {
                    if !SENSOR_PICKED_LOGGED.swap(true, Ordering::Relaxed) {
                        tracing::info!(
                            "WMI Hardware Monitor bridge selected CPU sensor: id='{}', name='{}', value={:.1} C",
                            best_id, best_name, temp
                        );
                    }
                } else if total_sensors_seen == 0 {
                    if !ZERO_SENSORS_LOGGED.swap(true, Ordering::Relaxed) {
                        tracing::info!("WMI Hardware Monitor query succeeded, but returned 0 temperature sensors");
                    }
                } else if !NONE_MATCHED_LOGGED.swap(true, Ordering::Relaxed) {
                    tracing::info!(
                        "WMI Hardware Monitor found {} temperature sensor(s), but none matched CPU criteria (/amdcpu/ or /intelcpu/)",
                        total_sensors_seen
                    );
                }

                best_cpu_temp
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_lhm_json_ryzen() {
        let json = serde_json::json!({
            "id": 0, "Text": "Sensor",
            "Children": [
                {
                    "id": 1, "Text": "DESKTOP-ABC",
                    "Children": [
                        {
                            "id": 2, "Text": "AMD Ryzen 7 8700F",
                            "Children": [
                                {
                                    "id": 3, "Text": "Temperatures",
                                    "Children": [
                                        {"id": 4, "Text": "Core (Tctl/Tdie)", "Value": "47.5 °C"},
                                        {"id": 5, "Text": "CPU CCD #1", "Value": "43.1 °C"}
                                    ]
                                }
                            ]
                        },
                        {
                            "id": 6, "Text": "AMD Radeon RX 7800 XT",
                            "Children": [
                                {
                                    "id": 7, "Text": "Temperatures",
                                    "Children": [
                                        {"id": 8, "Text": "GPU Core", "Value": "42.0 °C"}
                                    ]
                                }
                            ]
                        }
                    ]
                }
            ]
        });

        assert_eq!(parse_lhm_json(&json), Some(47.5));
    }

    #[test]
    fn test_parse_lhm_json_fahrenheit() {
        let json_tree = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "AMD Ryzen 7 7800X3D",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                // 119.3 °F = 48.5 °C
                                {"Text": "Core (Tctl/Tdie)", "Value": "119.3 °F"}
                            ]
                        }
                    ]
                }
            ]
        });

        let temp = parse_lhm_json(&json_tree).unwrap();
        assert!((temp - 48.5).abs() < 0.1);
    }

    #[test]
    fn test_parse_lhm_json_apu_ryzen_with_radeon_graphics() {
        let json_tree = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "AMD Ryzen 7 5700G with Radeon Graphics",
                    "ImageURL": "images_icon/cpu.png",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "Core (Tctl/Tdie)", "Value": "49.0 °C"},
                                {"Text": "CPU Core #1", "Value": "44.5 °C"}
                            ]
                        }
                    ]
                }
            ]
        });

        // Must recognize CPU despite "with Radeon Graphics" in name
        assert_eq!(parse_lhm_json(&json_tree), Some(49.0));
    }

    #[test]
    fn test_parse_lhm_json_computer_name_ryzen_veto_gpu() {
        let json_tree = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "RYZEN-DESKTOP",
                    "ImageURL": "images_icon/computer.png",
                    "Children": [
                        {
                            "Text": "AMD Radeon RX 7800 XT",
                            "ImageURL": "images_icon/ati.png",
                            "Children": [
                                {
                                    "Text": "Temperatures",
                                    "Children": [
                                        {"Text": "GPU Core", "Value": "48.0 °C"}
                                    ]
                                }
                            ]
                        }
                    ]
                }
            ]
        });

        // Even though computer name contains 'RYZEN', GPU sensors must be vetoed and not returned as CPU
        assert_eq!(parse_lhm_json(&json_tree), None);
    }

    #[test]
    fn test_parse_lhm_json_budget_and_legacy_cpus() {
        let json_celeron = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "Intel Celeron G5905",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "CPU Package", "Value": "38.5 °C"}
                            ]
                        }
                    ]
                }
            ]
        });
        assert_eq!(parse_lhm_json(&json_celeron), Some(38.5));

        let json_athlon = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "AMD Athlon 3000G with Radeon Vega Graphics",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "Core (Tctl/Tdie)", "Value": "41.2 °C"}
                            ]
                        }
                    ]
                }
            ]
        });
        assert_eq!(parse_lhm_json(&json_athlon), Some(41.2));

        let json_ssd = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "Intel SSD 660p 1TB",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "Temperature", "Value": "33.0 °C"}
                            ]
                        }
                    ]
                }
            ]
        });
        // Intel SSD must not be parsed as CPU
        assert_eq!(parse_lhm_json(&json_ssd), None);
    }

    #[test]
    fn test_parse_lhm_json_scoring_and_arc_filter() {
        let json_tree = serde_json::json!({
            "Text": "Sensor",
            "Children": [
                {
                    "Text": "Intel Arc A770",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "GPU Core", "Value": "60.0 °C"}
                            ]
                        }
                    ]
                },
                {
                    "Text": "Intel Core i9-13900K",
                    "Children": [
                        {
                            "Text": "Temperatures",
                            "Children": [
                                {"Text": "CPU Core #1 Distance to TjMax", "Value": "65.0 °C"},
                                {"Text": "CPU Core #1", "Value": "48.0 °C"},
                                {"Text": "CPU Package", "Value": "54.5 °C"}
                            ]
                        }
                    ]
                }
            ]
        });

        // "CPU Package" (score 3) should beat "CPU Core #1" (score 1) and ignore Distance to TjMax and Intel Arc
        assert_eq!(parse_lhm_json(&json_tree), Some(54.5));
    }

    #[test]
    fn test_parse_lhm_json_comma_and_no_cpu() {
        let json_comma = serde_json::json!({
            "id": 0, "Text": "Sensor",
            "Children": [
                {
                    "id": 1, "Text": "Intel Core i7-13700K",
                    "Children": [
                        {
                            "id": 2, "Text": "Temperatures",
                            "Children": [
                                {"id": 3, "Text": "CPU Package", "Value": "52,3 °C"}
                            ]
                        }
                    ]
                }
            ]
        });
        assert_eq!(parse_lhm_json(&json_comma), Some(52.3));

        let json_gpu_only = serde_json::json!({
            "id": 0, "Text": "Sensor",
            "Children": [
                {
                    "id": 1, "Text": "NVIDIA GeForce RTX 4070",
                    "Children": [
                        {
                            "id": 2, "Text": "Temperatures",
                            "Children": [
                                {"id": 3, "Text": "GPU Core", "Value": "55.0 °C"}
                            ]
                        }
                    ]
                }
            ]
        });
        assert_eq!(parse_lhm_json(&json_gpu_only), None);
    }
}
