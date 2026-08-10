#pragma once

#include "plugin_system/plugins_common.h"

#include <QtCore/QJsonObject>

#include "base/basic_types.h"

namespace PluginSystem {

struct ThemeEngineHooks {
	Fn<bool(QString /*paletteOrThemePath*/)> applyPath;
	Fn<void()> applyDefault;
	Fn<void()> keepApplied;
};

void SetThemeEngineHooks(ThemeEngineHooks hooks);

[[nodiscard]] bool ApplyThemePlugin(const PluginDescriptor &plugin);
void UnapplyThemePlugin();

[[nodiscard]] QString ResolveThemeEntryPath(const PluginDescriptor &plugin);
[[nodiscard]] QByteArray BuildPaletteFromThemeJson(const QString &jsonPath);
[[nodiscard]] QByteArray BuildPaletteFromObject(const QJsonObject &palette);
[[nodiscard]] bool ApplyPaletteObject(
	const QJsonObject &palette,
	const QString &pluginRoot);

} // namespace PluginSystem
