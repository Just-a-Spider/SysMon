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
    use std::sync::atomic::{AtomicBool, Ordering};
    use windows::Win32::Graphics::Dxgi::{CreateDXGIFactory1, IDXGIFactory1};

    pub fn get_dxgi_gpu_name() -> Option<String> {
        unsafe {
            let factory: IDXGIFactory1 = CreateDXGIFactory1().ok()?;
            let mut adapter_idx = 0;
            let mut best_name = None;
            let mut best_vram: usize = 0;

            while let Ok(adapter) = factory.EnumAdapters1(adapter_idx) {
                adapter_idx += 1;
                if let Ok(desc) = adapter.GetDesc1() {
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
    }

    static WMI_BRIDGE_FAILED: AtomicBool = AtomicBool::new(false);

    /// Fallback non-invasive CPU temperature reader from LibreHardwareMonitor / OpenHardwareMonitor WMI provider
    pub fn get_wmi_cpu_temp() -> Option<f32> {
        if WMI_BRIDGE_FAILED.load(Ordering::Relaxed) {
            return None;
        }

        use windows::core::{BSTR, HRESULT, PCWSTR};
        use windows::Win32::System::Com::{
            CoCreateInstance, CoInitializeEx, CoSetProxyBlanket, CLSCTX_INPROC_SERVER,
            COINIT_MULTITHREADED, EOAC_NONE, RPC_C_AUTHN_LEVEL_CALL, RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE, RPC_C_IMP_LEVEL_IMPERSONATE,
        };
        use windows::Win32::System::Wmi::{
            IEnumWbemClassObject, IWbemLocator, IWbemServices, WbemLocator,
            WBEM_FLAG_FORWARD_ONLY, WBEM_FLAG_RETURN_IMMEDIATELY,
        };
        use windows::Win32::System::Variant::VARIANT;

        unsafe {
            let _ = CoInitializeEx(None, COINIT_MULTITHREADED);

            let locator: IWbemLocator = match CoCreateInstance(&WbemLocator, None, CLSCTX_INPROC_SERVER) {
                Ok(l) => l,
                Err(_) => {
                    WMI_BRIDGE_FAILED.store(true, Ordering::Relaxed);
                    return None;
                }
            };

            let namespaces = [
                windows::core::w!(r"ROOT\LibreHardwareMonitor"),
                windows::core::w!(r"ROOT\OpenHardwareMonitor"),
            ];

            let mut services: Option<IWbemServices> = None;
            for ns in namespaces {
                if let Ok(svc) = locator.ConnectServer(&BSTR::from(PCWSTR(ns.as_ptr())), None, None, None, 0, None, None) {
                    let _ = CoSetProxyBlanket(
                        &svc,
                        RPC_C_AUTHN_WINNT,
                        RPC_C_AUTHZ_NONE,
                        None,
                        RPC_C_AUTHN_LEVEL_CALL,
                        RPC_C_IMP_LEVEL_IMPERSONATE,
                        None,
                        EOAC_NONE,
                    );
                    services = Some(svc);
                    break;
                }
            }

            let services = match services {
                Some(s) => s,
                None => {
                    // Do not spam failed connection if no hardware monitor service is active
                    WMI_BRIDGE_FAILED.store(true, Ordering::Relaxed);
                    return None;
                }
            };

            let query = windows::core::w!("SELECT Name, Value, SensorType FROM Sensor WHERE SensorType = 'Temperature'");
            let lang = windows::core::w!("WQL");

            let enumerator: IEnumWbemClassObject = match services.ExecQuery(
                &BSTR::from(PCWSTR(lang.as_ptr())),
                &BSTR::from(PCWSTR(query.as_ptr())),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                None,
            ) {
                Ok(e) => e,
                Err(_) => return None,
            };

            let mut objects = [None; 1];
            let mut returned = 0u32;
            let mut cpu_temp: Option<f32> = None;

            while enumerator.Next(0, &mut objects, &mut returned).is_ok() && returned > 0 {
                if let Some(ref obj) = objects[0] {
                    let mut var_name = VARIANT::default();
                    let mut var_val = VARIANT::default();

                    let name_prop = windows::core::w!("Name");
                    let val_prop = windows::core::w!("Value");

                    if obj.Get(PCWSTR(name_prop.as_ptr()), 0, &mut var_name, None, None).is_ok()
                        && obj.Get(PCWSTR(val_prop.as_ptr()), 0, &mut var_val, None, None).is_ok()
                    {
                        let name_bstr = var_name.Anonymous.Anonymous.Anonymous.bstrVal;
                        if !name_bstr.is_null() {
                            let name_str = name_bstr.to_string().to_lowercase();
                            if name_str.contains("cpu") || name_str.contains("package") || name_str.contains("core") || name_str.contains("tctl") {
                                // VT_R4 is float (f32)
                                let val = var_val.Anonymous.Anonymous.Anonymous.fltVal;
                                if val > 0.0 && val < 120.0 {
                                    cpu_temp = Some(val);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            cpu_temp
        }
    }
}
