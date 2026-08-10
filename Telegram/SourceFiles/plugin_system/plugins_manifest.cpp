#include "plugin_system/plugins_manifest.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace PluginSystem {
namespace {

[[nodiscard]] QString RequireString(
		const QJsonObject &object,
		const char *key,
		QString *error) {
	const auto i = object.find(QLatin1String(key));
	if (i == object.end() || !i->isString() || i->toString().isEmpty()) {
		*error = QStringLiteral("Missing required string field: %1")
			.arg(QLatin1String(key));
		return QString();
	}
	return i->toString();
}

[[nodiscard]] bool FileExists(const QString &root, const QString &name) {
	return QFileInfo::exists(root + QStringLiteral("/") + name);
}

[[nodiscard]] QString HumanizeFolderName(QString name) {
	name.replace(QRegularExpression(QStringLiteral("[-_]+")), QStringLiteral(" "));
	name = name.simplified();
	if (name.isEmpty()) {
		return QStringLiteral("Plugin");
	}
	auto parts = name.split(QChar(' '), Qt::SkipEmptyParts);
	for (auto &part : parts) {
		part = part.toLower();
		if (!part.isEmpty()) {
			part[0] = part[0].toUpper();
		}
	}
	return parts.join(QChar(' '));
}

[[nodiscard]] QString SanitizeId(QString value) {
	value = value.trimmed().toLower();
	value.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
	value.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
	while (value.startsWith(QChar('-')) || value.startsWith(QChar('.'))) {
		value = value.mid(1);
	}
	while (value.endsWith(QChar('-')) || value.endsWith(QChar('.'))) {
		value.chop(1);
	}
	return value.isEmpty() ? QStringLiteral("plugin") : value;
}

void FillDefaults(
		PluginManifest &manifest,
		const QString &folderName) {
	if (manifest.id.isEmpty()) {
		manifest.id = SanitizeId(folderName);
	}
	if (manifest.name.isEmpty()) {
		manifest.name = HumanizeFolderName(folderName);
	}
	if (manifest.version.isEmpty()) {
		manifest.version = QStringLiteral("1.0.0");
	}
	if (manifest.description.isEmpty()) {
		switch (manifest.type) {
		case PluginType::Theme:
			manifest.description = QStringLiteral("Theme plugin");
			break;
		case PluginType::UiExtension:
			manifest.description = QStringLiteral("UI extension plugin");
			break;
		case PluginType::Utility:
			manifest.description = QStringLiteral("Utility plugin");
			break;
		case PluginType::Unknown:
			break;
		}
	}
	if (manifest.permissions.empty()) {
		switch (manifest.type) {
		case PluginType::Theme:
			manifest.permissions.push_back(PluginPermission::UiModify);
			break;
		case PluginType::UiExtension:
			manifest.permissions.push_back(PluginPermission::UiModify);
			manifest.permissions.push_back(PluginPermission::CommandsRegister);
			break;
		case PluginType::Utility:
			manifest.permissions.push_back(PluginPermission::CommandsRegister);
			break;
		case PluginType::Unknown:
			break;
		}
	}
}

void FillEntryFromDisk(PluginManifest &manifest, const QString &rootPath) {
	if (!manifest.entry.isEmpty()) {
		return;
	}
	switch (manifest.type) {
	case PluginType::Theme:
		manifest.entry = DetectThemeEntryFile(rootPath);
		break;
	case PluginType::UiExtension:
		manifest.entry = DetectUiEntryFile(rootPath);
		break;
	case PluginType::Utility:
		manifest.entry = DetectUtilityEntryFile(rootPath);
		break;
	case PluginType::Unknown:
		break;
	}
}

void ParsePermissionsInto(
		PluginManifest &manifest,
		const QJsonObject &object) {
	const auto permissions = object.value(QStringLiteral("permissions"));
	if (!permissions.isArray()) {
		return;
	}
	for (const auto &permission : permissions.toArray()) {
		if (!permission.isString()) {
			continue;
		}
		auto parsed = PluginPermission::UiModify;
		if (!ParsePluginPermission(permission.toString(), &parsed)) {
			continue;
		}
		if (!ManifestHasPermission(manifest, parsed)) {
			manifest.permissions.push_back(parsed);
		}
	}
}

[[nodiscard]] std::vector<PluginCommand> ParseCommandsArray(
		const QJsonArray &commands) {
	auto result = std::vector<PluginCommand>();
	for (const auto &value : commands) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		auto command = PluginCommand{
			.id = object.value(QStringLiteral("id")).toString(),
			.title = object.value(QStringLiteral("title")).toString(),
			.description = object.value(QStringLiteral("description")).toString(),
		};
		if (command.title.isEmpty()) {
			command.title = object.value(QStringLiteral("label")).toString();
		}
		if (command.id.isEmpty()) {
			continue;
		}
		if (command.title.isEmpty()) {
			command.title = command.id;
		}
		result.push_back(std::move(command));
	}
	return result;
}

[[nodiscard]] ManifestParseResult SoftParsePluginJson(
		const QString &path,
		const QString &rootPath,
		const QString &folderName) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return { std::nullopt, QStringLiteral("Could not open plugin.json.") };
	}
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return {
			std::nullopt,
			QStringLiteral("Invalid plugin.json: %1").arg(parseError.errorString()),
		};
	}
	const auto object = document.object();
	auto manifest = PluginManifest();
	manifest.id = object.value(QStringLiteral("id")).toString();
	manifest.name = object.value(QStringLiteral("name")).toString();
	manifest.version = object.value(QStringLiteral("version")).toString();
	manifest.author = object.value(QStringLiteral("author")).toString();
	manifest.description = object.value(QStringLiteral("description")).toString();
	manifest.entry = object.value(QStringLiteral("entry")).toString();
	ParsePermissionsInto(manifest, object);

	const auto typeValue = object.value(QStringLiteral("type")).toString();
	if (!typeValue.isEmpty()) {
		manifest.type = ParsePluginType(typeValue);
	}

	const auto hasPalette = object.contains(QStringLiteral("palette"))
		|| object.contains(QStringLiteral("accent"))
		|| object.contains(QStringLiteral("background"));
	const auto hasPanels = object.contains(QStringLiteral("panels"));
	const auto hasCommands = object.contains(QStringLiteral("commands"))
		|| object.contains(QStringLiteral("actions"));

	const auto hasScript = FileExists(rootPath, QStringLiteral("plugin.js"))
		|| FileExists(rootPath, QStringLiteral("plugin-script.js"))
		|| FileExists(rootPath, QStringLiteral("main.js"));

	if (manifest.type == PluginType::Unknown) {
		if (hasPalette) {
			manifest.type = PluginType::Theme;
		} else if (hasPanels || hasScript) {
			manifest.type = PluginType::UiExtension;
		} else if (hasCommands) {
			manifest.type = PluginType::Utility;
		} else if (!DetectThemeEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::Theme;
		} else if (!DetectUiEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::UiExtension;
		} else if (!DetectUtilityEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::Utility;
		}
	}
	if (manifest.type == PluginType::Unknown) {
		return {
			std::nullopt,
			QStringLiteral(
				"plugin.json needs type, or palette/panels/commands, "
				"or a theme.json / ui.json / utility.json / plugin.js nearby."),
		};
	}

	if (object.contains(QStringLiteral("commands"))) {
		manifest.commands = ParseCommandsArray(
			object.value(QStringLiteral("commands")).toArray());
	} else if (object.contains(QStringLiteral("actions"))) {
		manifest.commands = ParseCommandsArray(
			object.value(QStringLiteral("actions")).toArray());
	}

	// One-file / script plugins: content may live in plugin.json and/or plugin.js.
	if (manifest.entry.isEmpty()) {
		if ((manifest.type == PluginType::Theme && hasPalette)
			|| (manifest.type == PluginType::UiExtension && hasPanels)
			|| (manifest.type == PluginType::Utility)
			|| (manifest.type == PluginType::UiExtension)) {
			manifest.entry = QStringLiteral("plugin.json");
		}
	}

	FillDefaults(manifest, folderName);
	FillEntryFromDisk(manifest, rootPath);
	if (manifest.entry.isEmpty()) {
		if (FileExists(rootPath, QStringLiteral("plugin.js"))) {
			manifest.entry = QStringLiteral("plugin.js");
		} else if (FileExists(rootPath, QStringLiteral("plugin-script.js"))) {
			manifest.entry = QStringLiteral("plugin-script.js");
		} else if (FileExists(rootPath, QStringLiteral("main.js"))) {
			manifest.entry = QStringLiteral("main.js");
		} else {
			// Metadata-only plugin.json is a valid entry.
			manifest.entry = QStringLiteral("plugin.json");
		}
	}
	if (manifest.type == PluginType::Unknown
		&& (manifest.entry.endsWith(QStringLiteral(".js"))
			|| FileExists(rootPath, QStringLiteral("plugin.js")))) {
		manifest.type = PluginType::UiExtension;
	}
	return { std::move(manifest), QString() };
}

[[nodiscard]] ManifestParseResult SoftParsePlugingramJson(
		const QString &path,
		const QString &rootPath,
		const QString &folderName) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {
			std::nullopt,
			QStringLiteral("Could not open plugingram.json."),
		};
	}
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return {
			std::nullopt,
			QStringLiteral("Invalid plugingram.json: %1")
				.arg(parseError.errorString()),
		};
	}
	const auto object = document.object();
	auto manifest = PluginManifest();
	manifest.id = object.value(QStringLiteral("id")).toString();
	manifest.name = object.value(QStringLiteral("name")).toString();
	manifest.version = object.value(QStringLiteral("version")).toString();
	manifest.author = object.value(QStringLiteral("author")).toString();
	manifest.description = object.value(QStringLiteral("description")).toString();
	manifest.entry = object.value(QStringLiteral("entry")).toString();
	ParsePermissionsInto(manifest, object);

	const auto typeValue = object.value(QStringLiteral("type")).toString();
	if (!typeValue.isEmpty()) {
		manifest.type = ParsePluginType(typeValue);
	}

	if (object.contains(QStringLiteral("commands"))) {
		manifest.commands = ParseCommandsArray(
			object.value(QStringLiteral("commands")).toArray());
	} else if (object.contains(QStringLiteral("actions"))) {
		manifest.commands = ParseCommandsArray(
			object.value(QStringLiteral("actions")).toArray());
	}

	const auto hasPalette = object.contains(QStringLiteral("palette"));
	const auto hasPanels = object.contains(QStringLiteral("panels"));
	const auto hasCommands = !manifest.commands.empty();

	const auto hasScript = FileExists(rootPath, QStringLiteral("plugin.js"))
		|| FileExists(rootPath, QStringLiteral("plugin-script.js"))
		|| FileExists(rootPath, QStringLiteral("main.js"));

	if (manifest.type == PluginType::Unknown) {
		if (hasPalette || !DetectThemeEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::Theme;
		} else if (hasPanels
			|| hasScript
			|| !DetectUiEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::UiExtension;
		} else if (hasCommands || !DetectUtilityEntryFile(rootPath).isEmpty()) {
			manifest.type = PluginType::Utility;
		} else {
			manifest.type = hasScript
				? PluginType::UiExtension
				: PluginType::Utility;
		}
	}

	FillDefaults(manifest, folderName);
	FillEntryFromDisk(manifest, rootPath);
	if (manifest.entry.isEmpty()) {
		if (FileExists(rootPath, QStringLiteral("plugin.js"))) {
			manifest.entry = QStringLiteral("plugin.js");
		} else if (FileExists(rootPath, QStringLiteral("plugin-script.js"))) {
			manifest.entry = QStringLiteral("plugin-script.js");
		} else if (FileExists(rootPath, QStringLiteral("main.js"))) {
			manifest.entry = QStringLiteral("main.js");
		} else {
			manifest.entry = QFileInfo(path).fileName();
		}
	}
	return { std::move(manifest), QString() };
}

[[nodiscard]] ManifestParseResult SynthesizeFromFiles(
		const QString &rootPath,
		const QString &folderName) {
	auto manifest = PluginManifest();
	const auto themeEntry = DetectThemeEntryFile(rootPath);
	const auto uiEntry = DetectUiEntryFile(rootPath);
	const auto utilityEntry = DetectUtilityEntryFile(rootPath);

	if (!themeEntry.isEmpty()) {
		manifest.type = PluginType::Theme;
		manifest.entry = themeEntry;
	} else if (!uiEntry.isEmpty()) {
		manifest.type = PluginType::UiExtension;
		manifest.entry = uiEntry;
	} else if (!utilityEntry.isEmpty()) {
		manifest.type = PluginType::Utility;
		manifest.entry = utilityEntry;
		// Pull commands from utility.json if present.
		auto file = QFile(rootPath + QStringLiteral("/") + utilityEntry);
		if (file.open(QIODevice::ReadOnly)) {
			const auto document = QJsonDocument::fromJson(file.readAll());
			if (document.isObject()) {
				const auto object = document.object();
				if (object.contains(QStringLiteral("commands"))) {
					manifest.commands = ParseCommandsArray(
						object.value(QStringLiteral("commands")).toArray());
				} else if (object.contains(QStringLiteral("actions"))) {
					manifest.commands = ParseCommandsArray(
						object.value(QStringLiteral("actions")).toArray());
				}
				const auto name = object.value(QStringLiteral("name")).toString();
				if (!name.isEmpty()) {
					manifest.name = name;
				}
				const auto description = object.value(
					QStringLiteral("description")).toString();
				if (!description.isEmpty()) {
					manifest.description = description;
				}
			}
		}
	} else if (FileExists(rootPath, QStringLiteral("plugin.js"))
		|| FileExists(rootPath, QStringLiteral("plugin-script.js"))
		|| FileExists(rootPath, QStringLiteral("main.js"))) {
		manifest.type = PluginType::UiExtension;
		if (FileExists(rootPath, QStringLiteral("plugin.js"))) {
			manifest.entry = QStringLiteral("plugin.js");
		} else if (FileExists(rootPath, QStringLiteral("plugin-script.js"))) {
			manifest.entry = QStringLiteral("plugin-script.js");
		} else {
			manifest.entry = QStringLiteral("main.js");
		}
	} else {
		return {
			std::nullopt,
			QStringLiteral(
				"Drop theme.json, ui.json, utility.json, plugin.json "
				"or plugin.js into this folder."),
		};
	}

	FillDefaults(manifest, folderName);
	return { std::move(manifest), QString() };
}

} // namespace

QString DetectThemeEntryFile(const QString &rootPath) {
	const auto candidates = {
		QStringLiteral("theme.json"),
		QStringLiteral("theme.tdesktop-palette"),
		QStringLiteral("theme.tdesktop-theme"),
		QStringLiteral("colors.tdesktop-palette"),
	};
	for (const auto &name : candidates) {
		if (FileExists(rootPath, name)) {
			return name;
		}
	}
	return {};
}

QString DetectUiEntryFile(const QString &rootPath) {
	if (FileExists(rootPath, QStringLiteral("ui.json"))) {
		return QStringLiteral("ui.json");
	}
	if (FileExists(rootPath, QStringLiteral("plugin.js"))) {
		return QStringLiteral("plugin.js");
	}
	if (FileExists(rootPath, QStringLiteral("plugin-script.js"))) {
		return QStringLiteral("plugin-script.js");
	}
	if (FileExists(rootPath, QStringLiteral("main.js"))) {
		return QStringLiteral("main.js");
	}
	if (FileExists(rootPath, QStringLiteral("plugin.json"))) {
		return QStringLiteral("plugin.json");
	}
	return {};
}

QString DetectUtilityEntryFile(const QString &rootPath) {
	if (FileExists(rootPath, QStringLiteral("utility.json"))) {
		return QStringLiteral("utility.json");
	}
	return {};
}

QString ResolveUiEntryPath(const PluginDescriptor &plugin) {
	if (!plugin.manifest.entry.isEmpty()) {
		const auto path = plugin.rootPath
			+ QStringLiteral("/")
			+ plugin.manifest.entry;
		if (QFileInfo::exists(path) && QFileInfo(path).isFile()) {
			return path;
		}
	}
	const auto detected = DetectUiEntryFile(plugin.rootPath);
	if (!detected.isEmpty()) {
		return plugin.rootPath + QStringLiteral("/") + detected;
	}
	if (FileExists(plugin.rootPath, QStringLiteral("plugin.json"))) {
		return plugin.rootPath + QStringLiteral("/plugin.json");
	}
	return {};
}

ManifestParseResult ParseManifestFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return { std::nullopt, QStringLiteral("Could not open manifest file.") };
	}

	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(
		file.readAll(),
		&parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return {
			std::nullopt,
			QStringLiteral("Invalid manifest JSON: %1").arg(parseError.errorString()),
		};
	}

	const auto object = document.object();
	auto manifest = PluginManifest();
	auto error = QString();

	manifest.id = RequireString(object, "id", &error);
	if (!error.isEmpty()) {
		return { std::nullopt, error };
	}
	manifest.name = RequireString(object, "name", &error);
	if (!error.isEmpty()) {
		return { std::nullopt, error };
	}
	manifest.version = RequireString(object, "version", &error);
	if (!error.isEmpty()) {
		return { std::nullopt, error };
	}

	const auto typeValue = RequireString(object, "type", &error);
	if (!error.isEmpty()) {
		return { std::nullopt, error };
	}
	manifest.type = ParsePluginType(typeValue);
	if (manifest.type == PluginType::Unknown) {
		return {
			std::nullopt,
			QStringLiteral("Unsupported plugin type: %1").arg(typeValue),
		};
	}

	manifest.author = object.value(QStringLiteral("author")).toString();
	manifest.description = object.value(QStringLiteral("description")).toString();
	manifest.entry = object.value(QStringLiteral("entry")).toString();

	const auto tags = object.value(QStringLiteral("tags"));
	if (tags.isArray()) {
		for (const auto &tag : tags.toArray()) {
			if (tag.isString()) {
				manifest.tags.push_back(tag.toString());
			}
		}
	}

	const auto permissions = object.value(QStringLiteral("permissions"));
	if (permissions.isArray()) {
		for (const auto &permission : permissions.toArray()) {
			if (!permission.isString()) {
				return {
					std::nullopt,
					QStringLiteral("Permission values must be strings."),
				};
			}
			auto parsed = PluginPermission::UiModify;
			if (!ParsePluginPermission(permission.toString(), &parsed)) {
				return {
					std::nullopt,
					QStringLiteral("Unsupported permission: %1")
						.arg(permission.toString()),
				};
			}
			manifest.permissions.push_back(parsed);
		}
	}

	const auto commands = object.value(QStringLiteral("commands"));
	if (commands.isArray()) {
		manifest.commands = ParseCommandsArray(commands.toArray());
		for (const auto &command : manifest.commands) {
			if (command.id.isEmpty() || command.title.isEmpty()) {
				return {
					std::nullopt,
					QStringLiteral("Command entries need id and title."),
				};
			}
		}
	}

	return { std::move(manifest), QString() };
}

ManifestParseResult DiscoverPluginManifest(
		const QString &rootPath,
		const QString &folderName) {
	const auto manifestPath = rootPath + QStringLiteral("/manifest.json");
	if (QFileInfo::exists(manifestPath)) {
		auto parsed = ParseManifestFile(manifestPath);
		if (!parsed.manifest.has_value()) {
			return parsed;
		}
		FillEntryFromDisk(*parsed.manifest, rootPath);
		if (parsed.manifest->entry.isEmpty()) {
			return {
				std::nullopt,
				QStringLiteral("Manifest has no entry and no known content file."),
			};
		}
		return parsed;
	}

	const auto pluginJson = rootPath + QStringLiteral("/plugin.json");
	if (QFileInfo::exists(pluginJson)) {
		return SoftParsePluginJson(pluginJson, rootPath, folderName);
	}

	const auto plugingramJson = rootPath + QStringLiteral("/plugingram.json");
	if (QFileInfo::exists(plugingramJson)) {
		return SoftParsePlugingramJson(plugingramJson, rootPath, folderName);
	}

	const auto storeMeta = rootPath + QStringLiteral("/.plugingram-store.json");
	if (QFileInfo::exists(storeMeta)) {
		return SoftParsePlugingramJson(storeMeta, rootPath, folderName);
	}

	return SynthesizeFromFiles(rootPath, folderName);
}

} // namespace PluginSystem
