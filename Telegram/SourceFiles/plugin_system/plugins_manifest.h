#pragma once

#include "plugin_system/plugins_common.h"

#include <optional>

namespace PluginSystem {

struct ManifestParseResult {
	std::optional<PluginManifest> manifest;
	QString error;
};

[[nodiscard]] ManifestParseResult ParseManifestFile(const QString &path);

// Discover a plugin from a folder:
// 1) manifest.json (full)
// 2) plugin.json (minimal / one-file)
// 3) well-known files: theme.json, ui.json, utility.json, palette/theme packs
[[nodiscard]] ManifestParseResult DiscoverPluginManifest(
	const QString &rootPath,
	const QString &folderName);

[[nodiscard]] QString ResolveUiEntryPath(const PluginDescriptor &plugin);
[[nodiscard]] QString DetectThemeEntryFile(const QString &rootPath);
[[nodiscard]] QString DetectUiEntryFile(const QString &rootPath);
[[nodiscard]] QString DetectUtilityEntryFile(const QString &rootPath);

} // namespace PluginSystem
