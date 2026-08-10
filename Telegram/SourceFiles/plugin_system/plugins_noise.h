// Plugingram: built-in Noise plugin (spoiler blur in profiles).
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace PluginSystem {

inline constexpr auto kNoisePluginId = "noise";

struct NoisePluginHooks {
	Fn<void(bool /*enabled*/)> apply;
	Fn<bool()> readSessionBlur;
	Fn<void(const QString & /*iconPath*/)> writeSpoilerIcon;
};

void SetNoisePluginHooks(NoisePluginHooks hooks);

// Ensure the bundled plugin folder exists under pluginsRoot.
void EnsureBundledNoisePlugin(const QString &pluginsRoot);

// Sync session spoiler blur with the plugin enabled flag.
void ApplyNoisePluginState(bool enabled);

// Prefer session preference when seeding plugin state the first time.
[[nodiscard]] bool DefaultNoisePluginEnabled();

} // namespace PluginSystem
