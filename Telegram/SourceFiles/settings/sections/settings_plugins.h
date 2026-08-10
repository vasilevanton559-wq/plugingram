/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "settings/settings_type.h"

namespace Ui {
class SettingsButton;
} // namespace Ui

namespace Settings {

[[nodiscard]] Type PluginsId();

void ApplyPluginsRainbowToButton(not_null<Ui::SettingsButton*> button);
void ApplyPlugingramFeaturesRainbowToButton(
	not_null<Ui::SettingsButton*> button);

} // namespace Settings
