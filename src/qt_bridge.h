#ifndef MOCHA_QT_BRIDGE_H
#define MOCHA_QT_BRIDGE_H

// Export all C API symbols from the cdylib so the Rust side (linked into
// the same .node) can reference them via `extern "C"`.
#ifndef MOCHA_EXPORT
#define MOCHA_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// QApplication
MOCHA_EXPORT void* qt_app_create(int argc, char** argv);
void  qt_app_destroy(void* app);

// QML Engine
MOCHA_EXPORT void* qml_engine_create();
void  qml_engine_destroy(void* engine);
void  qml_engine_close_all_windows(void* engine);
void  qml_engine_clear_cache(void* engine);
void  qml_engine_load_data(void* engine, const char* qml_data, const char* base_path, const char* import_path);
void  qml_engine_load_shell(void* engine, const char* import_path);
void  qml_engine_set_shell_source(void* engine, const char* qml_data);
void  qml_engine_set_shell_window_props(void* engine, const char* title, int width, int height);
MOCHA_EXPORT void* qml_engine_root_objects(void* engine);

// QObject property access
void        qt_object_addref(void* obj);
MOCHA_EXPORT const char* qt_object_get_property(void* obj, const char* name);
void        qt_object_set_property(void* obj, const char* name, const char* value);
 MOCHA_EXPORT int         qt_object_get_int_property(void* obj, const char* name);
void        qt_object_set_int_property(void* obj, const char* name, int value);
double      qt_object_get_double_property(void* obj, const char* name);
void        qt_object_set_double_property(void* obj, const char* name, double value);
 MOCHA_EXPORT int         qt_object_get_bool_property(void* obj, const char* name);
void        qt_object_set_bool_property(void* obj, const char* name, int value);

// Event loop
MOCHA_EXPORT void qt_app_process_events();
 MOCHA_EXPORT int  qt_app_exec(void* app);
MOCHA_EXPORT void qt_app_quit(void* app);

// MochaDynamicObject (proxy)
MOCHA_EXPORT void* mocha_object_create(int proxyId);
void  mocha_object_destroy(void* obj);
void  mocha_object_set_value(void* obj, const char* name, const char* value);
void  mocha_object_set_int(void* obj, const char* name, int value);
void  mocha_object_set_bool(void* obj, const char* name, int value);
MOCHA_EXPORT const char* mocha_object_get_value(void* obj, const char* name);
 MOCHA_EXPORT int         mocha_object_has_pending_calls(void* obj);
 MOCHA_EXPORT int         mocha_object_drain_pending_calls(void* obj, char* buf, int max);
void        qml_engine_set_context_property(void* engine, const char* name, void* obj);
 MOCHA_EXPORT int         qml_find_child_by_name(void* parent, const char* name);

// QML Native Tree Inspector
void  native_qml_register_app_objects(void* enginePtr);
 MOCHA_EXPORT int   native_qml_list_root_objects(int* ids, int max);
 MOCHA_EXPORT int   native_qml_list_children(int objId, int* ids, int max);
MOCHA_EXPORT const char* native_qml_get_property(int objId, const char* name);
MOCHA_EXPORT const char* native_qml_get_type_name(int objId);
MOCHA_EXPORT const char* native_qml_get_object_name(int objId);
void        native_qml_set_property(int objId, const char* name, const char* value);
void        native_qml_set_property_int(int objId, const char* name, int value);
void        native_qml_set_property_bool(int objId, const char* name, int value);
void        native_qml_set_property_double(int objId, const char* name, double value);
void        native_qml_get_all_properties(int objId, char* buf, int max);
 MOCHA_EXPORT int         qml_get_object_id(void* ptr);

// Window management
MOCHA_EXPORT void mocha_window_set_dark_title_bar(void* obj, int dark);
MOCHA_EXPORT void mocha_window_start_system_move(void* obj);

// MochaPropertyMap QObject setter
MOCHA_EXPORT void mocha_property_map_set_qobject(void* obj, const char* key, void* qobj);

// MochaListModel
MOCHA_EXPORT void* mocha_list_model_create();
void  mocha_list_model_destroy(void* obj);
void  mocha_list_model_set_rows(void* obj, const char* json);
void  mocha_list_model_clear(void* obj);

// ── Mobile / Touch / Haptics / Screen ─────────────────────────────────────
// See meta/mobile-gestures.md for context.
//
// All functions are safe to call before QGuiApplication is created — they
// gracefully return safe defaults in that case.

// Returns 1 if the runtime platform exposes a touch digitizer (iOS, Android,
// eglfs, Wayland with tablet mode, etc.), 0 otherwise.
 MOCHA_EXPORT int qt_is_touch_device();

// Trigger a tactile feedback pattern. `style` is one of:
//   0 = selection            (light tick — selection pickers)
//   1 = impactLight          (light bump — primary button tap)
//   2 = impactMedium         (medium bump — drawer/modal swipe-snap)
//   3 = impactHeavy          (heavy bump — long-press commit)
//   4 = notificationSuccess
//   5 = notificationWarning
//   6 = notificationError
// No-op on platforms without a haptic actuator (desktop without gamepad).
MOCHA_EXPORT void qt_haptic(int style);

// Primary screen device pixel ratio × 100 (fixed-point for FFI stability).
// 100 = 1.0×, 200 = 2.0× (typical iOS retina), 300 = 3.0×.
 MOCHA_EXPORT int qt_pixel_ratio_fixed();

// Returns 1 if the user has expressed a preference for reduced motion
// (Qt 6.5+ via QAccessible::prefersReducedMotion), 0 otherwise.
 MOCHA_EXPORT int qt_prefers_reduced_motion();

// Current virtual keyboard height in pixels (0 when hidden). Drives the
// MediaQuery.keyboardHeight property; updated automatically when the
// keyboard appears/disappears/resizes.
 MOCHA_EXPORT int qt_keyboard_height();

// Safe-area insets (top/right/bottom/left in pixels) from the primary
// screen's available geometry minus full geometry. Used for notches,
// home-bar, status bars. 0 on platforms that don't report them.
MOCHA_EXPORT void qt_safe_area_insets(int* top, int* right, int* bottom, int* left);

// Registers the `MochaDS.NativeBridge` singleton on the given engine.
// Call once after engine creation; the singleton exposes read-only
// properties (isTouchDevice, pixelRatio, keyboardHeight, safeAreaInsets,
// prefersReducedMotion) and an invokable haptic(style) method that all
// forward to the qt_* functions above.
MOCHA_EXPORT void qt_qml_register_native_bridge(void* engine);

#ifdef __cplusplus
}
#endif

#endif // MOCHA_QT_BRIDGE_H
