use napi::{Error, Result, Status};
use napi_derive::napi;
use std::collections::HashMap;
use std::ffi::{c_char, CStr, CString};
use std::os::raw::c_void;
use std::sync::{LazyLock, Mutex};

extern "C" {
    fn qt_app_create(argc: i32, argv: *const *const c_char) -> *mut c_void;
    fn qt_app_destroy(app: *mut c_void);
    fn qml_engine_create() -> *mut c_void;
    fn qml_engine_destroy(engine: *mut c_void);
    fn qml_engine_close_all_windows(engine: *mut c_void);
    fn qml_engine_clear_cache(engine: *mut c_void);
    fn qml_engine_load_data(engine: *mut c_void, qml_data: *const c_char, base_path: *const c_char, import_path: *const c_char);
    fn qml_engine_load_shell(engine: *mut c_void, import_path: *const c_char);
    fn qml_engine_set_shell_source(engine: *mut c_void, qml_data: *const c_char);
    fn qml_engine_set_shell_window_props(engine: *mut c_void, title: *const c_char, width: i32, height: i32);
    fn qml_engine_root_objects(engine: *mut c_void) -> *mut c_void;
    fn qt_object_get_property(obj: *mut c_void, name: *const c_char) -> *const c_char;
    fn qt_object_set_property(obj: *mut c_void, name: *const c_char, value: *const c_char);
    fn qt_object_get_int_property(obj: *mut c_void, name: *const c_char) -> i32;
    fn qt_object_set_int_property(obj: *mut c_void, name: *const c_char, value: i32);
    fn qt_object_set_bool_property(obj: *mut c_void, name: *const c_char, value: i32);
    fn qt_app_process_events();
    fn qt_app_exec(app: *mut c_void) -> i32;
    fn qt_app_quit(app: *mut c_void);

    // MochaDynamicObject
    fn mocha_object_create(proxy_id: i32) -> *mut c_void;
    fn mocha_object_destroy(obj: *mut c_void);
    fn mocha_object_set_value(obj: *mut c_void, name: *const c_char, value: *const c_char);
    fn mocha_object_set_int(obj: *mut c_void, name: *const c_char, value: i32);
    fn mocha_object_set_bool(obj: *mut c_void, name: *const c_char, value: i32);
    fn mocha_object_get_value(obj: *mut c_void, name: *const c_char) -> *const c_char;
    fn mocha_object_has_pending_calls(obj: *mut c_void) -> i32;
    fn mocha_object_drain_pending_calls(obj: *mut c_void, buf: *mut c_char, max: i32) -> i32;
    fn mocha_set_call_handler(cb: Option<unsafe extern "C" fn(proxy_id: i32, method: *const c_char)>);
    fn qml_engine_set_context_property(engine: *mut c_void, name: *const c_char, obj: *mut c_void);
    fn qml_find_child_by_name(parent: *mut c_void, name: *const c_char) -> i32;

    // QML Native Tree Inspector
    fn native_qml_register_app_objects(engine: *mut c_void);
    fn native_qml_list_root_objects(ids: *mut i32, max: i32) -> i32;
    fn native_qml_list_children(obj_id: i32, ids: *mut i32, max: i32) -> i32;
    fn native_qml_get_property(obj_id: i32, name: *const c_char) -> *const c_char;
    fn native_qml_get_type_name(obj_id: i32) -> *const c_char;
    fn native_qml_get_object_name(obj_id: i32) -> *const c_char;
    fn native_qml_set_property(obj_id: i32, name: *const c_char, value: *const c_char);
    fn qml_get_object_id(ptr: *mut c_void) -> i32;
    fn native_qml_get_all_properties(obj_id: i32, buf: *mut c_char, max: i32);

    // Window management
    fn mocha_window_set_dark_title_bar(obj: *mut c_void, dark: i32);
    fn mocha_window_start_system_move(obj: *mut c_void);

    // MochaListModel
    fn mocha_list_model_create() -> *mut c_void;
    fn mocha_list_model_destroy(obj: *mut c_void);
    fn mocha_list_model_set_rows(obj: *mut c_void, json: *const c_char);
    fn mocha_list_model_clear(obj: *mut c_void);

    // MochaPropertyMap QObject setter
    fn mocha_property_map_set_qobject(obj: *mut c_void, key: *const c_char, qobj: *mut c_void);
}

struct NativeState {
    next_id: u32,
    app: *mut c_void,
    objects: HashMap<u32, *mut c_void>,
}

unsafe impl Send for NativeState {}
unsafe impl Sync for NativeState {}

impl NativeState {
    fn alloc_id(&mut self, ptr: *mut c_void) -> u32 {
        let id = self.next_id;
        self.next_id += 1;
        self.objects.insert(id, ptr);
        id
    }

    fn get_ptr(&self, id: u32) -> Option<*mut c_void> {
        self.objects.get(&id).copied()
    }
}

static STATE: LazyLock<Mutex<NativeState>> = LazyLock::new(|| Mutex::new(NativeState {
    next_id: 1,
    app: std::ptr::null_mut(),
    objects: HashMap::new(),
}));

#[napi]
pub fn native_app_create() -> Result<()> {
    let c_name = CString::new("mocha-native").unwrap();
    let argv: [*const c_char; 1] = [c_name.as_ptr()];

    unsafe {
        let app = qt_app_create(1, argv.as_ptr());
        if app.is_null() {
            return Err(Error::new(Status::GenericFailure, "Failed to create QApplication"));
        }
        let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
        state.app = app;
    }
    Ok(())
}

#[napi]
pub fn native_engine_create() -> Result<u32> {
    unsafe {
        let engine = qml_engine_create();
        if engine.is_null() {
            return Err(Error::new(Status::GenericFailure, "Failed to create QQmlApplicationEngine"));
        }
        let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
        Ok(state.alloc_id(engine))
    }
}

#[napi]
pub fn native_engine_load(engine_id: u32, qml_data: String, base_path: String, import_path: Option<String>) -> Result<()> {
    let c_qml = CString::new(qml_data).map_err(|e| Error::from_reason(e.to_string()))?;
    let c_base = CString::new(base_path).map_err(|e| Error::from_reason(e.to_string()))?;
    let import_path_str = import_path.unwrap_or_default();
    let c_import = CString::new(import_path_str.clone()).map_err(|e| Error::from_reason(e.to_string()))?;

    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;

    unsafe {
        qml_engine_load_data(engine, c_qml.as_ptr(), c_base.as_ptr(), c_import.as_ptr());
    }
    Ok(())
}

#[napi]
pub fn native_engine_reload(engine_id: u32, qml_data: String, base_path: String, import_path: Option<String>) -> Result<u32> {
    let c_qml = CString::new(qml_data).map_err(|e| Error::from_reason(e.to_string()))?;
    let c_base = CString::new(base_path).map_err(|e| Error::from_reason(e.to_string()))?;
    let import_path_str = import_path.unwrap_or_default();
    let c_import = CString::new(import_path_str.clone()).map_err(|e| Error::from_reason(e.to_string()))?;

    let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;

    unsafe {
        qml_engine_close_all_windows(engine);
        qt_app_process_events();
        qml_engine_clear_cache(engine);
        qml_engine_load_data(engine, c_qml.as_ptr(), c_base.as_ptr(), c_import.as_ptr());
        let obj = qml_engine_root_objects(engine);
        if obj.is_null() {
            return Err(Error::new(Status::GenericFailure, "QML reload failed — no root object"));
        }
        Ok(state.alloc_id(obj))
    }
}

#[napi]
pub fn native_engine_load_shell(engine_id: u32, import_path: Option<String>) -> Result<()> {
    let import_path_str = import_path.unwrap_or_default();
    let c_import = CString::new(import_path_str).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;
    unsafe {
        qml_engine_load_shell(engine, c_import.as_ptr());
    }
    Ok(())
}

#[napi]
pub fn native_engine_set_shell_source(engine_id: u32, qml_data: String) -> Result<()> {
    let c_qml = CString::new(qml_data).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;
    unsafe {
        qml_engine_set_shell_source(engine, c_qml.as_ptr());
    }
    Ok(())
}

#[napi]
pub fn native_engine_set_shell_window_props(
    engine_id: u32,
    title: Option<String>,
    width: Option<i32>,
    height: Option<i32>,
) -> Result<()> {
    let c_title = title
        .map(|t| CString::new(t).unwrap_or_default());
    let c_title_ptr = c_title
        .as_ref()
        .map(|s| s.as_ptr())
        .unwrap_or(std::ptr::null());
    let w = width.unwrap_or(-1);
    let h = height.unwrap_or(-1);
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;
    unsafe {
        qml_engine_set_shell_window_props(engine, c_title_ptr, w, h);
    }
    Ok(())
}

#[napi]
pub fn native_engine_root_object(engine_id: u32) -> Result<u32> {
    let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;

    unsafe {
        let obj = qml_engine_root_objects(engine);
        if obj.is_null() {
            return Err(Error::new(Status::GenericFailure, "QML failed to load or no root object"));
        }
        Ok(state.alloc_id(obj))
    }
}

#[napi]
pub fn native_object_set_property(obj_id: u32, name: String, value: String) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let c_value = CString::new(value).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe {
        qt_object_set_property(obj, c_name.as_ptr(), c_value.as_ptr());
    }
    Ok(())
}

#[napi]
pub fn native_object_get_property(obj_id: u32, name: String) -> Result<String> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe {
        let result = qt_object_get_property(obj, c_name.as_ptr());
        let c_str = CStr::from_ptr(result);
        Ok(c_str.to_string_lossy().into_owned())
    }
}

#[napi]
pub fn native_object_set_int(obj_id: u32, name: String, value: i32) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe {
        qt_object_set_int_property(obj, c_name.as_ptr(), value);
    }
    Ok(())
}

#[napi]
pub fn native_object_get_int(obj_id: u32, name: String) -> Result<i32> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe {
        Ok(qt_object_get_int_property(obj, c_name.as_ptr()))
    }
}

#[napi]
pub fn native_object_set_bool(obj_id: u32, name: String, value: bool) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe {
        qt_object_set_bool_property(obj, c_name.as_ptr(), if value { 1 } else { 0 });
    }
    Ok(())
}

#[napi]
pub fn native_process_events() -> Result<()> {
    unsafe { qt_app_process_events(); }
    Ok(())
}

#[napi]
pub fn native_app_exec() -> Result<i32> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let app = state.app;
    drop(state);
    if app.is_null() {
        return Err(Error::new(Status::GenericFailure, "QApplication not created"));
    }
    unsafe { Ok(qt_app_exec(app)) }
}

#[napi]
pub fn native_app_quit() -> Result<()> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let app = state.app;
    if !app.is_null() {
        unsafe { qt_app_quit(app); }
    }
    Ok(())
}

// ── Proxy (MochaDynamicObject) bindings ──

#[napi]
pub fn native_engine_create_proxy(engine_id: u32) -> Result<u32> {
    unsafe {
        let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
        let id = state.next_id;
        state.next_id += 1;

        let proxy = mocha_object_create(id as i32);
        if proxy.is_null() {
            return Err(Error::new(Status::GenericFailure, "Failed to create MochaDynamicObject"));
        }
        state.objects.insert(id, proxy);
        Ok(id)
    }
}

#[napi]
pub fn native_proxy_set_value(proxy_id: u32, name: String, value: String) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let c_value = CString::new(value).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        mocha_object_set_value(proxy, c_name.as_ptr(), c_value.as_ptr());
    }
    Ok(())
}

#[napi]
pub fn native_proxy_set_int(proxy_id: u32, name: String, value: i32) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        mocha_object_set_int(proxy, c_name.as_ptr(), value);
    }
    Ok(())
}

#[napi]
pub fn native_proxy_set_bool(proxy_id: u32, name: String, value: bool) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        mocha_object_set_bool(proxy, c_name.as_ptr(), if value { 1 } else { 0 });
    }
    Ok(())
}

#[napi]
pub fn native_proxy_get_value(proxy_id: u32, name: String) -> Result<String> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        let result = mocha_object_get_value(proxy, c_name.as_ptr());
        let c_str = CStr::from_ptr(result);
        Ok(c_str.to_string_lossy().into_owned())
    }
}

#[napi]
pub fn native_proxy_has_pending_calls(proxy_id: u32) -> Result<bool> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe { Ok(mocha_object_has_pending_calls(proxy) != 0) }
}

#[napi]
pub fn native_proxy_drain_pending_calls(proxy_id: u32) -> Result<Vec<String>> {
    let mut calls = Vec::new();
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        let mut buf = [0i8; 4096];
        loop {
            let n = mocha_object_drain_pending_calls(proxy, buf.as_mut_ptr(), 4096);
            if n <= 0 { break; }
            let s = CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned();
            if !s.is_empty() { calls.push(s); }
        }
    }
    Ok(calls)
}

#[napi]
pub fn native_engine_set_context(engine_id: u32, name: String, proxy_id: u32) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    unsafe {
        qml_engine_set_context_property(engine, c_name.as_ptr(), proxy);
    }
    Ok(())
}

#[napi]
pub fn native_find_child_by_name(parent_id: u32, name: String) -> Result<u32> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let parent = state.get_ptr(parent_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid parent handle"))?;
    unsafe {
        let qml_id = qml_find_child_by_name(parent, c_name.as_ptr());
        Ok(qml_id as u32)
    }
}

#[napi]
pub fn native_get_qml_object_id(obj_id: u32) -> Result<u32> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let ptr = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe { Ok(qml_get_object_id(ptr) as u32) }
}

// ── QML Native Tree Inspector ──

#[napi]
pub fn qml_register_app_objects(engine_id: u32) -> Result<()> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let engine = state.get_ptr(engine_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid engine handle"))?;
    unsafe { native_qml_register_app_objects(engine); }
    Ok(())
}

#[napi]
pub fn qml_list_root_objects() -> Vec<i32> {
    let mut buf = vec![0i32; 512];
    let n = unsafe { native_qml_list_root_objects(buf.as_mut_ptr(), buf.len() as i32) };
    buf.truncate(n as usize);
    buf
}

#[napi]
pub fn qml_list_children(obj_id: i32) -> Vec<i32> {
    let mut buf = vec![0i32; 256];
    let n = unsafe { native_qml_list_children(obj_id, buf.as_mut_ptr(), buf.len() as i32) };
    buf.truncate(n as usize);
    buf
}

#[napi]
pub fn qml_get_property(obj_id: i32, name: String) -> Option<String> {
    let c_name = CString::new(name).unwrap();
    let ptr = unsafe { native_qml_get_property(obj_id, c_name.as_ptr()) };
    if ptr.is_null() { return None; }
    let s = unsafe { CStr::from_ptr(ptr).to_string_lossy().to_string() };
    if s.is_empty() { None } else { Some(s) }
}

#[napi]
pub fn qml_get_type_name(obj_id: i32) -> String {
    let ptr = unsafe { native_qml_get_type_name(obj_id) };
    if ptr.is_null() { return String::new(); }
    unsafe { CStr::from_ptr(ptr).to_string_lossy().to_string() }
}

#[napi]
pub fn qml_get_object_name(obj_id: i32) -> String {
    let ptr = unsafe { native_qml_get_object_name(obj_id) };
    if ptr.is_null() { return String::new(); }
    unsafe { CStr::from_ptr(ptr).to_string_lossy().to_string() }
}

#[napi]
pub fn qml_set_property(obj_id: i32, name: String, value: String) -> Result<()> {
    let c_name = CString::new(name).unwrap();
    let c_value = CString::new(value).unwrap();
    unsafe { native_qml_set_property(obj_id, c_name.as_ptr(), c_value.as_ptr()); }
    Ok(())
}

#[napi]
pub fn qml_get_all_properties(obj_id: i32) -> Result<String> {
    let mut buf = vec![0u8; 8192];
    unsafe { native_qml_get_all_properties(obj_id, buf.as_mut_ptr() as *mut c_char, buf.len() as i32); }
    let s = String::from_utf8_lossy(&buf);
    let nul = s.find('\0').unwrap_or(s.len());
    Ok(s[..nul].to_string())
}

// ── Window management ──

#[napi]
pub fn native_window_set_dark_title_bar(obj_id: u32, dark: bool) -> Result<()> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe { mocha_window_set_dark_title_bar(obj, if dark { 1 } else { 0 }); }
    Ok(())
}

#[napi]
pub fn native_window_start_system_move(obj_id: u32) -> Result<()> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let obj = state.get_ptr(obj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid object handle"))?;
    unsafe { mocha_window_start_system_move(obj); }
    Ok(())
}

// ── MochaListModel bindings ──

#[napi]
pub fn native_create_list_model() -> Result<u32> {
    unsafe {
        let model = mocha_list_model_create();
        if model.is_null() {
            return Err(Error::new(Status::GenericFailure, "Failed to create MochaListModel"));
        }
        let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
        Ok(state.alloc_id(model))
    }
}

#[napi]
pub fn native_destroy_list_model(model_id: u32) -> Result<()> {
    let mut state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let model = state.objects.remove(&model_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid model id"))?;
    unsafe { mocha_list_model_destroy(model); }
    Ok(())
}

#[napi]
pub fn native_model_set_rows(model_id: u32, json: String) -> Result<()> {
    let c_json = CString::new(json).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let model = state.get_ptr(model_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid model id"))?;
    unsafe { mocha_list_model_set_rows(model, c_json.as_ptr()); }
    Ok(())
}

#[napi]
pub fn native_model_clear(model_id: u32) -> Result<()> {
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let model = state.get_ptr(model_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid model id"))?;
    unsafe { mocha_list_model_clear(model); }
    Ok(())
}

// ── MochaPropertyMap: set QObject property ──

#[napi]
pub fn native_proxy_set_qobject(proxy_id: u32, name: String, qobj_id: u32) -> Result<()> {
    let c_name = CString::new(name).map_err(|e| Error::from_reason(e.to_string()))?;
    let state = STATE.lock().map_err(|e| Error::from_reason(e.to_string()))?;
    let proxy = state.get_ptr(proxy_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid proxy handle"))?;
    let qobj = state.get_ptr(qobj_id)
        .ok_or_else(|| Error::new(Status::InvalidArg, "Invalid qobject handle"))?;
    unsafe { mocha_property_map_set_qobject(proxy, c_name.as_ptr(), qobj); }
    Ok(())
}
