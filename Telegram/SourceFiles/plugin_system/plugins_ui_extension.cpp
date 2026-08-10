#include "plugin_system/plugins_ui_extension.h"

#include "plugin_system/plugins_manifest.h"

#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace PluginSystem {
namespace {

void RemovePluginPanels(
		std::vector<UiPanelDescriptor> &panels,
		const QString &pluginId,
		const QString &panelId = QString()) {
	auto out = std::vector<UiPanelDescriptor>();
	out.reserve(panels.size());
	for (auto &panel : panels) {
		const auto samePlugin = (panel.pluginId == pluginId);
		const auto samePanel = panelId.isEmpty() || (panel.id == panelId);
		if (!(samePlugin && samePanel)) {
			out.push_back(std::move(panel));
		}
	}
	panels = std::move(out);
}

} // namespace

UiExtensionRegistry &UiExtensionRegistry::Instance() {
	static UiExtensionRegistry instance;
	return instance;
}

void UiExtensionRegistry::clear() {
	_panels.clear();
}

void UiExtensionRegistry::registerPanels(
		std::vector<UiPanelDescriptor> panels) {
	for (auto &panel : panels) {
		RemovePluginPanels(_panels, panel.pluginId, panel.id);
		_panels.push_back(std::move(panel));
	}
}

void UiExtensionRegistry::unregisterPlugin(const QString &pluginId) {
	RemovePluginPanels(_panels, pluginId);
}

const std::vector<UiPanelDescriptor> &UiExtensionRegistry::panels() const {
	return _panels;
}

std::vector<UiPanelDescriptor> ParseUiExtensionPanels(
		const PluginDescriptor &plugin) {
	auto result = std::vector<UiPanelDescriptor>();
	const auto path = ResolveUiEntryPath(plugin);
	if (path.isEmpty()) {
		return result;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return result;
	}
	const auto root = document.object();

	auto actionsById = QHash<QString, PluginCommand>();
	const auto actions = root.value(QStringLiteral("actions")).toArray();
	for (const auto &value : actions) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		auto command = PluginCommand{
			.id = object.value(QStringLiteral("id")).toString(),
			.title = object.value(QStringLiteral("label")).toString(),
			.description = object.value(
				QStringLiteral("description")).toString(),
		};
		if (command.title.isEmpty()) {
			command.title = command.id;
		}
		if (!command.id.isEmpty()) {
			actionsById.insert(command.id, command);
		}
	}
	for (const auto &command : plugin.manifest.commands) {
		if (!actionsById.contains(command.id)) {
			actionsById.insert(command.id, command);
		}
	}

	const auto panels = root.value(QStringLiteral("panels")).toArray();
	for (const auto &value : panels) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		auto panel = UiPanelDescriptor{
			.id = object.value(QStringLiteral("id")).toString(),
			.title = object.value(QStringLiteral("title")).toString(),
			.placement = object.value(QStringLiteral("placement")).toString(),
			.pluginId = plugin.manifest.id,
			.pluginName = plugin.manifest.name,
			.description = plugin.manifest.description,
		};
		if (panel.id.isEmpty()) {
			continue;
		}
		if (panel.title.isEmpty()) {
			panel.title = plugin.manifest.name;
		}
		if (panel.placement.isEmpty()) {
			panel.placement = QStringLiteral("settings.sidebar");
		}
		if (panel.placement != QStringLiteral("settings.sidebar")
			&& panel.placement != QStringLiteral("chat.composer")) {
			continue;
		}
		for (auto i = actionsById.constBegin();
			i != actionsById.constEnd();
			++i) {
			panel.actions.push_back(i.value());
		}
		result.push_back(std::move(panel));
	}
	return result;
}

} // namespace PluginSystem
