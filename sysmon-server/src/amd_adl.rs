#[derive(Debug, Clone)]
pub struct AmdGpuData {
    pub name: String,
    pub temp: f32,
    pub fan_rpm: i32,
}

#[cfg(target_os = "windows")]
pub use imp::get_amd_gpu_data;

#[cfg(not(target_os = "windows"))]
#[allow(dead_code)]
pub fn get_amd_gpu_data() -> Option<AmdGpuData> {
    None
}

#[cfg(target_os = "windows")]
mod imp {
    use super::AmdGpuData;
    use std::ffi::{c_char, c_void};
    use std::sync::Mutex;
    use windows::core::PCWSTR;
    use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};

    const ADL_OK: i32 = 0;
    const ADL_VENDOR_AMD: i32 = 0x1002;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct AdapterInfo {
        pub size: i32,
        pub adapter_index: i32,
        pub udid: [c_char; 256],
        pub bus_number: i32,
        pub device_number: i32,
        pub function_number: i32,
        pub vendor_id: i32,
        pub adapter_name: [c_char; 256],
        pub display_name: [c_char; 256],
        pub present: i32,
        pub exist: i32,
        pub driver_path: [c_char; 256],
        pub driver_path_ext: [c_char; 256],
        pub pnp_string: [c_char; 256],
        pub os_display_index: i32,
    }

    #[repr(C)]
    pub struct ADLTemperature {
        pub size: i32,
        pub temperature: i32,
    }

    #[repr(C)]
    pub struct ADLFanSpeedValue {
        pub size: i32,
        pub speed_type: i32,
        pub fan_speed: i32,
        pub flags: i32,
    }

    type ADLContextHandle = *mut c_void;
    type FnAdlMainMemoryAlloc = unsafe extern "system" fn(i32) -> *mut c_void;
    type FnAdlMainControlCreate = unsafe extern "system" fn(Option<FnAdlMainMemoryAlloc>, i32) -> i32;
    type FnAdl2MainControlCreate = unsafe extern "system" fn(Option<FnAdlMainMemoryAlloc>, i32, *mut ADLContextHandle) -> i32;
    type FnAdlAdapterNumberOfAdaptersGet = unsafe extern "system" fn(*mut i32) -> i32;
    type FnAdlAdapterAdapterInfoGet = unsafe extern "system" fn(*mut AdapterInfo, i32) -> i32;
    type FnAdlOverdrive6TemperatureGet = unsafe extern "system" fn(i32, *mut i32) -> i32;
    type FnAdlOverdrive5TemperatureGet = unsafe extern "system" fn(i32, i32, *mut ADLTemperature) -> i32;
    type FnAdl2OverdriveNTemperatureGet = unsafe extern "system" fn(ADLContextHandle, i32, i32, *mut i32) -> i32;
    type FnAdlOverdrive6FanSpeedGet = unsafe extern "system" fn(i32, *mut ADLFanSpeedValue) -> i32;
    type FnAdlOverdrive5FanSpeedGet = unsafe extern "system" fn(i32, i32, *mut ADLFanSpeedValue) -> i32;

    unsafe extern "system" fn adl_alloc(size: i32) -> *mut c_void {
        if size <= 0 {
            return std::ptr::null_mut();
        }
        let layout = match std::alloc::Layout::from_size_align(size as usize, 8) {
            Ok(l) => l,
            Err(_) => return std::ptr::null_mut(),
        };
        std::alloc::alloc_zeroed(layout) as *mut c_void
    }

    struct AdlState {
        context_handle: Option<ADLContextHandle>,
        active_adapter_index: i32,
        adapter_name: String,
        fn_temp6: Option<FnAdlOverdrive6TemperatureGet>,
        fn_temp5: Option<FnAdlOverdrive5TemperatureGet>,
        fn_temp_n: Option<FnAdl2OverdriveNTemperatureGet>,
        fn_fan6: Option<FnAdlOverdrive6FanSpeedGet>,
        fn_fan5: Option<FnAdlOverdrive5FanSpeedGet>,
    }

    static ADL_STATE: Mutex<Option<AdlState>> = Mutex::new(None);
    static INIT_ATTEMPTED: Mutex<bool> = Mutex::new(false);

    fn init_adl() -> Option<AdlState> {
        unsafe {
            // Try 64-bit library first, then 32-bit fallback
            let mut h_module = LoadLibraryW(windows::core::w!("atiadlxx.dll")).ok();
            if h_module.is_none() {
                h_module = LoadLibraryW(windows::core::w!("atiadlxy.dll")).ok();
            }
            let h = h_module?;

            let get_sym = |name: &[u8]| -> Option<*const ()> {
                let proc = GetProcAddress(h, windows::core::PCSTR(name.as_ptr()));
                proc.map(|p| p as *const ())
            };

            let fn_adl2_create: Option<FnAdl2MainControlCreate> = get_sym(b"ADL2_Main_Control_Create\0").map(|p| std::mem::transmute(p));
            let fn_adl1_create: Option<FnAdlMainControlCreate> = get_sym(b"ADL_Main_Control_Create\0").map(|p| std::mem::transmute(p));
            let fn_num_adapters: FnAdlAdapterNumberOfAdaptersGet = std::mem::transmute(get_sym(b"ADL_Adapter_NumberOfAdapters_Get\0")?);
            let fn_adapter_info: FnAdlAdapterAdapterInfoGet = std::mem::transmute(get_sym(b"ADL_Adapter_AdapterInfo_Get\0")?);

            let fn_temp6: Option<FnAdlOverdrive6TemperatureGet> = get_sym(b"ADL_Overdrive6_Temperature_Get\0").map(|p| std::mem::transmute(p));
            let fn_temp5: Option<FnAdlOverdrive5TemperatureGet> = get_sym(b"ADL_Overdrive5_Temperature_Get\0").map(|p| std::mem::transmute(p));
            let fn_temp_n: Option<FnAdl2OverdriveNTemperatureGet> = get_sym(b"ADL2_OverdriveN_Temperature_Get\0").map(|p| std::mem::transmute(p));

            let fn_fan6: Option<FnAdlOverdrive6FanSpeedGet> = get_sym(b"ADL_Overdrive6_FanSpeed_Get\0").map(|p| std::mem::transmute(p));
            let fn_fan5: Option<FnAdlOverdrive5FanSpeedGet> = get_sym(b"ADL_Overdrive5_FanSpeed_Get\0").map(|p| std::mem::transmute(p));

            let mut context_handle = None;
            let mut init_success = false;

            if let Some(create2) = fn_adl2_create {
                let mut ctx: ADLContextHandle = std::ptr::null_mut();
                if create2(Some(adl_alloc), 1, &mut ctx) == ADL_OK && !ctx.is_null() {
                    context_handle = Some(ctx);
                    init_success = true;
                }
            }

            if !init_success {
                if let Some(create1) = fn_adl1_create {
                    if create1(Some(adl_alloc), 1) == ADL_OK {
                        init_success = true;
                    }
                }
            }

            if !init_success {
                tracing::warn!("ADL_Main_Control_Create returned non-zero status");
                return None;
            }

            let mut num_adapters = 0;
            if fn_num_adapters(&mut num_adapters) != ADL_OK || num_adapters <= 0 {
                tracing::warn!("ADL_Adapter_NumberOfAdapters_Get returned no adapters");
                return None;
            }

            let mut adapters = vec![std::mem::zeroed::<AdapterInfo>(); num_adapters as usize];
            for a in &mut adapters {
                a.size = std::mem::size_of::<AdapterInfo>() as i32;
            }

            let total_size = (num_adapters as usize * std::mem::size_of::<AdapterInfo>()) as i32;
            if fn_adapter_info(adapters.as_mut_ptr(), total_size) != ADL_OK {
                tracing::warn!("ADL_Adapter_AdapterInfo_Get failed");
                return None;
            }

            let mut selected_adapter = None;
            for adp in &adapters {
                if adp.exist != 0 && adp.present != 0 {
                    let name_bytes: Vec<u8> = adp.adapter_name.iter()
                        .take_while(|&&c| c != 0)
                        .map(|&c| c as u8)
                        .collect();
                    let name = String::from_utf8_lossy(&name_bytes).trim().to_string();
                    if adp.vendor_id == ADL_VENDOR_AMD || name.to_lowercase().contains("radeon") || name.to_lowercase().contains("amd") {
                        selected_adapter = Some((adp.adapter_index, name));
                        break;
                    }
                }
            }

            let (active_adapter_index, raw_name) = selected_adapter?;
            let cleaned_name = raw_name
                .replace("AMD Radeon ", "")
                .replace("Radeon ", "")
                .replace("Graphics", "")
                .trim()
                .to_string();
            let final_name = if cleaned_name.is_empty() { "Radeon GPU".to_string() } else { format!("Radeon {}", cleaned_name) };

            tracing::info!(
                "AMD ADL initialized: adapter index {}, name '{}'",
                active_adapter_index,
                final_name
            );

            Some(AdlState {
                context_handle,
                active_adapter_index,
                adapter_name: final_name,
                fn_temp6,
                fn_temp5,
                fn_temp_n,
                fn_fan6,
                fn_fan5,
            })
        }
    }

    pub fn get_amd_gpu_data() -> Option<AmdGpuData> {
        let mut attempted = INIT_ATTEMPTED.lock().ok()?;
        let mut state_lock = ADL_STATE.lock().ok()?;

        if !*attempted {
            *attempted = true;
            *state_lock = init_adl();
        }

        let state = state_lock.as_ref()?;
        let idx = state.active_adapter_index;

        let mut temp = 0.0f32;

        unsafe {
            // 1. Try Overdrive6 Temperature
            if let Some(f6) = state.fn_temp6 {
                let mut milli = 0i32;
                if f6(idx, &mut milli) == ADL_OK && milli > 0 {
                    temp = milli as f32 / 1000.0;
                }
            }

            // 2. Try OverdriveN Temperature (RDNA / modern)
            if temp == 0.0 {
                if let (Some(fn_n), Some(ctx)) = (state.fn_temp_n, state.context_handle) {
                    let mut milli = 0i32;
                    // thermal_type: 1 = CORE, 2 = MEMORY
                    if fn_n(ctx, idx, 1, &mut milli) == ADL_OK && milli > 0 {
                        temp = milli as f32 / 1000.0;
                    }
                }
            }

            // 3. Try Overdrive5 Temperature
            if temp == 0.0 {
                if let Some(f5) = state.fn_temp5 {
                    let mut val = ADLTemperature {
                        size: std::mem::size_of::<ADLTemperature>() as i32,
                        temperature: 0,
                    };
                    if f5(idx, 0, &mut val) == ADL_OK && val.temperature > 0 {
                        temp = val.temperature as f32 / 1000.0;
                    }
                }
            }
        }

        let mut fan_rpm = 0i32;

        unsafe {
            // 1. Try Overdrive6 Fan Speed (RPM)
            if let Some(f6) = state.fn_fan6 {
                let mut val = ADLFanSpeedValue {
                    size: std::mem::size_of::<ADLFanSpeedValue>() as i32,
                    speed_type: 1, // RPM
                    fan_speed: 0,
                    flags: 0,
                };
                if f6(idx, &mut val) == ADL_OK && val.fan_speed > 0 {
                    fan_rpm = val.fan_speed;
                }
            }

            // 2. Try Overdrive5 Fan Speed
            if fan_rpm == 0 {
                if let Some(f5) = state.fn_fan5 {
                    let mut val = ADLFanSpeedValue {
                        size: std::mem::size_of::<ADLFanSpeedValue>() as i32,
                        speed_type: 1, // RPM
                        fan_speed: 0,
                        flags: 0,
                    };
                    if f5(idx, 0, &mut val) == ADL_OK && val.fan_speed > 0 {
                        fan_rpm = val.fan_speed;
                    }
                }
            }
        }

        Some(AmdGpuData {
            name: state.adapter_name.clone(),
            temp,
            fan_rpm,
        })
    }
}
