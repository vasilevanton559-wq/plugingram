#pragma once

#include "plugin_system/plugins_common.h"

#include <rpl/rpl.h>

namespace PluginSystem {

class UiChromeState final {
public:
	[[nodiscard]] static UiChromeState &Instance();

	void set(UiChromeFlags flags);
	void reset();
	[[nodiscard]] UiChromeFlags flags() const;
	[[nodiscard]] int revision() const;
	[[nodiscard]] rpl::producer<> changes() const;

private:
	UiChromeState() = default;

	UiChromeFlags _flags;
	int _revision = 0;
	rpl::event_stream<> _changes;
};

} // namespace PluginSystem
