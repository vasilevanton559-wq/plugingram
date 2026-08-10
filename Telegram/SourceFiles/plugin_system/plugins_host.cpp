#include "plugin_system/plugins_host.h"

#include "plugin_system/plugins_common.h"
#include "plugin_system/plugins_manager.h"
#include "plugin_system/plugins_script_host.h"
#include "plugin_system/plugins_theme.h"
#include "plugin_system/plugins_ui_extension.h"

#include <vector>

namespace PluginSystem {
namespace {

void StartScriptIfNeeded(const PluginDescriptor &plugin) {
	if (plugin.scriptPath.isEmpty()
		&& ResolveScriptPath(plugin.rootPath).isEmpty()) {
		return;
	}
	if (!ManifestHasPermission(
			plugin.manifest,
			PluginPermission::ScriptRun)) {
		return;
	}
	auto error = QString();
	if (!ScriptHost::Instance().start(plugin, &error)) {
		qWarning(
			"[plugingram] script start failed for %s: %s",
			qUtf8Printable(plugin.manifest.id),
			qUtf8Printable(error));
	}
}

} // namespace

Host::Host(not_null<Manager*> manager)
: _manager(manager) {
}

void Host::rebuildUiExtensions() {
	auto &registry = UiExtensionRegistry::Instance();
	registry.clear();
	auto runningIds = std::vector<QString>();
	for (const auto &plugin : _manager->plugins()) {
		if (plugin.state == PluginState::Enabled
			&& ScriptHost::Instance().isRunning(plugin.manifest.id)) {
			runningIds.push_back(plugin.manifest.id);
		}
	}
	for (const auto &plugin : _manager->plugins()) {
		if (plugin.state != PluginState::Enabled
			|| plugin.manifest.type != PluginType::UiExtension
			|| !ManifestHasPermission(
				plugin.manifest,
				PluginPermission::UiModify)) {
			continue;
		}
		registry.registerPanels(ParseUiExtensionPanels(plugin));
	}
	// Registry was cleared — restart running scripts so JS panels return.
	// start() uses unload (not stop), so onDisable is NOT called here.
	for (const auto &id : runningIds) {
		if (const auto *plugin = _manager->findById(id)) {
			StartScriptIfNeeded(*plugin);
		}
	}
}

bool Host::applyPlugin(const PluginDescriptor &plugin) {
	if (plugin.state == PluginState::Error) {
		return false;
	}
	switch (plugin.manifest.type) {
	case PluginType::Theme:
		if (!ManifestHasPermission(
				plugin.manifest,
				PluginPermission::UiModify)) {
			return false;
		}
		if (!ApplyThemePlugin(plugin)) {
			return false;
		}
		StartScriptIfNeeded(plugin);
		return true;
	case PluginType::UiExtension:
		if (!ManifestHasPermission(
				plugin.manifest,
				PluginPermission::UiModify)) {
			return false;
		}
		UiExtensionRegistry::Instance().registerPanels(
			ParseUiExtensionPanels(plugin));
		StartScriptIfNeeded(plugin);
		return true;
	case PluginType::Utility:
		if (!ManifestHasPermission(
				plugin.manifest,
				PluginPermission::CommandsRegister)) {
			return false;
		}
		StartScriptIfNeeded(plugin);
		return true;
	case PluginType::Unknown:
		break;
	}
	return false;
}

bool Host::unapplyPlugin(const PluginDescriptor &plugin) {
	ScriptHost::Instance().stop(plugin.manifest.id);
	switch (plugin.manifest.type) {
	case PluginType::Theme:
		UnapplyThemePlugin();
		return true;
	case PluginType::UiExtension:
		UiExtensionRegistry::Instance().unregisterPlugin(plugin.manifest.id);
		return true;
	case PluginType::Utility:
		return true;
	case PluginType::Unknown:
		break;
	}
	return false;
}

void Host::applyEnabledPlugins() {
	// Unload without onDisable — otherwise every refresh restores plugin
	// side-effects (e.g. language) and onEnable re-applies them in a loop.
	ScriptHost::Instance().unloadAll();
	rebuildUiExtensions();
	for (const auto &plugin : _manager->plugins()) {
		if (plugin.state != PluginState::Enabled) {
			continue;
		}
		if (plugin.manifest.type == PluginType::Theme
			&& ManifestHasPermission(
				plugin.manifest,
				PluginPermission::UiModify)) {
			ApplyThemePlugin(plugin);
			StartScriptIfNeeded(plugin);
			break; // Only one theme active.
		}
	}
	for (const auto &plugin : _manager->plugins()) {
		if (plugin.state != PluginState::Enabled) {
			continue;
		}
		if (plugin.manifest.type == PluginType::Theme) {
			continue;
		}
		applyPlugin(plugin);
	}
}

} // namespace PluginSystem
