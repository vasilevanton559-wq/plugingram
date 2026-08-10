#pragma once

#include "plugin_system/plugins_common.h"

#include <vector>

namespace PluginSystem {

[[nodiscard]] std::vector<UiPanelDescriptor> ParseUiExtensionPanels(
	const PluginDescriptor &plugin);

class UiExtensionRegistry final {
public:
	[[nodiscard]] static UiExtensionRegistry &Instance();

	void clear();
	void registerPanels(std::vector<UiPanelDescriptor> panels);
	void unregisterPlugin(const QString &pluginId);

	[[nodiscard]] const std::vector<UiPanelDescriptor> &panels() const;

private:
	UiExtensionRegistry() = default;

	std::vector<UiPanelDescriptor> _panels;
};

} // namespace PluginSystem
