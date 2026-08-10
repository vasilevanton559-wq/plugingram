#include "plugin_system/plugins_script_host.h"

#include "plugin_system/plugins_ui_chrome.h"
#include "plugin_system/plugins_ui_extension.h"

extern "C" {
#include <quickjs.h>
}

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>

#include <crl/crl_on_main.h>
#include <crl/crl_time.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace PluginSystem {
namespace {

constexpr auto kMaxScriptBytes = 256 * 1024;
constexpr auto kMemoryLimitBytes = 8 * 1024 * 1024;
// Hard cap: plugins must not spam language switches / force-restarts.
// 2 = one “apply” + one “restore” per process lifetime.
constexpr auto kMaxLangSetsPerProcess = 2;
constexpr auto kMinOpacity = 0.35;
constexpr auto kMaxOpacity = 1.0;
constexpr auto kMinScale = 75;
constexpr auto kMaxScale = 300;
constexpr auto kMaxComposerButtons = 3;
constexpr auto kMaxFontFamilyLen = 64;
// Must leave the current UI stack (Settings enable click) before theme/chrome.
constexpr auto kUiDeferMs = crl::time(50);
constexpr auto kHeavyUiDeferMs = crl::time(120);

[[nodiscard]] QString SanitizeLabel(QString label) {
	label = label.trimmed();
	// Keep letters/digits/spaces/basic punctuation; drop emoji/symbols.
	static const auto re = QRegularExpression(
		QStringLiteral("[^\\p{L}\\p{N} _\\-.:/+#()]"));
	label.replace(re, QString());
	label = label.simplified();
	if (label.size() > 24) {
		label = label.left(24);
	}
	return label;
}

[[nodiscard]] QString JsToString(JSContext *ctx, JSValueConst value) {
	const auto cstr = JS_ToCString(ctx, value);
	auto text = cstr ? QString::fromUtf8(cstr) : QString();
	if (cstr) {
		JS_FreeCString(ctx, cstr);
	}
	return text;
}

[[nodiscard]] ScriptInstance *InstanceFromCtx(JSContext *ctx) {
	return static_cast<ScriptInstance*>(JS_GetContextOpaque(ctx));
}

JSValue JsToast(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	ScriptHost::Instance().jsToast(JsToString(ctx, argv[0]));
	return JS_UNDEFINED;
}

JSValue JsAlert(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	auto title = QStringLiteral("Plugin");
	auto text = QString();
	if (argc >= 1 && JS_IsObject(argv[0])) {
		const auto t = JS_GetPropertyStr(ctx, argv[0], "title");
		const auto b = JS_GetPropertyStr(ctx, argv[0], "text");
		if (!JS_IsUndefined(t)) {
			title = JsToString(ctx, t);
		}
		if (!JS_IsUndefined(b)) {
			text = JsToString(ctx, b);
		}
		JS_FreeValue(ctx, t);
		JS_FreeValue(ctx, b);
	} else if (argc >= 1) {
		text = JsToString(ctx, argv[0]);
		if (argc >= 2) {
			title = text;
			text = JsToString(ctx, argv[1]);
		}
	}
	ScriptHost::Instance().jsAlert(title, text);
	return JS_UNDEFINED;
}

JSValue JsLog(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	const auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_UNDEFINED;
	}
	ScriptHost::Instance().jsLog(
		inst->plugin.manifest.id,
		JsToString(ctx, argv[0]));
	return JS_UNDEFINED;
}

JSValue JsOnAction(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	const auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 2 || !JS_IsFunction(ctx, argv[1])) {
		return JS_ThrowTypeError(ctx, "onAction(id, function) expected");
	}
	const auto id = JsToString(ctx, argv[0]).trimmed();
	if (id.isEmpty()) {
		return JS_ThrowTypeError(ctx, "action id is empty");
	}
	auto fn = argv[1];
	ScriptHost::Instance().jsOnAction(
		inst->plugin.manifest.id,
		id,
		&fn);
	return JS_UNDEFINED;
}

JSValue JsStorageGet(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	const auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_UNDEFINED;
	}
	const auto key = JsToString(ctx, argv[0]);
	if (!inst->storage.contains(key)) {
		return JS_UNDEFINED;
	}
	const auto utf = inst->storage.value(key).toString().toUtf8();
	return JS_NewStringLen(ctx, utf.constData(), size_t(utf.size()));
}

JSValue JsStorageSet(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	const auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 2) {
		return JS_UNDEFINED;
	}
	ScriptHost::Instance().jsStorageSet(
		inst->plugin.manifest.id,
		JsToString(ctx, argv[0]),
		JsToString(ctx, argv[1]));
	return JS_UNDEFINED;
}

JSValue JsStorageRemove(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	const auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_UNDEFINED;
	}
	ScriptHost::Instance().jsStorageRemove(
		inst->plugin.manifest.id,
		JsToString(ctx, argv[0]));
	return JS_UNDEFINED;
}

JSValue JsAddPanel(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1 || !JS_IsObject(argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	const auto obj = argv[0];
	auto panel = UiPanelDescriptor();
	panel.pluginId = inst->plugin.manifest.id;
	panel.pluginName = inst->plugin.manifest.name;

	const auto idVal = JS_GetPropertyStr(ctx, obj, "id");
	const auto titleVal = JS_GetPropertyStr(ctx, obj, "title");
	const auto descVal = JS_GetPropertyStr(ctx, obj, "description");
	const auto placeVal = JS_GetPropertyStr(ctx, obj, "placement");
	panel.id = JsToString(ctx, idVal);
	panel.title = JsToString(ctx, titleVal);
	panel.description = JsToString(ctx, descVal);
	panel.placement = JsToString(ctx, placeVal);
	JS_FreeValue(ctx, idVal);
	JS_FreeValue(ctx, titleVal);
	JS_FreeValue(ctx, descVal);
	JS_FreeValue(ctx, placeVal);

	if (panel.id.isEmpty()) {
		ScriptHost::Instance().jsLog(
			inst->plugin.manifest.id,
			QStringLiteral("addPanel: id is required"));
		return JS_NewBool(ctx, 0);
	}
	panel.title = SanitizeLabel(panel.title);
	if (panel.title.isEmpty()) {
		panel.title = inst->plugin.manifest.name;
	}
	if (panel.placement.isEmpty()) {
		panel.placement = QStringLiteral("settings.sidebar");
	}
	const auto isSidebar = (panel.placement
		== QStringLiteral("settings.sidebar"));
	const auto isComposer = (panel.placement
		== QStringLiteral("chat.composer"));
	if (!isSidebar && !isComposer) {
		ScriptHost::Instance().jsLog(
			inst->plugin.manifest.id,
			QStringLiteral("addPanel: unsupported placement"));
		return JS_NewBool(ctx, 0);
	}
	if (isSidebar
		&& !ManifestHasPermission(
			inst->plugin.manifest,
			PluginPermission::UiModify)) {
		ScriptHost::Instance().jsLog(
			inst->plugin.manifest.id,
			QStringLiteral("addPanel: missing ui.modify"));
		return JS_NewBool(ctx, 0);
	}
	if (isComposer
		&& !ManifestHasPermission(
			inst->plugin.manifest,
			PluginPermission::UiComposer)) {
		ScriptHost::Instance().jsLog(
			inst->plugin.manifest.id,
			QStringLiteral("addPanel: missing ui.composer"));
		return JS_NewBool(ctx, 0);
	}
	if (panel.description.isEmpty()) {
		panel.description = inst->plugin.manifest.description;
	}

	const auto actionsVal = JS_GetPropertyStr(ctx, obj, "actions");
	if (JS_IsArray(actionsVal)) {
		const auto lengthVal = JS_GetPropertyStr(ctx, actionsVal, "length");
		uint32_t length = 0;
		JS_ToUint32(ctx, &length, lengthVal);
		JS_FreeValue(ctx, lengthVal);
		for (uint32_t i = 0; i < length; ++i) {
			const auto item = JS_GetPropertyUint32(ctx, actionsVal, i);
			if (JS_IsObject(item)) {
				const auto aid = JS_GetPropertyStr(ctx, item, "id");
				const auto label = JS_GetPropertyStr(ctx, item, "label");
				const auto title = JS_GetPropertyStr(ctx, item, "title");
				auto command = PluginCommand();
				command.id = JsToString(ctx, aid);
				command.title = JsToString(ctx, label);
				if (command.title.isEmpty()) {
					command.title = JsToString(ctx, title);
				}
				if (command.title.isEmpty()) {
					command.title = command.id;
				}
				JS_FreeValue(ctx, aid);
				JS_FreeValue(ctx, label);
				JS_FreeValue(ctx, title);
				if (!command.id.isEmpty()) {
					panel.actions.push_back(std::move(command));
				}
			}
			JS_FreeValue(ctx, item);
		}
	}
	JS_FreeValue(ctx, actionsVal);

	ScriptHost::Instance().jsAddPanel(inst->plugin.manifest.id, panel);
	return JS_NewBool(ctx, 1);
}

JSValue JsLangGet(JSContext *ctx, JSValueConst, int, JSValueConst *) {
	const auto id = ScriptHost::Instance().jsLangGet().toUtf8();
	return JS_NewString(ctx, id.constData());
}

JSValue JsLangSet(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsLangSet(
		inst->plugin.manifest.id,
		JsToString(ctx, argv[0]));
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsSetOpacity(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_NewBool(ctx, 0);
	}
	double opacity = 1.0;
	if (JS_ToFloat64(ctx, &opacity, argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsSetOpacity(
		inst->plugin.manifest.id,
		opacity);
	return JS_NewBool(ctx, ok ? 1 : 0);
}

[[nodiscard]] QJsonObject JsObjectToJson(JSContext *ctx, JSValueConst value) {
	auto result = QJsonObject();
	if (!JS_IsObject(value)) {
		return result;
	}
	JSPropertyEnum *tabs = nullptr;
	uint32_t count = 0;
	if (JS_GetOwnPropertyNames(
			ctx,
			&tabs,
			&count,
			value,
			JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
		return result;
	}
	for (uint32_t i = 0; i < count; ++i) {
		const auto key = JS_AtomToCString(ctx, tabs[i].atom);
		if (!key) {
			continue;
		}
		const auto prop = JS_GetProperty(ctx, value, tabs[i].atom);
		if (JS_IsString(prop)) {
			result.insert(QString::fromUtf8(key), JsToString(ctx, prop));
		}
		JS_FreeValue(ctx, prop);
		JS_FreeCString(ctx, key);
	}
	JS_FreePropertyEnum(ctx, tabs, count);
	return result;
}

JSValue JsSetColors(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1 || !JS_IsObject(argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsSetColors(
		inst->plugin.manifest.id,
		JsObjectToJson(ctx, argv[0]));
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsClearColors(JSContext *ctx, JSValueConst, int, JSValueConst *) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsClearColors(
		inst->plugin.manifest.id);
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsSetScale(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_NewBool(ctx, 0);
	}
	int32_t scale = 100;
	if (JS_ToInt32(ctx, &scale, argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsSetScale(
		inst->plugin.manifest.id,
		scale);
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsSetFont(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1) {
		return JS_NewBool(ctx, 0);
	}
	const auto ok = ScriptHost::Instance().jsSetFont(
		inst->plugin.manifest.id,
		JsToString(ctx, argv[0]));
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsSetChrome(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1 || !JS_IsObject(argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	auto flags = UiChromeFlags();
	const auto filters = JS_GetPropertyStr(ctx, argv[0], "filters");
	const auto mainMenu = JS_GetPropertyStr(ctx, argv[0], "mainMenu");
	const auto search = JS_GetPropertyStr(ctx, argv[0], "search");
	if (!JS_IsUndefined(filters)) {
		flags.filters = JS_ToBool(ctx, filters);
	}
	if (!JS_IsUndefined(mainMenu)) {
		flags.mainMenu = JS_ToBool(ctx, mainMenu);
	}
	if (!JS_IsUndefined(search)) {
		flags.search = JS_ToBool(ctx, search);
	}
	JS_FreeValue(ctx, filters);
	JS_FreeValue(ctx, mainMenu);
	JS_FreeValue(ctx, search);
	const auto ok = ScriptHost::Instance().jsSetChrome(
		inst->plugin.manifest.id,
		flags);
	return JS_NewBool(ctx, ok ? 1 : 0);
}

JSValue JsAddButton(
		JSContext *ctx,
		JSValueConst,
		int argc,
		JSValueConst *argv) {
	auto *inst = InstanceFromCtx(ctx);
	if (!inst || argc < 1 || !JS_IsObject(argv[0])) {
		return JS_NewBool(ctx, 0);
	}
	const auto obj = argv[0];
	auto panel = UiPanelDescriptor();
	panel.pluginId = inst->plugin.manifest.id;
	panel.pluginName = inst->plugin.manifest.name;
	const auto idVal = JS_GetPropertyStr(ctx, obj, "id");
	const auto labelVal = JS_GetPropertyStr(ctx, obj, "label");
	const auto titleVal = JS_GetPropertyStr(ctx, obj, "title");
	const auto placeVal = JS_GetPropertyStr(ctx, obj, "placement");
	panel.id = JsToString(ctx, idVal);
	auto label = SanitizeLabel(JsToString(ctx, labelVal));
	if (label.isEmpty()) {
		label = SanitizeLabel(JsToString(ctx, titleVal));
	}
	panel.title = label;
	panel.placement = JsToString(ctx, placeVal);
	JS_FreeValue(ctx, idVal);
	JS_FreeValue(ctx, labelVal);
	JS_FreeValue(ctx, titleVal);
	JS_FreeValue(ctx, placeVal);
	if (panel.id.isEmpty()) {
		ScriptHost::Instance().jsLog(
			inst->plugin.manifest.id,
			QStringLiteral("addButton: id is required"));
		return JS_NewBool(ctx, 0);
	}
	if (panel.title.isEmpty()) {
		panel.title = panel.id;
	}
	if (panel.placement.isEmpty()) {
		panel.placement = QStringLiteral("chat.composer");
	}
	panel.actions.push_back(PluginCommand{
		.id = panel.id,
		.title = panel.title,
	});
	const auto ok = ScriptHost::Instance().jsAddButton(
		inst->plugin.manifest.id,
		panel);
	return JS_NewBool(ctx, ok ? 1 : 0);
}

void SetMethod(
		JSContext *ctx,
		JSValue obj,
		const char *name,
		JSCFunction *fn,
		int argc) {
	JS_SetPropertyStr(
		ctx,
		obj,
		name,
		JS_NewCFunction(ctx, fn, name, argc));
}

} // namespace

QString ResolveScriptPath(const QString &rootPath) {
	const auto candidates = {
		QStringLiteral("/plugin.js"),
		QStringLiteral("/plugin-script.js"),
		QStringLiteral("/main.js"),
	};
	for (const auto &suffix : candidates) {
		const auto path = rootPath + suffix;
		if (PathIsInsideRoot(rootPath, path)
			&& QFileInfo::exists(path)
			&& QFileInfo(path).isFile()) {
			return path;
		}
	}
	return {};
}

ScriptHost &ScriptHost::Instance() {
	static ScriptHost instance;
	return instance;
}

void ScriptHost::setUiHooks(ScriptUiHooks hooks) {
	_ui = std::move(hooks);
}

bool ScriptHost::hasScript(const PluginDescriptor &plugin) const {
	return !plugin.scriptPath.isEmpty()
		|| !ResolveScriptPath(plugin.rootPath).isEmpty();
}

bool ScriptHost::isRunning(const QString &pluginId) const {
	return _instances.find(pluginId) != _instances.end();
}

QString ScriptHost::StoragePath(const QString &rootPath) {
	return rootPath + QStringLiteral("/storage.json");
}

ScriptInstance *ScriptHost::find(const QString &pluginId) {
	const auto it = _instances.find(pluginId);
	return (it == _instances.end()) ? nullptr : it->second.get();
}

const ScriptInstance *ScriptHost::find(const QString &pluginId) const {
	const auto it = _instances.find(pluginId);
	return (it == _instances.end()) ? nullptr : it->second.get();
}

bool ScriptHost::loadStorage(ScriptInstance *instance) {
	instance->storage = QJsonObject();
	instance->storageDirty = false;
	auto file = QFile(StoragePath(instance->plugin.rootPath));
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		return true;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (document.isObject()) {
		instance->storage = document.object();
	}
	return true;
}

bool ScriptHost::saveStorage(ScriptInstance *instance) const {
	if (!instance || !instance->storageDirty) {
		return true;
	}
	auto file = QFile(StoragePath(instance->plugin.rootPath));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.write(QJsonDocument(instance->storage).toJson(
		QJsonDocument::Indented));
	instance->storageDirty = false;
	return true;
}

bool ScriptHost::injectApi(ScriptInstance *instance, QString *error) {
	auto *ctx = instance->engine.context();
	if (!ctx) {
		if (error) {
			*error = QStringLiteral("Missing JS context.");
		}
		return false;
	}
	const auto global = JS_GetGlobalObject(ctx);
	const auto pg = JS_NewObject(ctx);
	const auto ui = JS_NewObject(ctx);
	const auto lang = JS_NewObject(ctx);
	const auto storage = JS_NewObject(ctx);
	const auto plugin = JS_NewObject(ctx);

	const auto idUtf = instance->plugin.manifest.id.toUtf8();
	const auto nameUtf = instance->plugin.manifest.name.toUtf8();
	const auto verUtf = instance->plugin.manifest.version.toUtf8();
	JS_SetPropertyStr(
		ctx,
		plugin,
		"id",
		JS_NewString(ctx, idUtf.constData()));
	JS_SetPropertyStr(
		ctx,
		plugin,
		"name",
		JS_NewString(ctx, nameUtf.constData()));
	JS_SetPropertyStr(
		ctx,
		plugin,
		"version",
		JS_NewString(ctx, verUtf.constData()));

	SetMethod(ctx, ui, "toast", JsToast, 1);
	SetMethod(ctx, ui, "alert", JsAlert, 1);
	SetMethod(ctx, ui, "addPanel", JsAddPanel, 1);
	SetMethod(ctx, ui, "addButton", JsAddButton, 1);
	SetMethod(ctx, ui, "setOpacity", JsSetOpacity, 1);
	SetMethod(ctx, ui, "setColors", JsSetColors, 1);
	SetMethod(ctx, ui, "clearColors", JsClearColors, 0);
	SetMethod(ctx, ui, "setScale", JsSetScale, 1);
	SetMethod(ctx, ui, "setFont", JsSetFont, 1);
	SetMethod(ctx, ui, "setChrome", JsSetChrome, 1);
	SetMethod(ctx, lang, "get", JsLangGet, 0);
	SetMethod(ctx, lang, "set", JsLangSet, 1);
	SetMethod(ctx, storage, "get", JsStorageGet, 1);
	SetMethod(ctx, storage, "set", JsStorageSet, 2);
	SetMethod(ctx, storage, "remove", JsStorageRemove, 1);

	JS_SetPropertyStr(ctx, pg, "ui", ui);
	JS_SetPropertyStr(ctx, pg, "lang", lang);
	JS_SetPropertyStr(ctx, pg, "storage", storage);
	JS_SetPropertyStr(ctx, pg, "plugin", plugin);
	SetMethod(ctx, pg, "onAction", JsOnAction, 2);
	SetMethod(ctx, pg, "log", JsLog, 1);
	JS_SetPropertyStr(ctx, global, "plugingram", pg);
	JS_FreeValue(ctx, global);
	return true;
}

bool ScriptHost::start(const PluginDescriptor &plugin, QString *error) {
	if (!ManifestHasPermission(plugin.manifest, PluginPermission::ScriptRun)) {
		if (error) {
			*error = QStringLiteral("Missing script.run permission.");
		}
		return false;
	}
	auto scriptPath = plugin.scriptPath;
	if (scriptPath.isEmpty()) {
		scriptPath = ResolveScriptPath(plugin.rootPath);
	}
	if (scriptPath.isEmpty()
		|| !PathIsInsideRoot(plugin.rootPath, scriptPath)) {
		if (error) {
			*error = QStringLiteral("Plugin script was not found.");
		}
		return false;
	}

	unload(plugin.manifest.id);

	auto file = QFile(scriptPath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Could not read plugin script.");
		}
		return false;
	}
	if (file.size() > kMaxScriptBytes) {
		if (error) {
			*error = QStringLiteral("Plugin script is too large (max 256KB).");
		}
		return false;
	}
	const auto source = file.readAll();

	auto instance = std::make_unique<ScriptInstance>();
	instance->plugin = plugin;
	instance->plugin.scriptPath = scriptPath;
	if (!instance->engine.create(kMemoryLimitBytes)) {
		if (error) {
			*error = QStringLiteral("Failed to create JS runtime.");
		}
		return false;
	}
	loadStorage(instance.get());
	const auto id = plugin.manifest.id;
	auto *raw = instance.get();
	raw->engine.setOpaque(raw);
	_instances.emplace(id, std::move(instance));

	auto localError = QString();
	if (!injectApi(raw, error)
		|| !raw->engine.eval(
			source,
			QFileInfo(scriptPath).fileName(),
			error)
		|| !raw->engine.callGlobalFunction("onEnable", &localError)) {
		if (!localError.isEmpty() && error && error->isEmpty()) {
			*error = localError;
		}
		const auto reason = (error && !error->isEmpty())
			? *error
			: (!localError.isEmpty()
				? localError
				: QStringLiteral("Plugin failed while enabling."));
		unload(id);
		reportFault(id, reason);
		return false;
	}
	clearFault(id);
	return true;
}

void ScriptHost::reportFault(
		const QString &pluginId,
		const QString &reason) {
	if (pluginId.isEmpty() || _faulted.contains(pluginId)) {
		return;
	}
	_faulted.insert(pluginId);
	jsLog(pluginId, QStringLiteral("FAULT: ") + reason);
	if (_ui.onFault) {
		const auto fault = _ui.onFault;
		const auto text = reason.trimmed().isEmpty()
			? QStringLiteral("Unknown plugin error.")
			: reason.trimmed();
		// Leave current call stack before showing UI / disabling.
		QTimer::singleShot(1, [fault, pluginId, text] {
			fault(pluginId, text);
		});
	}
}

void ScriptHost::clearFault(const QString &pluginId) {
	_faulted.remove(pluginId);
}

void ScriptHost::scheduleUi(const QString &pluginId, FnMut<void()> task) {
	auto shared = std::make_shared<FnMut<void()>>(std::move(task));
	QTimer::singleShot(int(kUiDeferMs), [this, pluginId, shared] {
		if (_faulted.contains(pluginId) || !shared || !*shared) {
			return;
		}
		(*shared)();
	});
}

void ScriptHost::scheduleHeavyUi(
		const QString &pluginId,
		FnMut<void()> task) {
	auto shared = std::make_shared<FnMut<void()>>(std::move(task));
	QTimer::singleShot(int(kHeavyUiDeferMs), [this, pluginId, shared] {
		if (_faulted.contains(pluginId) || !shared || !*shared) {
			return;
		}
		if (_busyUi) {
			reportFault(
				pluginId,
				QStringLiteral(
					"UI is busy applying another plugin change. "
					"Try again later."));
			return;
		}
		_busyUi = true;
		try {
			(*shared)();
		} catch (...) {
			reportFault(
				pluginId,
				QStringLiteral(
					"Plugin UI change threw an unexpected error."));
		}
		_busyUi = false;
	});
}

void ScriptHost::dispose(const QString &pluginId, bool callOnDisable) {
	const auto it = _instances.find(pluginId);
	if (it == _instances.end()) {
		return;
	}
	auto *instance = it->second.get();
	if (callOnDisable) {
		// Allow onDisable restore calls even after a fault.
		clearFault(pluginId);
		QString ignored;
		instance->engine.callGlobalFunction("onDisable", &ignored);
	}
	saveStorage(instance);
	UiExtensionRegistry::Instance().unregisterPlugin(pluginId);
	_instances.erase(it);
}

void ScriptHost::stop(const QString &pluginId) {
	dispose(pluginId, true);
}

void ScriptHost::unload(const QString &pluginId) {
	dispose(pluginId, false);
}

void ScriptHost::stopAll() {
	auto ids = std::vector<QString>();
	ids.reserve(_instances.size());
	for (const auto &entry : _instances) {
		ids.push_back(entry.first);
	}
	for (const auto &id : ids) {
		stop(id);
	}
}

void ScriptHost::unloadAll() {
	auto ids = std::vector<QString>();
	ids.reserve(_instances.size());
	for (const auto &entry : _instances) {
		ids.push_back(entry.first);
	}
	for (const auto &id : ids) {
		unload(id);
	}
}

bool ScriptHost::invokeAction(
		const QString &pluginId,
		const QString &actionId,
		QString *error) {
	auto *instance = find(pluginId);
	if (!instance) {
		if (error) {
			*error = QStringLiteral("Plugin script is not running.");
		}
		return false;
	}
	if (_faulted.contains(pluginId)) {
		if (error) {
			*error = QStringLiteral("Plugin is faulted and will be disabled.");
		}
		return false;
	}
	auto localError = QString();
	if (!instance->engine.callStoredFunction(
			QStringLiteral("action:") + actionId,
			&localError)) {
		if (error) {
			*error = localError;
		}
		reportFault(
			pluginId,
			localError.isEmpty()
				? QStringLiteral("Action handler failed.")
				: localError);
		return false;
	}
	return true;
}

bool ScriptHost::hasPermission(
		const QString &pluginId,
		PluginPermission permission) const {
	const auto *instance = find(pluginId);
	return instance
		&& ManifestHasPermission(instance->plugin.manifest, permission);
}

void ScriptHost::jsAddPanel(
		const QString &pluginId,
		const UiPanelDescriptor &panel) {
	Q_UNUSED(pluginId);
	UiExtensionRegistry::Instance().registerPanels({ panel });
}

bool ScriptHost::jsAddButton(
		const QString &pluginId,
		const UiPanelDescriptor &panel) {
	if (!hasPermission(pluginId, PluginPermission::UiComposer)
		&& !(panel.placement == QStringLiteral("settings.sidebar")
			&& hasPermission(pluginId, PluginPermission::UiModify))) {
		return false;
	}
	auto copy = panel;
	if (copy.placement.isEmpty()) {
		copy.placement = QStringLiteral("chat.composer");
	}
	if (copy.placement == QStringLiteral("chat.composer")) {
		if (!hasPermission(pluginId, PluginPermission::UiComposer)) {
			return false;
		}
		auto composerCount = 0;
		for (const auto &existing
			: UiExtensionRegistry::Instance().panels()) {
			if (existing.placement == QStringLiteral("chat.composer")) {
				composerCount += std::max(1, int(existing.actions.size()));
			}
		}
		composerCount += std::max(1, int(copy.actions.size()));
		if (composerCount > kMaxComposerButtons) {
			jsLog(
				pluginId,
				QStringLiteral("addButton blocked: composer limit."));
			return false;
		}
	}
	UiExtensionRegistry::Instance().registerPanels({ std::move(copy) });
	return true;
}

void ScriptHost::jsToast(const QString &text) {
	const auto message = text.trimmed();
	if (message.isEmpty() || !_ui.toast) {
		return;
	}
	const auto toast = _ui.toast;
	crl::on_main([toast, message] {
		toast(message);
	});
}

void ScriptHost::jsAlert(const QString &title, const QString &text) {
	if (!_ui.alert) {
		return;
	}
	const auto boxTitle = title.trimmed().isEmpty()
		? QStringLiteral("Plugin")
		: title.trimmed();
	const auto boxText = text;
	const auto alert = _ui.alert;
	crl::on_main([alert, boxTitle, boxText] {
		alert(boxTitle, boxText);
	});
}

void ScriptHost::jsLog(const QString &pluginId, const QString &message) {
	qInfo("[plugingram:%s] %s",
		qUtf8Printable(pluginId),
		qUtf8Printable(message));
}

QString ScriptHost::jsStorageGet(
		const QString &pluginId,
		const QString &key) const {
	const auto *instance = find(pluginId);
	if (!instance || key.isEmpty() || !instance->storage.contains(key)) {
		return QString();
	}
	return instance->storage.value(key).toString();
}

void ScriptHost::jsStorageSet(
		const QString &pluginId,
		const QString &key,
		const QString &value) {
	auto *instance = find(pluginId);
	if (!instance || key.isEmpty()) {
		return;
	}
	instance->storage.insert(key, value);
	instance->storageDirty = true;
	saveStorage(instance);
}

void ScriptHost::jsStorageRemove(
		const QString &pluginId,
		const QString &key) {
	auto *instance = find(pluginId);
	if (!instance || key.isEmpty()) {
		return;
	}
	instance->storage.remove(key);
	instance->storageDirty = true;
	saveStorage(instance);
}

void ScriptHost::jsOnAction(
		const QString &pluginId,
		const QString &actionId,
		void *jsFunctionValue) {
	auto *instance = find(pluginId);
	if (!instance || !jsFunctionValue || actionId.isEmpty()) {
		return;
	}
	instance->engine.storeFunction(
		QStringLiteral("action:") + actionId,
		jsFunctionValue);
}

QString ScriptHost::jsLangGet() const {
	if (!_ui.langGet) {
		return QString();
	}
	return _ui.langGet();
}

bool ScriptHost::jsLangSet(const QString &pluginId, const QString &langId) {
	auto *instance = find(pluginId);
	if (!instance
		|| !_ui.langSet
		|| !ManifestHasPermission(
			instance->plugin.manifest,
			PluginPermission::SettingsReadWrite)) {
		return false;
	}
	const auto id = langId.trimmed();
	if (id.isEmpty()) {
		return false;
	}
	if (_langSetCount >= kMaxLangSetsPerProcess) {
		jsLog(
			pluginId,
			QStringLiteral("lang.set blocked: process limit reached."));
		return false;
	}
	if (_ui.langGet && _ui.langGet() == id) {
		return true;
	}
	_lastLangSetAt = crl::now();
	++_langSetCount;
	const auto set = _ui.langSet;
	crl::on_main([set, id] {
		set(id);
	});
	return true;
}

bool ScriptHost::jsSetOpacity(const QString &pluginId, float64 opacity) {
	if (!hasPermission(pluginId, PluginPermission::UiOpacity)
		|| !_ui.setOpacity) {
		jsLog(pluginId, QStringLiteral("setOpacity: missing ui.opacity"));
		return false;
	}
	if (!std::isfinite(opacity)) {
		reportFault(
			pluginId,
			QStringLiteral("setOpacity got an invalid number."));
		return false;
	}
	opacity = std::clamp(opacity, kMinOpacity, kMaxOpacity);
	const auto set = _ui.setOpacity;
	scheduleUi(pluginId, [set, opacity, pluginId, this] {
		if (!set(opacity)) {
			reportFault(
				pluginId,
				QStringLiteral("Failed to change window opacity."));
		}
	});
	return true;
}

bool ScriptHost::jsSetColors(
		const QString &pluginId,
		const QJsonObject &palette) {
	auto *instance = find(pluginId);
	if (!instance
		|| !hasPermission(pluginId, PluginPermission::UiTheme)
		|| !_ui.applyColors
		|| palette.isEmpty()) {
		jsLog(pluginId, QStringLiteral("setColors: missing ui.theme/colors"));
		return false;
	}
	const auto root = instance->plugin.rootPath;
	const auto apply = _ui.applyColors;
	scheduleHeavyUi(pluginId, [apply, palette, root, pluginId, this] {
		if (!apply(palette, root)) {
			reportFault(
				pluginId,
				QStringLiteral(
					"Could not apply colors (invalid palette or theme engine)."));
		}
	});
	return true;
}

bool ScriptHost::jsClearColors(const QString &pluginId) {
	if (!hasPermission(pluginId, PluginPermission::UiTheme)
		|| !_ui.clearColors) {
		return false;
	}
	const auto clear = _ui.clearColors;
	// Clearing during disable must not block on fault set.
	QTimer::singleShot(int(kHeavyUiDeferMs), [clear] {
		clear();
	});
	return true;
}

bool ScriptHost::jsSetScale(const QString &pluginId, int scale) {
	if (!hasPermission(pluginId, PluginPermission::UiScale)
		|| !_ui.setScale) {
		jsLog(pluginId, QStringLiteral("setScale: missing ui.scale"));
		return false;
	}
	scale = std::clamp(scale, kMinScale, kMaxScale);
	const auto set = _ui.setScale;
	scheduleUi(pluginId, [set, scale] {
		set(scale);
	});
	return true;
}

bool ScriptHost::jsSetFont(const QString &pluginId, const QString &family) {
	if (!hasPermission(pluginId, PluginPermission::UiScale)
		|| !_ui.setFont) {
		jsLog(pluginId, QStringLiteral("setFont: missing ui.scale"));
		return false;
	}
	auto cleaned = family.trimmed();
	if (cleaned.size() > kMaxFontFamilyLen) {
		cleaned = cleaned.left(kMaxFontFamilyLen);
	}
	static const auto re = QRegularExpression(
		QStringLiteral("^[\\w \\-\\.]+$"));
	if (!cleaned.isEmpty() && !re.match(cleaned).hasMatch()) {
		reportFault(
			pluginId,
			QStringLiteral("setFont: unsupported font family name."));
		return false;
	}
	const auto set = _ui.setFont;
	scheduleUi(pluginId, [set, cleaned] {
		set(cleaned);
	});
	return true;
}

bool ScriptHost::jsSetChrome(
		const QString &pluginId,
		UiChromeFlags flags) {
	if (!hasPermission(pluginId, PluginPermission::UiChrome)
		|| !_ui.setChrome) {
		jsLog(pluginId, QStringLiteral("setChrome: missing ui.chrome"));
		return false;
	}
	const auto set = _ui.setChrome;
	scheduleUi(pluginId, [set, flags, pluginId, this] {
		if (!set(flags)) {
			reportFault(
				pluginId,
				QStringLiteral("Failed to update chrome visibility."));
		}
	});
	return true;
}

} // namespace PluginSystem
