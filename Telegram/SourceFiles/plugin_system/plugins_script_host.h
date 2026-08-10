#pragma once

#include "base/basic_types.h"
#include "plugin_system/plugins_common.h"
#include "plugin_system/plugins_script_engine.h"

#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <crl/crl_time.h>

#include <memory>
#include <map>

namespace PluginSystem {

struct ScriptInstance {
	PluginDescriptor plugin;
	ScriptEngine engine;
	QJsonObject storage;
	bool storageDirty = false;
};

struct ScriptUiHooks {
	Fn<void(QString)> toast;
	Fn<void(QString /*title*/, QString /*text*/)> alert;
	Fn<QString()> langGet;
	Fn<bool(QString /*langId*/)> langSet;
	Fn<bool(float64 /*opacity*/)> setOpacity;
	Fn<bool(QJsonObject /*palette*/, QString /*pluginRoot*/)> applyColors;
	Fn<bool()> clearColors;
	Fn<bool(int /*scale*/)> setScale;
	Fn<bool(QString /*family*/)> setFont;
	Fn<bool(UiChromeFlags)> setChrome;
	// Show reason and schedule safe disable (host must not crash).
	Fn<void(QString /*pluginId*/, QString /*reason*/)> onFault;
};

class ScriptHost final {
public:
	[[nodiscard]] static ScriptHost &Instance();

	void setUiHooks(ScriptUiHooks hooks);

	[[nodiscard]] bool hasScript(const PluginDescriptor &plugin) const;
	[[nodiscard]] bool isRunning(const QString &pluginId) const;

	bool start(const PluginDescriptor &plugin, QString *error = nullptr);
	// User disable / uninstall: runs onDisable, then tears down.
	void stop(const QString &pluginId);
	void stopAll();
	// Internal reload: tear down without onDisable (avoids enable/disable loops).
	void unload(const QString &pluginId);
	void unloadAll();

	bool invokeAction(
		const QString &pluginId,
		const QString &actionId,
		QString *error = nullptr);

	void reportFault(const QString &pluginId, const QString &reason);
	void clearFault(const QString &pluginId);

	void jsAddPanel(const QString &pluginId, const UiPanelDescriptor &panel);
	void jsToast(const QString &text);
	void jsAlert(const QString &title, const QString &text);
	void jsLog(const QString &pluginId, const QString &message);
	[[nodiscard]] QString jsStorageGet(
		const QString &pluginId,
		const QString &key) const;
	void jsStorageSet(
		const QString &pluginId,
		const QString &key,
		const QString &value);
	void jsStorageRemove(const QString &pluginId, const QString &key);
	void jsOnAction(
		const QString &pluginId,
		const QString &actionId,
		void *jsFunctionValue);
	[[nodiscard]] QString jsLangGet() const;
	bool jsLangSet(const QString &pluginId, const QString &langId);
	bool jsSetOpacity(const QString &pluginId, float64 opacity);
	bool jsSetColors(const QString &pluginId, const QJsonObject &palette);
	bool jsClearColors(const QString &pluginId);
	bool jsSetScale(const QString &pluginId, int scale);
	bool jsSetFont(const QString &pluginId, const QString &family);
	bool jsSetChrome(const QString &pluginId, UiChromeFlags flags);
	bool jsAddButton(const QString &pluginId, const UiPanelDescriptor &panel);

private:
	ScriptHost() = default;

	[[nodiscard]] ScriptInstance *find(const QString &pluginId);
	[[nodiscard]] const ScriptInstance *find(const QString &pluginId) const;
	bool loadStorage(ScriptInstance *instance);
	bool saveStorage(ScriptInstance *instance) const;
	bool injectApi(ScriptInstance *instance, QString *error);
	[[nodiscard]] static QString StoragePath(const QString &rootPath);
	[[nodiscard]] bool hasPermission(
		const QString &pluginId,
		PluginPermission permission) const;
	void scheduleUi(const QString &pluginId, FnMut<void()> task);
	void scheduleHeavyUi(const QString &pluginId, FnMut<void()> task);

	void dispose(const QString &pluginId, bool callOnDisable);

	std::map<QString, std::unique_ptr<ScriptInstance>> _instances;
	ScriptUiHooks _ui;
	crl::time _lastLangSetAt = 0;
	int _langSetCount = 0;
	QSet<QString> _faulted;
	bool _busyUi = false;
};

[[nodiscard]] QString ResolveScriptPath(const QString &rootPath);

} // namespace PluginSystem
