# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

include(cmake/external_quickjs.cmake)

add_library(td_plugins OBJECT)
init_non_host_target(td_plugins)
add_library(tdesktop::td_plugins ALIAS td_plugins)

nice_target_sources(td_plugins ${src_loc}
PRIVATE
	plugin_system/plugins_common.cpp
	plugin_system/plugins_common.h
	plugin_system/plugins_host.cpp
	plugin_system/plugins_host.h
	plugin_system/plugins_manager.cpp
	plugin_system/plugins_manager.h
	plugin_system/plugins_manifest.cpp
	plugin_system/plugins_manifest.h
	plugin_system/plugins_noise.cpp
	plugin_system/plugins_noise.h
	plugin_system/plugins_script_engine.cpp
	plugin_system/plugins_script_engine.h
	plugin_system/plugins_script_host.cpp
	plugin_system/plugins_script_host.h
	plugin_system/plugins_store.cpp
	plugin_system/plugins_store.h
	plugin_system/plugins_theme.h
	plugin_system/plugins_ui_chrome.cpp
	plugin_system/plugins_ui_chrome.h
	plugin_system/plugins_ui_extension.cpp
	plugin_system/plugins_ui_extension.h
)

target_include_directories(td_plugins
PUBLIC
	${src_loc}
)

target_link_libraries(td_plugins
PUBLIC
	desktop-app::lib_base
PRIVATE
	desktop-app::external_qt
	desktop-app::external_minizip
	desktop-app::external_quickjs
)
