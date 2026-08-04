#include <QString>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QObject>
#include <QTimer>
#include <QThread>
#include <QVariant>
#include <QMetaObject>
#include <QMetaProperty>
#include <QWindow>
#include <QDebug>
#include <QScreen>
#include <QInputMethod>
#include <QStyleHints>
#include <QAccessible>
#include <QtGlobal>

#include <QMap>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QColor>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <functional>
#include <cstdio>
#include <QQmlEngine>
#include <QFile>
#include <QDir>
#include <QDateTime>

#include "qt_bridge.h"
#include "mocha_list_model.h"

// ── MochaPropertyMap: TS ↔ QML proxy via QQmlPropertyMap ──
//
// QQmlPropertyMap exposes dynamic properties to QML with automatic
// change notification.  Calling insert(key, value) emits
// valueChanged(key), which QML's binding engine tracks natively —
// no comma-operator tricks needed.

class MochaPropertyMap : public QQmlPropertyMap {
    Q_OBJECT

    QStringList _pendingCalls;
    int _seq = 0;

public:
    int proxyId = 0;

    MochaPropertyMap(QObject* parent = nullptr) : QQmlPropertyMap(this, parent) {}

    int seq() const { return _seq; }

    Q_INVOKABLE void setValue(QString name, QString value) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                QVariantList variantList = doc.array().toVariantList();
                insert(name, QVariant(variantList));
                _seq++;
                fprintf(stderr, "[C++ MochaPropertyMap] setValue('%s', QVariantList[%d]), _seq=%d\n",
                    name.toUtf8().constData(), (int)variantList.size(), _seq);
                // No emit seqChanged() - dynamic meta objects break if we add custom signals!
                _notifyProperty(name);
                return;
            }
            if (doc.isObject()) {
                QVariantMap variantMap = doc.object().toVariantMap();
                insert(name, QVariant(variantMap));
                _seq++;
                fprintf(stderr, "[C++ MochaPropertyMap] setValue('%s', QVariantMap[%d]), _seq=%d\n",
                    name.toUtf8().constData(), (int)variantMap.size(), _seq);
                // No emit seqChanged()
                _notifyProperty(name);
                return;
            }
        }
        if (value.startsWith("#") && (value.length() == 7 || value.length() == 9)) {
            QColor color(value);
            if (color.isValid()) {
                insert(name, QVariant::fromValue(color));
                _seq++;
                fprintf(stderr, "[C++ MochaPropertyMap] setValue('%s', QColor('%s')), _seq=%d\n",
                    name.toUtf8().constData(), value.toUtf8().constData(), _seq);
                // No emit seqChanged()
                _notifyProperty(name);
                return;
            }
        }
        insert(name, QVariant(value));
        _seq++;
        fprintf(stderr, "[C++ MochaPropertyMap] setValue('%s', '%s'), _seq=%d\n",
            name.toUtf8().constData(), value.toUtf8().constData(), _seq);
        // No emit seqChanged()
        _notifyProperty(name);
    }

    Q_INVOKABLE void setInt(QString name, int value) {
        insert(name, QVariant(value));
        _seq++;
        fprintf(stderr, "[C++ MochaPropertyMap] setInt('%s', %d), _seq=%d\n",
            name.toUtf8().constData(), value, _seq);
        // No emit seqChanged()
        _notifyProperty(name);
    }

    Q_INVOKABLE void setBool(QString name, bool value) {
        insert(name, QVariant(value));
        _seq++;
        fprintf(stderr, "[C++ MochaPropertyMap] setBool('%s', %d), _seq=%d\n",
            name.toUtf8().constData(), value ? 1 : 0, _seq);
        // No emit seqChanged()
        _notifyProperty(name);
    }

    Q_INVOKABLE QVariant getValue(QString name) const {
        return value(name);
    }

    Q_INVOKABLE QVariant get(QString name) const {
        return value(name);
    }

    Q_INVOKABLE void bridgeCall(QString method) {
        _pendingCalls.append(method);
        _seq++;
        fprintf(stderr, "[C++ MochaPropertyMap] bridgeCall('%s'), _seq=%d, pending=%d\n",
            method.toUtf8().constData(), _seq, _pendingCalls.size());
        // No emit seqChanged()
    }

    bool hasPendingCalls() const {
        return !_pendingCalls.isEmpty();
    }

    QString drainOneCall() {
        if (_pendingCalls.isEmpty()) return QString();
        return _pendingCalls.takeFirst();
    }

    void notifySeqChanged() {
        _seq++;
    }

private:
    void _notifyProperty(const QString& name) {
        const QMetaObject* mo = metaObject();
        int idx = mo->indexOfProperty(name.toUtf8().constData());
        if (idx >= 0) {
            QMetaProperty prop = mo->property(idx);
            if (prop.hasNotifySignal()) {
                QVariant val = value(name);
                prop.notifySignal().invoke(this, Qt::DirectConnection,
                    Q_ARG(QString, name),
                    Q_ARG(QVariant, val));
            }
        }
    }
};

static const char* SHELL_QML = R"mocha-shell(
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MochaDS

ApplicationWindow {
    id: mochaShell
    objectName: "mochaShell"
    visible: true
    title: "Mocha App"
    color: Theme.colors.background

    property string mochaSource: ""
    property bool hmrExplicitErrorVisible: false
    property bool hmrErrorVisible: hmrExplicitErrorVisible || mochaLoader.status === Loader.Error
    property string hmrErrorTitle: ""
    property string hmrErrorMessage: ""
    property string hmrErrorDetails: ""

    onMochaSourceChanged: {
        if (mochaSource !== "") {
            mochaLoader.source = mochaSource
        }
    }

    Loader {
        id: mochaLoader
        objectName: "mochaLoader"
        anchors.fill: parent

        onStatusChanged: {
            if (status === Loader.Ready) {
                mochaShell.hmrExplicitErrorVisible = false
            } else if (status === Loader.Error) {
                mochaShell.hmrErrorTitle = "HMR render failed"
                mochaShell.hmrErrorMessage = "The updated QML could not be rendered. Check terminal logs for native/QML details."
                if (mochaShell.hmrErrorDetails === "") {
                    mochaShell.hmrErrorDetails = String(source)
                }
            }
        }
    }

    Rectangle {
        visible: hmrErrorVisible
        z: 9999
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        radius: 12
        color: "#B91C1C"
        border.color: "#FCA5A5"
        border.width: 1
        implicitHeight: errorColumn.implicitHeight + 24

        Column {
            id: errorColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Text {
                text: mochaShell.hmrErrorTitle
                color: "#FFFFFF"
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.Wrap
            }

            Text {
                text: mochaShell.hmrErrorMessage
                color: "#FEE2E2"
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: parent.width
                color: "#7F1D1D"
                radius: 8
                visible: mochaShell.hmrErrorDetails !== ""
                implicitHeight: errorDetails.implicitHeight + 16

                Text {
                    id: errorDetails
                    anchors.fill: parent
                    anchors.margins: 8
                    text: mochaShell.hmrErrorDetails
                    color: "#FDE68A"
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
)mocha-shell";

static void mochaMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    fprintf(stderr, "[QT %s] %s\n",
        type == QtDebugMsg ? "DEBUG" :
        type == QtWarningMsg ? "WARN" :
        type == QtCriticalMsg ? "CRIT" :
        type == QtFatalMsg ? "FATAL" : "INFO",
        msg.toUtf8().constData());
    if (type == QtFatalMsg) abort();
}

extern "C" {

// QApplication

void* qt_app_create(int /*argc*/, char** /*argv*/) {
    qputenv("QT_QML_DEBUG", "1");
    qputenv("QML_DEBUGGER_PORT", "3768");
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");
    qInstallMessageHandler(mochaMessageHandler);
    fprintf(stderr, "[MOCHA DEBUG] Creating QGuiApplication...\n");
    static int dummy_argc = 1;
    static char dummy_argv0[] = "mocha-native";
    static char* dummy_argv[] = { dummy_argv0, nullptr };
    auto* app = new QGuiApplication(dummy_argc, dummy_argv);
    fprintf(stderr, "[MOCHA DEBUG] QGuiApplication created: %p, platform: %s\n", (void*)app, app->platformName().toUtf8().constData());
    return app;
}

void qt_app_destroy(void* app) {
    delete static_cast<QGuiApplication*>(app);
}

// QML Engine

void* qml_engine_create() {
    fprintf(stderr, "[MOCHA DEBUG] Creating QQmlApplicationEngine...\n");
    auto* e = new QQmlApplicationEngine();
    fprintf(stderr, "[MOCHA DEBUG] QQmlApplicationEngine created: %p\n", (void*)e);
    return e;
}

void qml_engine_destroy(void* engine) {
    delete static_cast<QQmlApplicationEngine*>(engine);
}

void qml_engine_close_all_windows(void* engine) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    const auto roots = e->rootObjects();
    fprintf(stderr, "[MOCHA DEBUG] Closing %d root windows...\n", (int)roots.size());
    for (QObject* root : roots) {
        QWindow* win = qobject_cast<QWindow*>(root);
        if (win) {
            fprintf(stderr, "[MOCHA DEBUG]   closing window: %p\n", (void*)win);
            win->close();
        }
    }
}

void qml_engine_clear_cache(void* engine) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    fprintf(stderr, "[MOCHA DEBUG] Clearing QML component cache...\n");
    e->clearComponentCache();
}

void qml_engine_load_data(void* engine, const char* qml_data, const char* base_path, const char* import_path) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    fprintf(stderr, "[MOCHA DEBUG] Loading QML data (%zu bytes)\n", strlen(qml_data));
    
    // Add MochaDS import path if provided
    if (import_path && import_path[0] != '\0') {
        fprintf(stderr, "[MOCHA DEBUG] Adding import path: %s\n", import_path);
        e->addImportPath(QString::fromUtf8(import_path));
    }
    
    QUrl baseUrl = QUrl::fromLocalFile(QString::fromUtf8(base_path));
    e->loadData(QByteArray(qml_data), baseUrl);
    fprintf(stderr, "[MOCHA DEBUG] QML loadData completed\n");
}

void qml_engine_load_shell(void* engine, const char* import_path) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    fprintf(stderr, "[MOCHA DEBUG] Loading MochaAppShell...\n");
    if (import_path && import_path[0] != '\0') {
        fprintf(stderr, "[MOCHA DEBUG] Adding import path: %s\n", import_path);
        e->addImportPath(QString::fromUtf8(import_path));
    }
    e->loadData(QByteArray(SHELL_QML), QUrl());
    fprintf(stderr, "[MOCHA DEBUG] MochaAppShell loaded\n");
}

void qml_engine_set_shell_source(void* engine, const char* qml_data) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    const auto roots = e->rootObjects();
    if (roots.isEmpty()) {
        fprintf(stderr, "[MOCHA DEBUG] set_shell_source: no root objects\n");
        return;
    }

    QObject* shell = roots.first();
    QObject* loader = shell->findChild<QObject*>("mochaLoader");
    if (!loader) {
        fprintf(stderr, "[MOCHA DEBUG] set_shell_source: loader not found\n");
        return;
    }

    QString tempDir = QDir::tempPath();
    QString fileName = QString("mocha_content_%1.qml").arg(QDateTime::currentMSecsSinceEpoch());
    QString filePath = tempDir + "/" + fileName;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(qml_data);
        file.close();
        fprintf(stderr, "[MOCHA DEBUG] Wrote content QML to: %s (%d bytes)\n",
            filePath.toUtf8().constData(), (int)strlen(qml_data));
    } else {
        fprintf(stderr, "[MOCHA DEBUG] Failed to write content QML to: %s\n",
            filePath.toUtf8().constData());
        return;
    }
    e->clearComponentCache();
    loader->setProperty("source", QUrl());
    loader->setProperty("source", QUrl::fromLocalFile(filePath));
    fprintf(stderr, "[MOCHA DEBUG] Loader source updated to: %s\n", filePath.toUtf8().constData());
}

void qml_engine_set_shell_window_props(void* engine, const char* title, int width, int height) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    const auto roots = e->rootObjects();
    if (roots.isEmpty()) return;

    QObject* shell = roots.first();
    if (title && title[0] != '\0') {
        shell->setProperty("title", QString::fromUtf8(title));
    }
    if (width > 0) shell->setProperty("width", width);
    if (height > 0) shell->setProperty("height", height);
}

void* qml_engine_root_objects(void* engine) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    auto roots = e->rootObjects();
    fprintf(stderr, "[MOCHA DEBUG] rootObjects count: %d\n", roots.size());
    if (roots.isEmpty()) {
        fprintf(stderr, "[MOCHA DEBUG] No root objects - QML errors?\n");
        return nullptr;
    }
    // Return the first root object - caller must manage via qt_object_addref
    QObject* root = roots.first();
    QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);
    fprintf(stderr, "[MOCHA DEBUG] Root object: %p (%s)\n", (void*)root, root->metaObject()->className());
    return root;
}

// QObject property access

void qt_object_addref(void* /*obj*/) {
    // Qt objects are managed by the QML engine or parent - no-op
}

const char* qt_object_get_property(void* obj, const char* name) {
    auto* o = static_cast<QObject*>(obj);
    QVariant val = o->property(name);
    static thread_local QByteArray result;
    result = val.toString().toUtf8();
    return result.constData();
}

void qt_object_set_property(void* obj, const char* name, const char* value) {
    auto* o = static_cast<QObject*>(obj);
    o->setProperty(name, QVariant(QString::fromUtf8(value)));
}

int qt_object_get_int_property(void* obj, const char* name) {
    auto* o = static_cast<QObject*>(obj);
    return o->property(name).toInt();
}

void qt_object_set_int_property(void* obj, const char* name, int value) {
    auto* o = static_cast<QObject*>(obj);
    o->setProperty(name, QVariant(value));
}

double qt_object_get_double_property(void* obj, const char* name) {
    auto* o = static_cast<QObject*>(obj);
    return o->property(name).toDouble();
}

void qt_object_set_double_property(void* obj, const char* name, double value) {
    auto* o = static_cast<QObject*>(obj);
    o->setProperty(name, QVariant(value));
}

int qt_object_get_bool_property(void* obj, const char* name) {
    auto* o = static_cast<QObject*>(obj);
    return o->property(name).toBool() ? 1 : 0;
}

void qt_object_set_bool_property(void* obj, const char* name, int value) {
    auto* o = static_cast<QObject*>(obj);
    o->setProperty(name, QVariant(value != 0));
}

// Event loop

void qt_app_process_events() {
    QCoreApplication::processEvents();
}

int qt_app_exec(void* app) {
    fprintf(stderr, "[MOCHA DEBUG] Entering Qt event loop (exec)...\n");
    auto ret = static_cast<QGuiApplication*>(app)->exec();
    fprintf(stderr, "[MOCHA DEBUG] Qt event loop exited with code: %d\n", ret);
    return ret;
}

void qt_app_quit(void* app) {
    static_cast<QGuiApplication*>(app)->quit();
}

// MochaPropertyMap

void* mocha_object_create(int proxyId) {
    auto* obj = new MochaPropertyMap();
    obj->proxyId = proxyId;
    QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
    return obj;
}

void mocha_object_destroy(void* obj) {
    delete static_cast<MochaPropertyMap*>(obj);
}

void mocha_object_set_value(void* obj, const char* name, const char* value) {
    static_cast<MochaPropertyMap*>(obj)->setValue(
        QString::fromUtf8(name), QString::fromUtf8(value));
}

void mocha_object_set_int(void* obj, const char* name, int value) {
    static_cast<MochaPropertyMap*>(obj)->setInt(QString::fromUtf8(name), value);
}

void mocha_object_set_bool(void* obj, const char* name, int value) {
    static_cast<MochaPropertyMap*>(obj)->setBool(QString::fromUtf8(name), value != 0);
}

const char* mocha_object_get_value(void* obj, const char* name) {
    auto* mo = static_cast<MochaPropertyMap*>(obj);
    static thread_local QByteArray result;
    result = mo->getValue(QString::fromUtf8(name)).toString().toUtf8();
    return result.constData();
}

int mocha_object_has_pending_calls(void* obj) {
    return static_cast<MochaPropertyMap*>(obj)->hasPendingCalls() ? 1 : 0;
}

int mocha_object_drain_pending_calls(void* obj, char* buf, int max) {
    auto* mo = static_cast<MochaPropertyMap*>(obj);
    QString call = mo->drainOneCall();
    if (call.isEmpty()) return 0;
    QByteArray utf8 = call.toUtf8();
    int len = qMin(utf8.size(), max - 1);
    memcpy(buf, utf8.constData(), len);
    buf[len] = '\0';
    return len;
}

void qml_engine_set_context_property(void* engine, const char* name, void* obj) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    auto* qobj = static_cast<QObject*>(obj);
    e->rootContext()->setContextProperty(QString::fromUtf8(name), qobj);
}

static int registerQmlObj(QObject* obj);

int qml_find_child_by_name(void* parent, const char* name) {
    auto* obj = static_cast<QObject*>(parent);
    auto* child = obj->findChild<QObject*>(
        QString::fromUtf8(name), Qt::FindChildrenRecursively);
    if (child) {
        QQmlEngine::setObjectOwnership(child, QQmlEngine::CppOwnership);
        return registerQmlObj(child);
    }
    return 0;
}

// ── QML Native Tree Inspector ──

static QMutex g_objMutex;
static int g_nextObjId = 1;
static QHash<int, QObject*> g_idToObj;
static QHash<QObject*, int> g_objToId;
static QList<int> g_rootIds;

static int registerQmlObjUnlocked(QObject* obj) {
    if (!obj) return 0;
    auto it = g_objToId.find(obj);
    if (it != g_objToId.end()) return it.value();
    int id = g_nextObjId++;
    g_idToObj[id] = obj;
    g_objToId[obj] = id;
    return id;
}

static int registerQmlObj(QObject* obj) {
    if (!obj) return 0;
    QMutexLocker lock(&g_objMutex);
    return registerQmlObjUnlocked(obj);
}

int qml_get_object_id(void* ptr) {
    if (!ptr) return 0;
    QMutexLocker lock(&g_objMutex);
    return g_objToId.value(static_cast<QObject*>(ptr), 0);
}

static void unregisterQmlObj(QObject* obj) {
    if (!obj) return;
    QMutexLocker lock(&g_objMutex);
    auto it = g_objToId.find(obj);
    if (it != g_objToId.end()) {
        g_idToObj.remove(it.value());
        g_objToId.erase(it);
    }
}

static void collectAllQmlObjects(QObject* root, QList<QObject*>& out) {
    if (!root) return;
    out.append(root);
    const auto children = root->children();
    for (QObject* child : children) {
        collectAllQmlObjects(child, out);
    }
}

void native_qml_register_app_objects(void* enginePtr) {
    auto* e = static_cast<QQmlApplicationEngine*>(enginePtr);
    if (!e) return;

    QMutexLocker lock(&g_objMutex);
    g_idToObj.clear();
    g_objToId.clear();
    g_rootIds.clear();
    g_nextObjId = 1;
    const auto roots = e->rootObjects();
    for (QObject* root : roots) {
        int rootId = registerQmlObjUnlocked(root);
        g_rootIds.append(rootId);
        QList<QObject*> all;
        collectAllQmlObjects(root, all);
        for (QObject* obj : all) {
            registerQmlObjUnlocked(obj);
        }
    }
}

int native_qml_list_root_objects(int* ids, int max) {
    QMutexLocker lock(&g_objMutex);
    int n = qMin(g_rootIds.size(), max);
    for (int i = 0; i < n; ++i) {
        ids[i] = g_rootIds[i];
    }
    return n; // count
}

int native_qml_list_children(int objId, int* ids, int max) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return 0;

    const auto children = obj->children();
    int n = qMin(children.size(), max);
    for (int i = 0; i < n; i++) {
        ids[i] = registerQmlObjUnlocked(children[i]);
    }
    return n;
}

const char* native_qml_get_property(int objId, const char* name) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return "";

    QVariant val = obj->property(name);
    static thread_local QByteArray result;
    result = val.toString().toUtf8();
    return result.constData();
}

const char* native_qml_get_type_name(int objId) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return "";
    return obj->metaObject()->className();
}

const char* native_qml_get_object_name(int objId) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return "";
    static thread_local QByteArray result;
    result = obj->objectName().toUtf8();
    return result.constData();
}

void native_qml_set_property(int objId, const char* name, const char* value) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return;

    QByteArray propName(name);
    QVariant current = obj->property(propName);
    QVariant newVal;

    if (!current.isValid() || current.type() == QVariant::String) {
        newVal = QVariant(QString::fromUtf8(value));
    } else if (current.type() == QVariant::Int) {
        bool ok;
        int v = QString::fromUtf8(value).toInt(&ok);
        if (ok) newVal = QVariant(v); else newVal = QVariant(QString::fromUtf8(value));
    } else if (current.type() == QVariant::Double) {
        bool ok;
        double v = QString::fromUtf8(value).toDouble(&ok);
        if (ok) newVal = QVariant(v); else newVal = QVariant(QString::fromUtf8(value));
    } else if (current.type() == QVariant::Bool) {
        QString sv = QString::fromUtf8(value).toLower();
        newVal = QVariant(sv == "true" || sv == "1");
    } else if (current.type() == QVariant::Color) {
        newVal = QVariant(QColor(QString::fromUtf8(value)));
    } else {
        newVal = QVariant(QString::fromUtf8(value));
    }

    obj->setProperty(propName, newVal);
}

void native_qml_set_property_int(int objId, const char* name, int value) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return;
    obj->setProperty(name, QVariant(value));
}

void native_qml_set_property_bool(int objId, const char* name, int value) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return;
    obj->setProperty(name, QVariant(value != 0));
}

void native_qml_set_property_double(int objId, const char* name, double value) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) return;
    obj->setProperty(name, QVariant(value));
}

void native_qml_get_all_properties(int objId, char* buf, int max) {
    QMutexLocker lock(&g_objMutex);
    QObject* obj = g_idToObj.value(objId);
    if (!obj) { buf[0] = '\0'; return; }

    const QMetaObject* meta = obj->metaObject();
    QByteArray result = QByteArray("[");

    for (int i = meta->propertyOffset(); i < meta->propertyCount(); i++) {
        QMetaProperty prop = meta->property(i);
        if (!prop.isReadable()) continue;
        if (result.size() > 1) result.append(',');
        QVariant val = prop.read(obj);

        QByteArray entry;
        entry.append("{\"n\":\"");
        entry.append(prop.name());
        entry.append("\",\"t\":\"");
        entry.append(val.typeName());
        entry.append("\",\"v\":\"");
        QByteArray escaped = val.toString().toUtf8();
        escaped.replace('\\', "\\\\");
        escaped.replace('"', "\\\"");
        entry.append(escaped);
        entry.append("\",\"r\":");
        entry.append(prop.isReadable() ? "true" : "false");
        entry.append(",\"w\":");
        entry.append(prop.isWritable() ? "true" : "false");
        entry.append('}');
        result.append(entry);
    }
    result.append(']');

    int len = qMin(result.size(), max - 1);
    memcpy(buf, result.constData(), len);
    buf[len] = '\0';
}

// ── Dark Title Bar (platform-specific) ──

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#ifdef __APPLE__
#include <objc/runtime.h>
#include <objc/message.h>

static id mocha_nsstring(const char* str) {
    return ((id (*)(id, SEL, const char*))objc_msgSend)(
        (id)objc_getClass("NSString"),
        sel_registerName("stringWithUTF8String:"),
        str
    );
}
#endif

void mocha_window_set_dark_title_bar(void* obj, int dark) {
    QWindow* win = qobject_cast<QWindow*>(static_cast<QObject*>(obj));
    if (!win) return;

#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd) return;
    BOOL useDark = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 1809+)
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
    fprintf(stderr, "[MOCHA] DWM dark title bar: %s\n", dark ? "on" : "off");
#endif

#ifdef __APPLE__
    void* nsView = reinterpret_cast<void*>(win->winId());
    if (!nsView) return;
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)((id)nsView, sel_registerName("window"));
    if (!nsWindow) return;
    Class appearanceClass = objc_getClass("NSAppearance");
    id name = mocha_nsstring(dark ? "NSAppearanceNameDarkAqua" : "NSAppearanceNameAqua");
    id appearance = ((id (*)(Class, SEL, id))objc_msgSend)(appearanceClass, sel_registerName("appearanceNamed:"), name);
    if (appearance) {
        ((void (*)(id, SEL, id))objc_msgSend)(nsWindow, sel_registerName("setAppearance:"), appearance);
        fprintf(stderr, "[MOCHA] macOS dark title bar: %s\n", dark ? "on" : "off");
    }
#endif
}

void mocha_window_start_system_move(void* obj) {
    QWindow* win = qobject_cast<QWindow*>(static_cast<QObject*>(obj));
    if (!win) return;

#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd) return;
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
#endif

#ifdef __APPLE__
    void* nsView = reinterpret_cast<void*>(win->winId());
    if (!nsView) return;
    id nsWindow = ((id (*)(id, SEL))objc_msgSend)((id)nsView, sel_registerName("window"));
    if (!nsWindow) return;
    id sharedApp = ((id (*)(Class, SEL))objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id event = ((id (*)(id, SEL))objc_msgSend)(sharedApp, sel_registerName("currentEvent"));
    if (event) {
        ((void (*)(id, SEL, id))objc_msgSend)(nsWindow, sel_registerName("performWindowDragWithEvent:"), event);
    }
#endif
}

// ── MochaPropertyMap: set QObject property (for models) ──

void mocha_property_map_set_qobject(void* obj, const char* key, void* qobj) {
    auto* map = static_cast<MochaPropertyMap*>(obj);
    auto* qobject = static_cast<QObject*>(qobj);
    QQmlEngine::setObjectOwnership(qobject, QQmlEngine::CppOwnership);
    map->insert(QString::fromUtf8(key), QVariant::fromValue(qobject));
    map->notifySeqChanged();
}

// ── MochaListModel factory functions ──

void* mocha_list_model_create() {
    auto* model = new MochaListModel();
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

void mocha_list_model_destroy(void* obj) {
    delete static_cast<MochaListModel*>(obj);
}

void mocha_list_model_set_rows(void* obj, const char* json) {
    auto* model = static_cast<MochaListModel*>(obj);
    QString str = QString::fromUtf8(json);
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(str.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        model->setRows(doc.array());
    }
}

void mocha_list_model_clear(void* obj) {
    static_cast<MochaListModel*>(obj)->clear();
}

// ── Mobile / Touch / Haptics / Screen ─────────────────────────────────────
//
// See meta/mobile-gestures.md. The QtNativeBridge QObject is registered as
// a MochaDS singleton so MediaQuery.qml can bind to its properties with
// ordinary QML bindings. A 50 ms poll timer keeps `keyboardHeight` and
// `pixelRatio` reasonably fresh without flooding the binding engine.

} // extern "C"

class QtNativeBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isTouchDevice READ isTouchDevice CONSTANT)
    Q_PROPERTY(qreal pixelRatio READ pixelRatio CONSTANT)
    Q_PROPERTY(bool prefersReducedMotion READ prefersReducedMotion CONSTANT)
    Q_PROPERTY(int keyboardHeight READ keyboardHeight NOTIFY keyboardHeightChanged)
    Q_PROPERTY(QVariantMap safeAreaInsets READ safeAreaInsets NOTIFY safeAreaInsetsChanged)

public:
    explicit QtNativeBridge(QObject* parent = nullptr) : QObject(parent) {
        // Poll the keyboard + screen at 20 Hz. Cheap (a handful of ints) and
        // guarantees MediaQuery.qml bindings stay live without Qt signals
        // being delivered across module boundaries.
        m_pollTimer.setInterval(50);
        QObject::connect(&m_pollTimer, &QTimer::timeout, this, [this] {
            int kh = qt_keyboard_height();
            if (kh != m_lastKeyboardHeight) {
                m_lastKeyboardHeight = kh;
                emit keyboardHeightChanged(kh);
            }
            QVariantMap insets;
            int t=0, r=0, b=0, l=0;
            qt_safe_area_insets(&t, &r, &b, &l);
            insets["top"] = t; insets["right"] = r; insets["bottom"] = b; insets["left"] = l;
            if (insets != m_lastInsets) {
                m_lastInsets = insets;
                emit safeAreaInsetsChanged(insets);
            }
        });
        m_pollTimer.start();
    }

    bool isTouchDevice() const { return qt_is_touch_device() != 0; }
    qreal pixelRatio() const { return qt_pixel_ratio_fixed() / 100.0; }
    bool prefersReducedMotion() const { return qt_prefers_reduced_motion() != 0; }
    int keyboardHeight() const { return m_lastKeyboardHeight; }
    QVariantMap safeAreaInsets() const { return m_lastInsets; }

    Q_INVOKABLE void haptic(int style) { qt_haptic(style); }
    Q_INVOKABLE void haptic(const QString& style) {
        int code = 0;
        if      (style == "selection")            code = 0;
        else if (style == "impactLight")          code = 1;
        else if (style == "impactMedium")         code = 2;
        else if (style == "impactHeavy")          code = 3;
        else if (style == "notificationSuccess")  code = 4;
        else if (style == "notificationWarning")  code = 5;
        else if (style == "notificationError")    code = 6;
        qt_haptic(code);
    }

signals:
    void keyboardHeightChanged(int);
    void safeAreaInsetsChanged(QVariantMap);

private:
    QTimer m_pollTimer;
    int m_lastKeyboardHeight = 0;
    QVariantMap m_lastInsets;
};

extern "C" {

int qt_is_touch_device() {
    if (!QGuiApplication::instance()) return 0;
    QString platform = QGuiApplication::platformName();
    if (platform == "eglfs" || platform == "android" || platform == "ios"
        || platform.startsWith("wayland") || platform == "vnc") {
        return 1;
    }
    // startDragDistance is non-zero on touch platforms per Qt docs.
    return QGuiApplication::styleHints()->startDragDistance() > 0 ? 1 : 0;
}

void qt_haptic(int style) {
    if (!QGuiApplication::instance()) return;
    if (qt_is_touch_device() == 0) return;
    (void)style;

#ifdef __APPLE__
    // UIImpactFeedbackGenerator / UINotificationFeedbackGenerator.
    // Same ObjC runtime trick used by mocha_window_set_dark_title_bar.
    #include <objc/runtime.h>
    #include <objc/message.h>
    auto sendMsg = [](id target, SEL sel) {
        ((void (*)(id, SEL))objc_msgSend)(target, sel);
    };
    Class impactCls = objc_getClass("UIImpactFeedbackGenerator");
    Class notifCls  = objc_getClass("UINotificationFeedbackGenerator");
    Class styleCls  = objc_getClass("UIImpactFeedbackStyle");
    if (!impactCls || !notifCls || !styleCls) return;

    id generator = nullptr;
    if (style >= 4 && notifCls) {
        generator = ((id (*)(Class, SEL))objc_msgSend)(notifCls, sel_registerName("new"));
    } else if (impactCls) {
        long long uiStyle = 0; // UIImpactFeedbackStyleLight
        if (style == 1)      uiStyle = 0; // Light
        else if (style == 2) uiStyle = 1; // Medium
        else if (style == 3) uiStyle = 2; // Heavy
        else                 uiStyle = 0;
        generator = ((id (*)(Class, SEL, long long))objc_msgSend)(
            impactCls, sel_registerName("alloc"));
        generator = ((id (*)(id, SEL, long long))objc_msgSend)(
            generator, sel_registerName("initWithStyle:"), uiStyle);
    }
    if (!generator) return;
    sendMsg(generator, sel_registerName("prepare"));
    if (style == 4) {
        ((void (*)(id, SEL))objc_msgSend)(generator, sel_registerName("notificationOccurred:"), 0); // Success
    } else if (style == 5) {
        ((void (*)(id, SEL))objc_msgSend)(generator, sel_registerName("notificationOccurred:"), 1); // Warning
    } else if (style == 6) {
        ((void (*)(id, SEL))objc_msgSend)(generator, sel_registerName("notificationOccurred:"), 2); // Error
    } else {
        sendMsg(generator, sel_registerName("impactOccurred"));
    }
    // Generator is autoreleased — ARC handles cleanup.
    // Defer release a tick to let the haptic play.
    QTimer::singleShot(100, [generator]() {
        ((void (*)(id, SEL))objc_msgSend)(generator, sel_registerName("release"));
    });
#endif
    // Android and Linux/Windows desktop paths intentionally omitted here —
    // they're per-platform and require conditional compilation that's tied
    // to the build target. iOS is the platform that ships first.
}

int qt_pixel_ratio_fixed() {
    if (!QGuiApplication::instance()) return 100;
    auto* s = QGuiApplication::primaryScreen();
    if (!s) return 100;
    return int(s->devicePixelRatio() * 100.0);
}

int qt_prefers_reduced_motion() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && defined(Q_ACCESSIBLE_HAS_PREFERS_REDUCED_MOTION)
    return QAccessible::prefersReducedMotion() ? 1 : 0;
#else
    return 0;
#endif
}

int qt_keyboard_height() {
    if (!QGuiApplication::instance()) return 0;
    auto* im = QGuiApplication::inputMethod();
    if (!im) return 0;
    QRectF r = im->keyboardRectangle();
    return int(r.height());
}

void qt_safe_area_insets(int* top, int* right, int* bottom, int* left) {
    *top = *right = *bottom = *left = 0;
    if (!QGuiApplication::instance()) return;
    auto* s = QGuiApplication::primaryScreen();
    if (!s) return;
    QRect avail = s->availableGeometry();
    QRect full  = s->geometry();
    *top    = avail.top()    - full.top();
    *left   = avail.left()   - full.left();
    *right  = full.right()   - avail.right();
    *bottom = full.bottom()  - avail.bottom();
    // Clamp to non-negative — some platform plugins report availableGeometry
    // larger than geometry in odd multi-screen setups.
    if (*top    < 0) *top    = 0;
    if (*left   < 0) *left   = 0;
    if (*right  < 0) *right  = 0;
    if (*bottom < 0) *bottom = 0;
}

void qt_qml_register_native_bridge(void* engine) {
    auto* e = static_cast<QQmlApplicationEngine*>(engine);
    if (!e) return;
    qmlRegisterSingletonType<QtNativeBridge>(
        "MochaDS", 1, 0,
        "NativeBridge",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            auto* obj = new QtNativeBridge();
            QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
            return obj;
        });
    Q_UNUSED(e);
}

} // extern "C"

// Include moc-generated code for MochaPropertyMap
#include "qt_bridge.moc"
