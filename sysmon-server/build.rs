fn main() {
    #[cfg(feature = "cam")]
    #[cfg(target_os = "linux")]
    {
        use std::env;
        use std::fs;
        use std::path::{Path, PathBuf};
        let search_dirs = [
            "/usr/lib64",
            "/lib64",
            "/usr/lib/x86_64-linux-gnu",
            "/usr/lib",
            "/lib",
        ];

        let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
        let target_link = out_dir.join("libgbm.so");

        for dir in search_dirs {
            let p1 = Path::new(dir).join("libgbm.so.1");
            let p2 = Path::new(dir).join("libgbm.so.1.0.0");
            let found = if p1.exists() {
                Some(p1)
            } else if p2.exists() {
                Some(p2)
            } else {
                None
            };

            if let Some(src) = found {
                let _ = fs::remove_file(&target_link);
                #[cfg(unix)]
                {
                    use std::os::unix::fs::symlink;
                    if symlink(&src, &target_link).is_ok() {
                        println!("cargo:rustc-link-search=native={}", out_dir.display());
                        break;
                    }
                }
            }
        }
    }
}
