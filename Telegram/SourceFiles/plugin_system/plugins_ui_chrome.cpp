#include "plugin_system/plugins_ui_chrome.h"

namespace PluginSystem {

UiChromeState &UiChromeState::Instance() {
	static UiChromeState instance;
	return instance;
}

void UiChromeState::set(UiChromeFlags flags) {
	if (_flags.filters == flags.filters
		&& _flags.mainMenu == flags.mainMenu
		&& _flags.search == flags.search) {
		return;
	}
	_flags = flags;
	++_revision;
	_changes.fire({});
}

void UiChromeState::reset() {
	set(UiChromeFlags{});
}

UiChromeFlags UiChromeState::flags() const {
	return _flags;
}

int UiChromeState::revision() const {
	return _revision;
}

rpl::producer<> UiChromeState::changes() const {
	return _changes.events();
}

} // namespace PluginSystem
