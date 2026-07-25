#ifndef MOCHA_QT_BRIDGE_H
#define MOCHA_QT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// QApplication
void* qt_app_create(int argc, char** argv);
void  qt_app_destroy(void* app);

// QML Engine
void* qml_engine_create();
void  qml_engine_destroy(void* engine);
void  qml_engine_close_all_windows(void* engine);
void  qml_engine_clear_cache(void* engine);
void  qml_engine_load_data(void* engine, const char* qml_data, const char* base_path, const char* import_path);
void  qml_engine_load_shell(void* engine, const char* import_path);
void  qml_engine_set_shell_source(void* engine, const char* qml_data);
void  qml_engine_set_shell_window_props(void* engine, const char* title, int width, int height);
void* qml_engine_root_objects(void* engine);

// QObject property access
void        qt_object_addref(void* obj);
const char* qt_object_get_property(void* obj, const char* name);
void        qt_object_set_property(void* obj, const char* name, const char* value);
int         qt_object_get_int_property(void* obj, const char* name);
void        qt_object_set_int_property(void* obj, const char* name, int value);
double      qt_object_get_double_property(void* obj, const char* name);
void        qt_object_set_double_property(void* obj, const char* name, double value);
int         qt_object_get_bool_property(void* obj, const char* name);
void        qt_object_set_bool_property(void* obj, const char* name, int value);

// Event loop
void qt_app_process_events();
int  qt_app_exec(void* app);
void qt_app_quit(void* app);

// MochaDynamicObject (proxy)
void* mocha_object_create(int proxyId);
void  mocha_object_destroy(void* obj);
void  mocha_object_set_value(void* obj, const char* name, const char* value);
void  mocha_object_set_int(void* obj, const char* name, int value);
void  mocha_object_set_bool(void* obj, const char* name, int value);
const char* mocha_object_get_value(void* obj, const char* name);
int         mocha_object_has_pending_calls(void* obj);
int         mocha_object_drain_pending_calls(void* obj, char* buf, int max);
void        qml_engine_set_context_property(void* engine, const char* name, void* obj);
int         qml_find_child_by_name(void* parent, const char* name);

// QML Native Tree Inspector
void  native_qml_register_app_objects(void* enginePtr);
int   native_qml_list_root_objects(int* ids, int max);
int   native_qml_list_children(int objId, int* ids, int max);
const char* native_qml_get_property(int objId, const char* name);
const char* native_qml_get_type_name(int objId);
const char* native_qml_get_object_name(int objId);
void        native_qml_set_property(int objId, const char* name, const char* value);
void        native_qml_set_property_int(int objId, const char* name, int value);
void        native_qml_set_property_bool(int objId, const char* name, int value);
void        native_qml_set_property_double(int objId, const char* name, double value);
void        native_qml_get_all_properties(int objId, char* buf, int max);
int         qml_get_object_id(void* ptr);

// Window management
void mocha_window_set_dark_title_bar(void* obj, int dark);
void mocha_window_start_system_move(void* obj);

// MochaPropertyMap QObject setter
void mocha_property_map_set_qobject(void* obj, const char* key, void* qobj);

// MochaListModel
void* mocha_list_model_create();
void  mocha_list_model_destroy(void* obj);
void  mocha_list_model_set_rows(void* obj, const char* json);
void  mocha_list_model_clear(void* obj);

#ifdef __cplusplus
}
#endif

#endif // MOCHA_QT_BRIDGE_H
