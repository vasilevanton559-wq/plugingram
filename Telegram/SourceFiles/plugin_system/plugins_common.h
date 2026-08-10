#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

namespace PluginSystem {

enum class PluginType {
	Unknown,
	Theme,
	UiExtension,
	Utility,
};

enum class PluginPermission {
	UiModify,
	CommandsRegister,
	SettingsReadWrite,
	StoreBrowse,
	ScriptRun,
	UiTheme,
	UiOpacity,
	UiScale,
	UiChrome,
	UiComposer,
};

struct UiChromeFlags {
	bool filters = true;
	bool mainMenu = true;
	bool search = true;
};

enum class PluginState {
	Disabled,
	Enabled,
	Error,
};

struct PluginCommand {
	QString id;
	QString title;
	QString description;
};

struct PluginManifest {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	PluginType type = PluginType::Unknown;
	QString entry;
	QStringList tags;
	std::vector<PluginPermission> permissions;
	std::vector<PluginCommand> commands;
};

struct UiPanelDescriptor {
	QString id;
	QString title;
	QString placement;
	QString pluginId;
	QString pluginName;
	QString description;
	std::vector<PluginCommand> actions;
};

struct PluginDescriptor {
	PluginManifest manifest;
	QString rootPath;
	QString manifestPath;
	QString iconPath;
	QString scriptPath;
	PluginState state = PluginState::Disabled;
	QString error;
	bool bundled = false;
	bool favorite = false;
};

struct RemotePluginEntry {
	QString id;
	QString name;
	QString version;
	QString author;
	QString description;
	PluginType type = PluginType::Unknown;
	QString minApiVersion;
	QString packageUrl;
	QString repoUrl;
	QString icon; // relative path in package or https GitHub URL
	QStringList tags;
	std::vector<PluginPermission> permissions;
};

struct RemotePluginIndex {
	QString schemaVersion;
	QString catalogTitle;
	std::vector<RemotePluginEntry> entries;
};

struct HostCapabilities {
	QString apiVersion;
	QString contract;
	QStringList supportedModuleTypes;
	std::vector<PluginPermission> supportedPermissions;
};

[[nodiscard]] PluginType ParsePluginType(const QString &value);
[[nodiscard]] QString SerializePluginType(PluginType type);

[[nodiscard]] bool ParsePluginPermission(
	const QString &value,
	PluginPermission *result);
[[nodiscard]] QString SerializePluginPermission(PluginPermission permission);
[[nodiscard]] bool ManifestHasPermission(
	const PluginManifest &manifest,
	PluginPermission permission);
[[nodiscard]] bool PathIsInsideRoot(const QString &root, const QString &path);

} // namespace PluginSystem
