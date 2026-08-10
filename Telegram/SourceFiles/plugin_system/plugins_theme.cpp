#include "plugin_system/plugins_theme.h"

#include "plugin_system/plugins_common.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>

namespace PluginSystem {
namespace {

ThemeEngineHooks &Hooks() {
	static ThemeEngineHooks hooks;
	return hooks;
}

[[nodiscard]] QString MapSimpleKey(const QString &key) {
	static const auto kMap = QMap<QString, QString>{
		{ QStringLiteral("accent"), QStringLiteral("windowBgActive") },
		{ QStringLiteral("background"), QStringLiteral("windowBg") },
		{ QStringLiteral("surface"), QStringLiteral("dialogsBg") },
		{ QStringLiteral("text"), QStringLiteral("windowFg") },
		{ QStringLiteral("windowBg"), QStringLiteral("windowBg") },
		{ QStringLiteral("windowFg"), QStringLiteral("windowFg") },
		{ QStringLiteral("windowBgActive"), QStringLiteral("windowBgActive") },
		{ QStringLiteral("dialogsBg"), QStringLiteral("dialogsBg") },
		{ QStringLiteral("dialogsNameFg"), QStringLiteral("dialogsNameFg") },
		{ QStringLiteral("msgInBg"), QStringLiteral("msgInBg") },
		{ QStringLiteral("msgOutBg"), QStringLiteral("msgOutBg") },
		{ QStringLiteral("historyComposeAreaBg"), QStringLiteral("historyComposeAreaBg") },
		{ QStringLiteral("menuIconFg"), QStringLiteral("menuIconFg") },
		{ QStringLiteral("activeButtonBg"), QStringLiteral("activeButtonBg") },
		{ QStringLiteral("activeButtonFg"), QStringLiteral("activeButtonFg") },
	};
	return kMap.value(key, key);
}

[[nodiscard]] QString NormalizeColor(QString value) {
	value = value.trimmed();
	if (value.startsWith(QChar('#'))) {
		return value;
	}
	if (value.size() == 6 || value.size() == 8) {
		return QChar('#') + value;
	}
	return value;
}

[[nodiscard]] bool ApplyGeneratedPath(const QString &path) {
	if (!Hooks().applyPath || path.isEmpty()) {
		return false;
	}
	if (!Hooks().applyPath(path)) {
		return false;
	}
	if (Hooks().keepApplied) {
		Hooks().keepApplied();
	}
	return true;
}

} // namespace

void SetThemeEngineHooks(ThemeEngineHooks hooks) {
	Hooks() = std::move(hooks);
}

QString ResolveThemeEntryPath(const PluginDescriptor &plugin) {
	if (!plugin.manifest.entry.isEmpty()) {
		const auto entry = plugin.rootPath
			+ QStringLiteral("/")
			+ plugin.manifest.entry;
		if (PathIsInsideRoot(plugin.rootPath, entry)
			&& QFileInfo(entry).isFile()) {
			return entry;
		}
	}
	const auto candidates = {
		QStringLiteral("/theme.json"),
		QStringLiteral("/theme.tdesktop-palette"),
		QStringLiteral("/theme.tdesktop-theme"),
		QStringLiteral("/colors.tdesktop-palette"),
		QStringLiteral("/plugin.json"),
	};
	for (const auto &suffix : candidates) {
		const auto path = plugin.rootPath + suffix;
		if (PathIsInsideRoot(plugin.rootPath, path)
			&& QFileInfo(path).isFile()) {
			return path;
		}
	}
	return {};
}

QByteArray BuildPaletteFromObject(const QJsonObject &palette) {
	if (palette.isEmpty()) {
		return {};
	}

	auto lines = QByteArray();
	lines.append("// Plugingram generated palette\n");
	auto count = 0;
	for (auto i = palette.begin(); i != palette.end(); ++i) {
		if (!i.value().isString()) {
			continue;
		}
		if (++count > 64) {
			break;
		}
		const auto tgKey = MapSimpleKey(i.key());
		const auto color = NormalizeColor(i.value().toString());
		if (!color.startsWith(QChar('#'))) {
			continue;
		}
		lines.append(tgKey.toUtf8());
		lines.append(": ");
		lines.append(color.toUtf8());
		lines.append(";\n");
	}
	if (count <= 0) {
		return {};
	}

	const auto ensure = [&](const char *key, const char *value) {
		if (!lines.contains(key)) {
			lines.append(key);
			lines.append(": ");
			lines.append(value);
			lines.append(";\n");
		}
	};
	ensure("windowBg", "#10141F");
	ensure("windowFg", "#F2F5FF");
	ensure("windowBgActive", "#4F7FFF");
	ensure("dialogsBg", "#182033");
	ensure("dialogsNameFg", "#F2F5FF");
	ensure("msgInBg", "#1E2A40");
	ensure("msgOutBg", "#24375A");
	ensure("historyComposeAreaBg", "#182033");
	ensure("menuIconFg", "#A8B3C7");
	ensure("activeButtonBg", "#4F7FFF");
	ensure("activeButtonFg", "#FFFFFF");
	return lines;
}

QByteArray BuildPaletteFromThemeJson(const QString &jsonPath) {
	auto file = QFile(jsonPath);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return {};
	}
	const auto root = document.object();
	const auto paletteValue = root.value(QStringLiteral("palette"));
	const auto palette = paletteValue.isObject()
		? paletteValue.toObject()
		: root;
	return BuildPaletteFromObject(palette);
}

bool ApplyPaletteObject(
		const QJsonObject &palette,
		const QString &pluginRoot) {
	if (pluginRoot.isEmpty()) {
		return false;
	}
	const auto content = BuildPaletteFromObject(palette);
	if (content.isEmpty()) {
		return false;
	}
	const auto generated = pluginRoot
		+ QStringLiteral("/.generated.tdesktop-palette");
	if (!PathIsInsideRoot(pluginRoot, generated)) {
		return false;
	}
	{
		auto out = QFile(generated);
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return false;
		}
		out.write(content);
	}
	return ApplyGeneratedPath(generated);
}

bool ApplyThemePlugin(const PluginDescriptor &plugin) {
	const auto path = ResolveThemeEntryPath(plugin);
	if (path.isEmpty() || !QFileInfo(path).isFile()) {
		return false;
	}

	if (path.endsWith(QStringLiteral(".tdesktop-theme"), Qt::CaseInsensitive)
		|| path.endsWith(
			QStringLiteral(".tdesktop-palette"),
			Qt::CaseInsensitive)) {
		return ApplyGeneratedPath(path);
	}

	if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
		const auto content = BuildPaletteFromThemeJson(path);
		if (content.isEmpty()) {
			return false;
		}
		const auto generated = plugin.rootPath
			+ QStringLiteral("/.generated.tdesktop-palette");
		{
			auto out = QFile(generated);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				return false;
			}
			out.write(content);
		}
		return ApplyGeneratedPath(generated);
	}

	return false;
}

void UnapplyThemePlugin() {
	if (Hooks().applyDefault) {
		Hooks().applyDefault();
	}
	if (Hooks().keepApplied) {
		Hooks().keepApplied();
	}
}

} // namespace PluginSystem
