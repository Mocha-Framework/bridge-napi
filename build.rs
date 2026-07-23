extern crate napi_build;

use std::path::{Path, PathBuf};
use std::process::Command;

struct QtPaths {
    include_dirs: Vec<String>,
    lib_dir: String,
    libs: Vec<String>,
    #[allow(dead_code)]
    frameworks: Vec<String>,
    moc: String,
}

fn main() {
    napi_build::setup();

    let qt = find_qt();

    println!("cargo:warning=Qt include dirs: {:?}", qt.include_dirs);
    println!("cargo:warning=Qt lib dir: {}", qt.lib_dir);
    println!("cargo:warning=Qt moc: {}", qt.moc);

    // Generate moc file for qt_bridge.cpp
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let bridge_qt_src = manifest_dir.join("..").join("bridge-qt").join("src");
    let src_dir = manifest_dir.join("src");
    let moc_out = src_dir.join("qt_bridge.moc");

    let mut moc_cmd = Command::new(&qt.moc);
    for inc in &qt.include_dirs {
        moc_cmd.arg("-I").arg(inc);
    }
    let moc_status = moc_cmd
        .arg("-o")
        .arg(&moc_out)
        .arg(bridge_qt_src.join("qt_bridge.cpp"))
        .status()
        .expect("moc failed — is Qt6 installed?");
    if !moc_status.success() {
        panic!("moc failed — check Qt6 installation");
    }

    // Generate moc file for mocha_list_model.h
    let moc_list_out = src_dir.join("mocha_list_model.moc");
    let mut moc_list = Command::new(&qt.moc);
    for inc in &qt.include_dirs {
        moc_list.arg("-I").arg(inc);
    }
    moc_list.arg("-I").arg(&bridge_qt_src);
    let moc_list_status = moc_list
        .arg("-o")
        .arg(&moc_list_out)
        .arg(bridge_qt_src.join("mocha_list_model.h"))
        .status()
        .expect("moc (list_model) failed — is Qt6 installed?");
    if !moc_list_status.success() {
        panic!("moc (list_model) failed — check Qt6 installation");
    }

    // Compile C++ bridge from bridge-qt source
    let mut cc = cc::Build::new();
    cc.cpp(true)
        .file(bridge_qt_src.join("qt_bridge.cpp"))
        .file(bridge_qt_src.join("mocha_list_model.cpp"))
        .std("c++17")
        .pic(true);

    #[cfg(target_env = "msvc")]
    {
        cc.flag("/Zc:__cplusplus");
        cc.flag("/permissive-");
    }

    for inc in &qt.include_dirs {
        cc.include(inc);
    }
    // Add bridge-napi/src for generated moc files
    cc.include(&src_dir);

    // macOS: framework includes need special handling
    #[cfg(target_os = "macos")]
    for fw in &qt.frameworks {
        let fw_path = format!("{}/{}.framework/Headers", qt.lib_dir, fw);
        if Path::new(&fw_path).exists() {
            cc.include(fw_path);
        }
    }

    cc.compile("qt_bridge");

    // Link Qt libraries
    println!("cargo:rustc-link-search=native={}", qt.lib_dir);

    #[cfg(target_os = "macos")]
    {
        for fw in &qt.frameworks {
            println!("cargo:rustc-link-lib=framework={}", fw);
        }
        if qt.frameworks.is_empty() {
            for lib in &qt.libs {
                println!("cargo:rustc-link-lib={}", lib);
            }
        }
    }

    #[cfg(not(target_os = "macos"))]
    {
        for lib in &qt.libs {
            println!("cargo:rustc-link-lib={}", lib);
        }
    }

    println!("cargo:rerun-if-changed={}", bridge_qt_src.join("qt_bridge.cpp").display());
    println!("cargo:rerun-if-changed={}", bridge_qt_src.join("mocha_list_model.cpp").display());
    println!("cargo:rerun-if-changed={}", bridge_qt_src.join("mocha_list_model.h").display());
    println!("cargo:rerun-if-env-changed=QT6_DIR");
}

fn find_qt() -> QtPaths {
    // Strategy 1: qmake -query (cross-platform, most reliable)
    for qmake in &["qmake6", "qmake"] {
        if let Ok(paths) = qt_from_qmake(qmake) {
            return paths;
        }
    }

    // Strategy 2: QT6_DIR env var (CI-friendly)
    if let Ok(dir) = std::env::var("QT6_DIR") {
        if let Ok(paths) = qt_from_prefix(&dir) {
            return paths;
        }
    }

    // Strategy 3: pkg-config (Linux/macOS with dev packages)
    #[cfg(any(target_os = "linux", target_os = "macos"))]
    if let Ok(paths) = qt_from_pkgconfig() {
        return paths;
    }

    // Strategy 4: hardcoded platform paths
    let candidates = platform_qt_paths();
    for dir in &candidates {
        if let Ok(paths) = qt_from_prefix(dir) {
            return paths;
        }
    }

    panic!(
        "Qt6 not found. Install Qt6 and ensure qmake6 is in PATH, \
         or set QT6_DIR env var.\n\
         Linux:   sudo apt install qt6-base-dev qt6-tools-dev\n\
         macOS:   brew install qt@6\n\
         Windows: choco install qt6"
    );
}

fn qt_from_qmake(qmake: &str) -> Result<QtPaths, String> {
    let output = Command::new(qmake)
        .arg("-query")
        .output()
        .map_err(|e| format!("{} not found: {}", qmake, e))?;

    if !output.status.success() {
        return Err(format!("{} -query failed", qmake));
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    let get = |key: &str| -> String {
        for line in stdout.lines() {
            if let Some(val) = line.strip_prefix(&format!("{}:", key)) {
                return val.trim().to_string();
            }
        }
        String::new()
    };

    let prefix = get("QT_INSTALL_PREFIX");
    let headers = get("QT_INSTALL_HEADERS");
    let libs_dir = get("QT_INSTALL_LIBS");
    let bins = get("QT_INSTALL_BINS");

    let include_dirs = if headers.is_empty() {
        vec![format!("{}/include", prefix)]
    } else {
        let mut dirs = vec![headers.clone()];
        // Debian/Ubuntu puts module headers in subdirectories
        for module in &["QtCore", "QtGui", "QtQml", "QtQuick"] {
            let sub = format!("{}/{}", headers, module);
            if std::path::Path::new(&sub).exists() {
                dirs.push(sub);
            }
        }
        dirs
    };

    let lib_dir = if libs_dir.is_empty() {
        format!("{}/lib", prefix)
    } else {
        libs_dir
    };

    let moc = find_moc_in(&bins, &prefix, &lib_dir);

    let (libs, frameworks) = qt_libs_for_platform(&lib_dir);

    Ok(QtPaths { include_dirs, lib_dir, libs, frameworks, moc })
}

fn qt_from_prefix(dir: &str) -> Result<QtPaths, String> {
    let base_include = format!("{}/include", dir);
    let mut include_dirs = vec![base_include.clone()];
    // Add module subdirectories for distros like Debian/Ubuntu
    for module in &["QtCore", "QtGui", "QtQml", "QtQuick"] {
        let sub = format!("{}/{}", base_include, module);
        if std::path::Path::new(&sub).exists() {
            include_dirs.push(sub);
        }
    }

    let lib_dir = if Path::new(&format!("{}/lib", dir)).exists() {
        format!("{}/lib", dir)
    } else {
        format!("{}/lib", dir) // fallback
    };

    let bins = format!("{}/bin", dir);
    let moc = find_moc_in(&bins, dir, &lib_dir);

    let (libs, frameworks) = qt_libs_for_platform(&lib_dir);

    let include_dirs = if cfg!(target_os = "macos") {
        let mut dirs = include_dirs;
        for fw in &frameworks {
            let fw_inc = format!("{}/{}.framework/Headers", lib_dir, fw);
            if Path::new(&fw_inc).exists() {
                dirs.push(fw_inc);
            }
        }
        dirs
    } else {
        include_dirs
    };

    Ok(QtPaths { include_dirs, lib_dir, libs, frameworks, moc })
}

#[cfg(any(target_os = "linux", target_os = "macos"))]
fn qt_from_pkgconfig() -> Result<QtPaths, String> {
    let pkgs = "Qt6Core Qt6Gui Qt6Qml Qt6Quick";
    let output = Command::new("pkg-config")
        .args(["--cflags-only-I", "--libs"])
        .args(pkgs.split_whitespace())
        .output()
        .map_err(|_| "pkg-config not found".to_string())?;

    if !output.status.success() {
        return Err("pkg-config failed".to_string());
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut include_dirs = Vec::new();
    let mut lib_dir = String::new();
    let mut libs = Vec::new();

    for part in stdout.split_whitespace() {
        if part.starts_with("-I") {
            include_dirs.push(part[2..].to_string());
        } else if part.starts_with("-L") {
            lib_dir = part[2..].to_string();
        } else if part.starts_with("-l") {
            libs.push(part[2..].to_string());
        }
    }

    if include_dirs.is_empty() {
        return Err("No includes from pkg-config".to_string());
    }

    let moc = find_moc_fallback();
    let (_, frameworks) = qt_libs_for_platform(&lib_dir);

    Ok(QtPaths { include_dirs, lib_dir, libs, frameworks, moc })
}

fn qt_libs_for_platform(_lib_dir: &str) -> (Vec<String>, Vec<String>) {
    #[cfg(target_os = "macos")]
    {
        // Qt6 on macOS via Homebrew can use frameworks
        let fw_base = format!("{}/QtCore.framework", lib_dir);
        if Path::new(&fw_base).exists() {
            return (
                vec![],
                vec![
                    "QtCore".into(),
                    "QtGui".into(),
                    "QtQml".into(),
                    "QtQuick".into(),
                ],
            );
        }
    }

    // Standard .so/.dylib/.lib linking
    (
        vec![
            "Qt6Core".into(),
            "Qt6Gui".into(),
            "Qt6Qml".into(),
            "Qt6Quick".into(),
        ],
        vec![],
    )
}

fn find_moc_in(bins: &str, prefix: &str, _lib_dir: &str) -> String {
    let exe = if cfg!(target_os = "windows") { "moc.exe" } else { "moc" };

    // 1. QT_INSTALL_BINS/moc
    let in_bins = Path::new(bins).join(exe);
    if in_bins.exists() {
        return in_bins.to_string_lossy().to_string();
    }

    // 2. prefix/libexec/moc (some Linux installs)
    let in_libexec = Path::new(prefix).join("libexec").join(exe);
    if in_libexec.exists() {
        return in_libexec.to_string_lossy().to_string();
    }

    find_moc_fallback()
}

fn find_moc_fallback() -> String {
    #[cfg(target_os = "linux")]
    let platform_paths = vec![
        "/usr/lib/qt6/moc".to_string(),
        "/usr/lib/qt6/libexec/moc".to_string(),
        "/usr/lib/qt6/bin/moc".to_string(),
        "/usr/lib/x86_64-linux-gnu/qt6/libexec/moc".to_string(),
    ];

    #[cfg(target_os = "macos")]
    let platform_paths = vec![
        "/opt/homebrew/opt/qt@6/bin/moc".to_string(),
        "/opt/homebrew/bin/moc".to_string(),
        "/usr/local/opt/qt@6/bin/moc".to_string(),
        "/usr/local/bin/moc".to_string(),
    ];

    #[cfg(target_os = "windows")]
    let platform_paths = vec![
        "C:\\Qt\\6.8.2\\msvc2022_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.8.0\\msvc2022_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.7.0\\msvc2022_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.8.0\\msvc2019_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.7.0\\msvc2019_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.8.0\\mingw_64\\bin\\moc.exe".to_string(),
        "C:\\Qt\\6.7.0\\mingw_64\\bin\\moc.exe".to_string(),
    ];

    #[cfg(not(any(target_os = "linux", target_os = "macos", target_os = "windows")))]
    let platform_paths: Vec<String> = vec![];

    // Strategy 1: platform-specific hardcoded paths
    for path in &platform_paths {
        if std::path::Path::new(path).exists() {
            return path.to_string();
        }
    }

    // Strategy 2: command in PATH (moc-qt6 first, then moc)
    for cmd in &["moc-qt6", "moc"] {
        if Command::new(cmd).arg("--version").output().map(|o| o.status.success()).unwrap_or(false) {
            return cmd.to_string();
        }
    }

    panic!(
        "moc (Qt6 Meta-Object Compiler) not found.\n\
         Arch:    pacman -S qt6-base (moc at /usr/lib/qt6/moc)\n\
         Debian:  apt install qt6-base-dev-tools\n\
         macOS:   brew install qt@6\n\
         Windows: Set QT6_DIR or install Qt6 from qt.io"
    );
}

fn platform_qt_paths() -> Vec<String> {
    #[cfg(target_os = "linux")]
    return vec![
        "/usr/lib/x86_64-linux-gnu/qt6".into(),
        "/usr/lib/aarch64-linux-gnu/qt6".into(),
        "/usr".into(),
    ];

    #[cfg(target_os = "macos")]
    return vec![
        "/opt/homebrew/opt/qt@6".into(),
        "/opt/homebrew/Cellar/qt@6/*/".into(), // glob won't work in this context but kept as hint
        "/usr/local/opt/qt@6".into(),
    ];

    #[cfg(target_os = "windows")]
    return vec![
        "C:\\Qt\\6.8.2\\msvc2022_64".into(),
        "C:\\Qt\\6.8.0\\msvc2022_64".into(),
        "C:\\Qt\\6.7.0\\msvc2022_64".into(),
        "C:\\Qt\\6.8.0\\msvc2019_64".into(),
    ];

    #[cfg(not(any(target_os = "linux", target_os = "macos", target_os = "windows")))]
    return vec![];
}
