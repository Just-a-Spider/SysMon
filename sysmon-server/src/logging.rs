use std::path::PathBuf;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt, EnvFilter};
use sysinfo::{Components, System};

pub fn init_logging(config_dir: &PathBuf) -> tracing_appender::non_blocking::WorkerGuard {
    let file_appender = tracing_appender::rolling::never(config_dir, "sysmon.log");
    let (non_blocking, guard) = tracing_appender::non_blocking(file_appender);

    let filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new("info,sysmon_server=debug"));

    let stdout_layer = tracing_subscriber::fmt::layer()
        .with_writer(std::io::stdout)
        .with_ansi(true);

    let file_layer = tracing_subscriber::fmt::layer()
        .with_writer(non_blocking)
        .with_ansi(false);

    tracing_subscriber::registry()
        .with(filter)
        .with(stdout_layer)
        .with(file_layer)
        .init();

    guard
}

pub fn log_system_diagnostics(sys: &System, components: &Components) {
    tracing::info!("=== SysMon Server v{} Startup Diagnostics ===", env!("CARGO_PKG_VERSION"));
    tracing::info!(
        "Operating System: {} {} (Kernel: {})",
        System::name().unwrap_or_else(|| "Unknown".into()),
        System::os_version().unwrap_or_else(|| "Unknown".into()),
        System::kernel_version().unwrap_or_else(|| "Unknown".into())
    );
    tracing::info!("Host Name: {}", System::host_name().unwrap_or_else(|| "Unknown".into()));

    if let Some(first_cpu) = sys.cpus().first() {
        tracing::info!(
            "CPU: {} ({} physical cores, {} logical cores)",
            first_cpu.brand(),
            System::physical_core_count().unwrap_or(0),
            sys.cpus().len()
        );
    } else {
        tracing::info!("CPU: 0 CPUs detected by sysinfo");
    }

    tracing::info!(
        "Memory: {:.2} GB available / {:.2} GB total",
        sys.available_memory() as f64 / 1_000_000_000.0,
        sys.total_memory() as f64 / 1_000_000_000.0
    );

    let comp_list = components.list();
    tracing::info!("Hardware Sensors detected via sysinfo (total: {}):", comp_list.len());
    if comp_list.is_empty() {
        tracing::warn!("No sensors detected via sysinfo. On Windows, thermal zones via WMI often require Administrator privileges, and many motherboard BIOSes do not expose ACPI ThermalZone entries.");
    } else {
        for (i, comp) in comp_list.iter().enumerate() {
            tracing::info!(
                "  [{}] Label: '{}' | Temp: {:?} C | Max: {:?} C | Critical: {:?} C",
                i,
                comp.label(),
                comp.temperature(),
                comp.max(),
                comp.critical()
            );
        }
    }

    log_gpu_probe();
}

fn log_gpu_probe() {
    tracing::info!("Probing GPU diagnostics...");

    // Probe nvidia-smi
    match std::process::Command::new("nvidia-smi")
        .args(&["--query-gpu=name,temperature.gpu,driver_version", "--format=csv,noheader,nounits"])
        .output() 
    {
        Ok(out) if out.status.success() => {
            let s = String::from_utf8_lossy(&out.stdout);
            tracing::info!("nvidia-smi detected: {}", s.trim());
        }
        Ok(out) => {
            tracing::debug!("nvidia-smi returned non-zero status: {:?}", out.status);
        }
        Err(e) => {
            tracing::debug!("nvidia-smi query failed: {}", e);
        }
    }

    // Windows GPU and sensor diagnostics
    #[cfg(target_os = "windows")]
    {
        if let Some(amd_data) = crate::amd_adl::get_amd_gpu_data() {
            tracing::info!(
                "AMD ADL query succeeded: model='{}', temp={:.1} C, fan={} RPM",
                amd_data.name,
                amd_data.temp,
                amd_data.fan_rpm
            );
        } else {
            let adl_path = std::path::Path::new(r"C:\Windows\System32\atiadlxx.dll");
            if adl_path.exists() {
                tracing::info!("AMD ADL library found at C:\\Windows\\System32\\atiadlxx.dll (no active AMD telemetry returned)");
            } else {
                tracing::info!("AMD ADL library not found at C:\\Windows\\System32\\atiadlxx.dll");
            }
        }

        if let Some(dxgi_name) = crate::win_hw::get_dxgi_gpu_name() {
            tracing::info!("DXGI Primary GPU Adapter: '{}'", dxgi_name);
        }

        if let Some(wmi_cpu) = crate::win_hw::get_wmi_cpu_temp() {
            tracing::info!("WMI Hardware Monitor bridge active at startup: CPU temp = {:.1} C", wmi_cpu);
        } else {
            tracing::info!("WMI Hardware Monitor bridge not active at startup (will continuously retry in background)");
        }
    }

    // Linux hwmon check
    #[cfg(target_os = "linux")]
    {
        if let Ok(entries) = std::fs::read_dir("/sys/class/hwmon/") {
            for entry in entries.flatten() {
                let path = entry.path();
                if let Ok(name) = std::fs::read_to_string(path.join("name")) {
                    tracing::info!("hwmon device ({:?}): '{}'", path.file_name().unwrap_or_default(), name.trim());
                }
            }
        }
    }
}
