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

                    for ns in namespaces {
                        if let Ok(svc) = locator.ConnectServer(&BSTR::from(ns), None, None, None, 0, None, None) {
                            state.services = Some(svc);
                            break;
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

                if let Some(temp) = best_cpu_temp {
                    if !SENSOR_PICKED_LOGGED.swap(true, Ordering::Relaxed) {
                        tracing::info!(
                            "WMI Hardware Monitor bridge selected CPU sensor: id='{}', name='{}', value={:.1} C",
                            best_id, best_name, temp
                        );
                    }
                } else if !NONE_MATCHED_LOGGED.swap(true, Ordering::Relaxed) {
                    tracing::info!("WMI Hardware Monitor query succeeded, but no matching CPU sensor found (/amdcpu/ or /intelcpu/)");
                }

                best_cpu_temp
            }
        })
    }
}
