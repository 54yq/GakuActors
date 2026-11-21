use std::{env,path::PathBuf,process::Command};
fn main() {
    println!("cargo:rustc-env=SLINT_STYLE=fluent-dark");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=app-icon.rc");
    println!("cargo:rerun-if-changed=../GakuActors.ico");
    if env::var("CARGO_CFG_TARGET_OS").as_deref()==Ok("windows") && env::var("CARGO_CFG_TARGET_ENV").as_deref()==Ok("msvc") {
        let output=PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR missing")).join("gakuactors-icon.res");
        let rc=env::var_os("GAKUACTORS_RC").unwrap_or_else(||"rc.exe".into());
        let status=Command::new(rc).arg("/nologo").arg(format!("/fo{}",output.display())).arg("app-icon.rc").status().expect("Windows resource compiler rc.exe is required to embed the application icon");
        assert!(status.success(),"Windows icon resource compilation failed");
        println!("cargo:rustc-link-arg={}",output.display());
    }
}
