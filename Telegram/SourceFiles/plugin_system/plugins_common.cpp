#include "plugin_system/plugins_common.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace PluginSystem {

PluginType ParsePluginType(const QString &value) {
	if (value == QStringLiteral("theme")) {
		return PluginType::Theme;
	} else if (value == QStringLiteral("ui_extension")) {
		return PluginType::UiExtension;
	} else if (value == QStringLiteral("utility")) {
		return PluginType::Utility;
	}
	return PluginType::Unknown;
}

QString SerializePluginType(PluginType type) {
	switch (type) {
	case PluginType::Theme: return QStringLiteral("theme");
	case PluginType::UiExtension: return QStringLiteral("ui_extension");
	case PluginType::Utility: return QStringLiteral("utility");
	case PluginType::Unknown: break;
	}
	return QStringLiteral("unknown");
}

bool ParsePluginPermission(
		const QString &value,
		PluginPermission *result) {
	if (value == QStringLiteral("ui.modify")) {
		*result = PluginPermission::UiModify;
		return true;
	} else if (value == QStringLiteral("commands.register")) {
		*result = PluginPermission::CommandsRegister;
		return true;
	} else if (value == QStringLiteral("settings.readwrite")) {
		*result = PluginPermission::SettingsReadWrite;
		return true;
	} else if (value == QStringLiteral("store.browse")) {
		*result = PluginPermission::StoreBrowse;
		return true;
	} else if (value == QStringLiteral("script.run")) {
		*result = PluginPermission::ScriptRun;
		return true;
	} else if (value == QStringLiteral("ui.theme")) {
		*result = PluginPermission::UiTheme;
		return true;
	} else if (value == QStringLiteral("ui.opacity")) {
		*result = PluginPermission::UiOpacity;
		return true;
	} else if (value == QStringLiteral("ui.scale")) {
		*result = PluginPermission::UiScale;
		return true;
	} else if (value == QStringLiteral("ui.chrome")) {
		*result = PluginPermission::UiChrome;
		return true;
	} else if (value == QStringLiteral("ui.composer")) {
		*result = PluginPermission::UiComposer;
		return true;
	}
	return false;
}

QString SerializePluginPermission(PluginPermission permission) {
	switch (permission) {
	case PluginPermission::UiModify:
		return QStringLiteral("ui.modify");
	case PluginPermission::CommandsRegister:
		return QStringLiteral("commands.register");
	case PluginPermission::SettingsReadWrite:
		return QStringLiteral("settings.readwrite");
	case PluginPermission::StoreBrowse:
		return QStringLiteral("store.browse");
	case PluginPermission::ScriptRun:
		return QStringLiteral("script.run");
	case PluginPermission::UiTheme:
		return QStringLiteral("ui.theme");
	case PluginPermission::UiOpacity:
		return QStringLiteral("ui.opacity");
	case PluginPermission::UiScale:
		return QStringLiteral("ui.scale");
	case PluginPermission::UiChrome:
		return QStringLiteral("ui.chrome");
	case PluginPermission::UiComposer:
		return QStringLiteral("ui.composer");
	}
	return QStringLiteral("unknown");
}

bool ManifestHasPermission(
		const PluginManifest &manifest,
		PluginPermission permission) {
	for (const auto item : manifest.permissions) {
		if (item == permission) {
			return true;
		}
	}
	return false;
}

bool PathIsInsideRoot(const QString &root, const QString &path) {
	if (root.isEmpty() || path.isEmpty()) {
		return false;
	}
	const auto rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
	const auto pathAbs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	if (pathAbs.isEmpty() || rootAbs.isEmpty()) {
		return false;
	}
#if defined(Q_OS_WIN)
	const auto cmp = Qt::CaseInsensitive;
#else
	const auto cmp = Qt::CaseSensitive;
#endif
	if (pathAbs.compare(rootAbs, cmp) == 0) {
		return true;
	}
	const auto prefix = rootAbs.endsWith(QChar('/'))
		? rootAbs
		: (rootAbs + QChar('/'));
	return pathAbs.startsWith(prefix, cmp);
}

} // namespace PluginSystem
