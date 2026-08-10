#pragma once

#include "base/basic_types.h"
#include "plugin_system/plugins_common.h"

namespace PluginSystem {

class Manager;

class Host final {
public:
	explicit Host(not_null<Manager*> manager);

	void applyEnabledPlugins();
	bool applyPlugin(const PluginDescriptor &plugin);
	bool unapplyPlugin(const PluginDescriptor &plugin);
	void rebuildUiExtensions();

private:
	const not_null<Manager*> _manager;
};

} // namespace PluginSystem
