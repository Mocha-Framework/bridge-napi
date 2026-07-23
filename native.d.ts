// @mocha/bridge-napi — type declarations
// Implements IBridgeApp from @mocha/bridge-api
export type {
  IBridgeApp,
  IBridgeFFI,
  IBridgeFactory,
  QmlNode,
  QmlProperty,
  ShellWindowProps,
  BridgeRuntime,
} from "@mocha/bridge-api";

export {
  nativeAppCreate,
  nativeEngineCreate,
  nativeEngineLoad,
  nativeEngineReload,
  nativeEngineLoadShell,
  nativeEngineSetShellSource,
  nativeEngineSetShellWindowProps,
  nativeEngineRootObject,
  nativeObjectSetProperty,
  nativeObjectGetProperty,
  nativeObjectSetInt,
  nativeObjectGetInt,
  nativeObjectSetBool,
  nativeProcessEvents,
  nativeAppExec,
  nativeAppQuit,
  nativeEngineCreateProxy,
  nativeProxySetValue,
  nativeProxySetInt,
  nativeProxySetBool,
  nativeProxyGetValue,
  nativeProxyHasPendingCalls,
  nativeProxyDrainPendingCalls,
  nativeProxySetQobject,
  nativeEngineSetContext,
  nativeCreateListModel,
  nativeDestroyListModel,
  nativeModelSetRows,
  nativeModelClear,
  nativeFindChildByName,
  qmlRegisterAppObjects,
  qmlListRootObjects,
  qmlListChildren,
  qmlGetProperty,
  qmlGetTypeName,
  qmlGetObjectName,
  qmlSetProperty,
  qmlGetAllProperties,
  nativeWindowSetDarkTitleBar,
  nativeWindowStartSystemMove,
} from "./index.js";

export declare function createNativeApp(): Promise<IBridgeApp>;
export declare function getNativeApp(): IBridgeApp | null;
export declare class NativeApp implements IBridgeApp {
  init(): void;
  loadQML(qml: string, basePath?: string): void;
  reloadQML(qml: string, basePath?: string): number;
  loadShell(basePath?: string): void;
  setShellSource(qml: string): void;
  setShellWindowProps(opts: ShellWindowProps): void;
  setProperty(property: string, value: string | number | boolean): void;
  getProperty(property: string): string;
  createProxy(): number;
  destroyProxy(proxyId: number): void;
  proxySetValue(proxyId: number, name: string, value: unknown): void;
  proxyGetValue(proxyId: number, name: string): string;
  proxyHasPendingCalls(proxyId: number): boolean;
  proxyDrainPendingCalls(proxyId: number): string[];
  setContextProperty(name: string, proxyId: number): void;
  findChild(name: string): number | null;
  getRootObject(): number | null;
  getObjectProperty(objId: number, name: string): string;
  setObjectProperty(objId: number, name: string, value: string | number | boolean): void;
  setDarkTitleBar(dark: boolean): void;
  startSystemMove(objId: number): void;
  processEvents(): void;
  exec(): number;
  quit(): void;
  registerAppObjects(): void;
  listRootObjects(): QmlNode[];
  listChildren(objId: number): QmlNode[];
  getQmlProperty(objId: number, name: string): string;
  getQmlProperties(objId: number): QmlProperty[];
  setQmlProperty(objId: number, name: string, value: string): void;
  getQmlObjectId(objId: number): number;
}
