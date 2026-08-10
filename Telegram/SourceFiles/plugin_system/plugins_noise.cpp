#include "plugin_system/plugins_noise.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace PluginSystem {
namespace {

NoisePluginHooks &Hooks() {
	static NoisePluginHooks hooks;
	return hooks;
}

[[nodiscard]] QByteArray ManifestJson() {
	const auto object = QJsonObject{
		{ QStringLiteral("id"), QString::fromUtf8(kNoisePluginId) },
		{ QStringLiteral("name"), QStringLiteral("Noise") },
		{ QStringLiteral("version"), QStringLiteral("1.0.0") },
		{ QStringLiteral("author"), QStringLiteral("Plugingram") },
		{
			QStringLiteral("description"),
			QStringLiteral(
				"Скрывает телефоны и коды в профилях шумом спойлера. "
				"Клик — показать."),
		},
		{ QStringLiteral("type"), QStringLiteral("utility") },
		{ QStringLiteral("icon"), QStringLiteral("icon.png") },
		{
			QStringLiteral("permissions"),
			QJsonArray{ QStringLiteral("commands.register") },
		},
	};
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

} // namespace

void SetNoisePluginHooks(NoisePluginHooks hooks) {
	Hooks() = std::move(hooks);
}

void EnsureBundledNoisePlugin(const QString &pluginsRoot) {
	if (pluginsRoot.isEmpty()) {
		return;
	}

	// Drop the old phone-blur folder if it was seeded earlier.
	const auto legacy = pluginsRoot + QStringLiteral("/phone-blur");
	if (QDir(legacy).exists()) {
		QDir(legacy).removeRecursively();
	}

	const auto root = pluginsRoot
		+ QStringLiteral("/")
		+ QString::fromUtf8(kNoisePluginId);
	QDir().mkpath(root);

	const auto manifestPath = root + QStringLiteral("/plugingram.json");
	{
		auto file = QFile(manifestPath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(ManifestJson());
		}
	}

	const auto iconPath = root + QStringLiteral("/icon.png");
	if (Hooks().writeSpoilerIcon) {
		Hooks().writeSpoilerIcon(iconPath);
	}
}

void ApplyNoisePluginState(bool enabled) {
	if (Hooks().apply) {
		Hooks().apply(enabled);
	}
}

bool DefaultNoisePluginEnabled() {
	if (Hooks().readSessionBlur) {
		return Hooks().readSessionBlur();
	}
	return true;
}

} // namespace PluginSystem
