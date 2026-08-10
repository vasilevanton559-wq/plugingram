#include "plugin_system/plugins_manager.h"

#include "plugin_system/plugins_manifest.h"
#include "plugin_system/plugins_noise.h"
#include "plugin_system/plugins_script_host.h"
#include "plugin_system/plugins_store.h"
#include "plugin_system/plugins_theme.h"
#include "plugin_system/plugins_ui_extension.h"

#include <crl/crl.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>

// Declared in settings.cpp; avoid including settings.h here (td_plugins
// does not depend on lib_ui style headers).
extern QString gWorkingDir;

namespace {

[[nodiscard]] bool IsPluginsCatalog(const QString &path) {
	if (path.isEmpty() || !QDir(path).exists()) {
		return false;
	}
	return QFileInfo(
		path + QStringLiteral("/store/index.json")).exists()
		|| QFileInfo(
			path + QStringLiteral("/.plugingram-plugins")).exists();
}

[[nodiscard]] QString EnsurePluginsRoot(const QString &absolute) {
	QDir().mkpath(absolute);
	QDir().mkpath(absolute + QStringLiteral("/store"));
	const auto marker = absolute + QStringLiteral("/.plugingram-plugins");
	if (!QFileInfo::exists(marker)) {
		QFile(marker).open(QIODevice::WriteOnly);
	}
	return absolute;
}

[[nodiscard]] QString ResolvePluginsRoot() {
	// Explicit override for developers / tests.
	if (const auto env = qEnvironmentVariable("PLUGINGRAM_PLUGINS");
		!env.isEmpty()) {
		return EnsurePluginsRoot(QFileInfo(env).absoluteFilePath());
	}

	const auto exeDir = QDir(QCoreApplication::applicationDirPath());

	// Optional: use the repo plugins/ folder while developing.
	if (qEnvironmentVariableIsSet("PLUGINGRAM_USE_PROJECT_PLUGINS")) {
		const auto projectPlugins = QFileInfo(
			exeDir.absoluteFilePath(QStringLiteral("../../plugins"))
		).absoluteFilePath();
		if (IsPluginsCatalog(projectPlugins)
			|| QDir(projectPlugins).exists()) {
			return EnsurePluginsRoot(projectPlugins);
		}
	}

	// Default: keep plugins with profile data in the working dir
	// (%AppData%/Plugingram Desktop/plugins on Windows).
	if (!gWorkingDir.isEmpty()) {
		return EnsurePluginsRoot(
			QFileInfo(gWorkingDir + QStringLiteral("plugins")).absoluteFilePath());
	}

	const auto besideExe = QFileInfo(
		exeDir.absoluteFilePath(QStringLiteral("plugins"))
	).absoluteFilePath();
	return EnsurePluginsRoot(besideExe);
}

} // namespace

namespace PluginSystem {

Manager::Manager()
: _host(std::make_unique<Host>(this))
, _store(std::make_unique<StoreService>([=] { return pluginsRoot(); })) {
}

void Manager::refresh() {
	loadPersistentState();
	EnsureBundledNoisePlugin(pluginsRoot());
	_plugins.clear();

	auto root = QDir(pluginsRoot());
	if (!root.exists()) {
		root.mkpath(QStringLiteral("."));
	}

	const auto entries = root.entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const auto &entry : entries) {
		if (entry.fileName() == QStringLiteral("store")) {
			continue;
		}

		auto descriptor = PluginDescriptor();
		descriptor.rootPath = entry.absoluteFilePath();
		descriptor.manifestPath = entry.absoluteFilePath()
			+ QStringLiteral("/manifest.json");
		descriptor.iconPath = ResolveIconPath(descriptor.rootPath);
		descriptor.bundled = (entry.fileName()
			== QString::fromUtf8(kNoisePluginId));

		const auto parsed = DiscoverPluginManifest(
			descriptor.rootPath,
			entry.fileName());
		if (!parsed.manifest.has_value()) {
			descriptor.manifest.id = entry.fileName();
			descriptor.manifest.name = entry.fileName();
			descriptor.manifest.type = PluginType::Unknown;
			descriptor.error = parsed.error.isEmpty()
				? QStringLiteral(
					"Drop theme.json, ui.json or plugin.json here.")
				: parsed.error;
			descriptor.state = PluginState::Error;
			_plugins.push_back(std::move(descriptor));
			continue;
		}

		descriptor.manifest = *parsed.manifest;
		if (!QFileInfo::exists(descriptor.manifestPath)) {
			const auto candidates = {
				QStringLiteral("/plugin.json"),
				QStringLiteral("/plugingram.json"),
				QStringLiteral("/.plugingram-store.json"),
			};
			for (const auto &suffix : candidates) {
				const auto path = descriptor.rootPath + suffix;
				if (QFileInfo::exists(path)) {
					descriptor.manifestPath = path;
					break;
				}
			}
		}
		descriptor.scriptPath = ResolveScriptPath(descriptor.rootPath);
		if (!descriptor.scriptPath.isEmpty()) {
			const auto ensure = [&](PluginPermission permission) {
				if (!ManifestHasPermission(
						descriptor.manifest,
						permission)) {
					descriptor.manifest.permissions.push_back(permission);
				}
			};
			ensure(PluginPermission::ScriptRun);
			ensure(PluginPermission::UiModify);
			ensure(PluginPermission::CommandsRegister);
			// Auto-grant UI customize surface for script plugins so authors
			// don't need a long permissions list for basic UI demos.
			ensure(PluginPermission::UiTheme);
			ensure(PluginPermission::UiOpacity);
			ensure(PluginPermission::UiScale);
			ensure(PluginPermission::UiChrome);
			ensure(PluginPermission::UiComposer);
			ensure(PluginPermission::SettingsReadWrite);
			if (descriptor.manifest.type == PluginType::Unknown) {
				descriptor.manifest.type = PluginType::UiExtension;
			}
		}
		if (!hasEntryFile(descriptor.rootPath, descriptor.manifest)
			&& descriptor.scriptPath.isEmpty()) {
			descriptor.error = QStringLiteral(
				"The entry file declared by the module does not exist.");
			descriptor.state = PluginState::Error;
			_plugins.push_back(std::move(descriptor));
			continue;
		}

		const auto id = descriptor.manifest.id;
		if (!_persistentState.contains(id)
			&& id == QString::fromUtf8(kNoisePluginId)) {
			// Migrate old phone-blur toggle if present.
			auto enabled = DefaultNoisePluginEnabled();
			if (_persistentState.contains(QStringLiteral("phone-blur"))) {
				enabled = _persistentState.value(
					QStringLiteral("phone-blur")).enabled;
				_persistentState.remove(QStringLiteral("phone-blur"));
			}
			_persistentState.insert(id, PersistentState{
				enabled,
				false,
			});
			savePersistentState();
		}
		const auto state = stateFor(id);
		descriptor.favorite = state.favorite;
		descriptor.state = state.enabled
			? PluginState::Enabled
			: PluginState::Disabled;
		_plugins.push_back(std::move(descriptor));
	}

	sortPlugins();
	refreshStore();
	_host->rebuildUiExtensions();
}

void Manager::refreshStore() {
	const auto parsed = ParseRemoteIndexFile(storeIndexPath());
	_remoteIndex = parsed.index;
	if (!parsed.error.isEmpty()) {
		_remoteIndex.entries.clear();
		_remoteIndex.catalogTitle = parsed.error;
	}
}

void Manager::applyEnabledPlugins() {
	refresh();
	_host->applyEnabledPlugins();
	if (const auto *plugin = findById(QString::fromUtf8(kNoisePluginId))) {
		ApplyNoisePluginState(plugin->state == PluginState::Enabled);
	}
}

const std::vector<PluginDescriptor> &Manager::plugins() const {
	return _plugins;
}

const RemotePluginIndex &Manager::remoteIndex() const {
	return _remoteIndex;
}

Host &Manager::host() {
	return *_host;
}

StoreService &Manager::store() {
	return *_store;
}

QString Manager::storeCatalogUrl() const {
	return ResolveStoreCatalogUrl(pluginsRoot());
}

void Manager::fetchStoreCatalog(Fn<void(bool ok, QString error)> done) {
	_store->fetchCatalog([=](bool ok, QString error) {
		if (!ok) {
			if (done) {
				done(false, std::move(error));
			}
			return;
		}
		refreshStore();
		// Pull latest package contents for already installed store plugins.
		syncInstalledFromStore([=]() mutable {
			refresh();
			if (done) {
				done(true, std::move(error));
			}
		});
	});
}

void Manager::installStoreEntry(
		const RemotePluginEntry &entry,
		Fn<void(float64 progress)> progress,
		Fn<void(bool ok, QString error)> done,
		bool preserveState) {
	loadPersistentState();
	const auto id = entry.id;
	const auto existed = (findById(id) != nullptr);
	const auto previous = stateFor(id);
	_store->installEntry(
		entry,
		[=](float64 value) {
			if (progress) {
				crl::on_main([progress, value] {
					progress(value);
				});
			}
		},
		[=](bool ok, QString error) {
			crl::on_main([=]() mutable {
				if (ok) {
					loadPersistentState();
					PersistentState state;
					if (preserveState || existed) {
						state = previous;
					} else {
						state = PersistentState{ false, false };
					}
					_persistentState.insert(id, state);
					savePersistentState();
					refresh();
					// One GitHub repo → one installed plugin folder.
					removeDuplicateStorePlugins(id, entry.repoUrl);
					refresh();
					if (state.enabled) {
						if (const auto *plugin = findById(id)) {
							if (plugin->manifest.type == PluginType::Theme) {
								crl::on_main([=] {
									if (const auto *p = findById(id)) {
										_host->applyPlugin(*p);
									}
								});
							} else {
								_host->applyPlugin(*plugin);
							}
						}
					}
				}
				if (done) {
					done(ok, std::move(error));
				}
			});
		});
}

void Manager::syncInstalledFromStore(Fn<void()> done) {
	auto queue = std::make_shared<std::vector<RemotePluginEntry>>();
	for (const auto &entry : _remoteIndex.entries) {
		const auto *local = findById(entry.id);
		if (!local) {
			continue;
		}
		// Only auto-update store installs pinned to the same GitHub repo.
		// Never overwrite local plugins or foreign id claims.
		if (!CanInstallOrUpdateStoreEntry(pluginsRoot(), entry)) {
			continue;
		}
		queue->push_back(entry);
	}
	syncInstalledFromStoreStep(queue, 0, std::move(done));
}

void Manager::syncInstalledFromStoreStep(
		std::shared_ptr<std::vector<RemotePluginEntry>> queue,
		int index,
		Fn<void()> done) {
	if (!queue || index >= int(queue->size())) {
		if (done) {
			done();
		}
		return;
	}
	const auto entry = (*queue)[index];
	installStoreEntry(entry, nullptr, [=](bool, QString) {
		syncInstalledFromStoreStep(queue, index + 1, done);
	}, true);
}

HostCapabilities Manager::hostCapabilities() const {
	return HostCapabilities{
		.apiVersion = pluginApiVersion(),
		.contract = QStringLiteral(
			"Local plugins + GitHub store catalog/install"),
		.supportedModuleTypes = {
			QStringLiteral("theme"),
			QStringLiteral("ui_extension"),
			QStringLiteral("utility"),
		},
		.supportedPermissions = {
			PluginPermission::UiModify,
			PluginPermission::CommandsRegister,
			PluginPermission::SettingsReadWrite,
			PluginPermission::StoreBrowse,
			PluginPermission::ScriptRun,
			PluginPermission::UiTheme,
			PluginPermission::UiOpacity,
			PluginPermission::UiScale,
			PluginPermission::UiChrome,
			PluginPermission::UiComposer,
		},
	};
}

QString Manager::pluginApiVersion() const {
	return QStringLiteral("2");
}

QString Manager::pluginsRoot() const {
	return ResolvePluginsRoot();
}

QString Manager::storeIndexPath() const {
	return pluginsRoot() + QStringLiteral("/store/index.json");
}

const PluginDescriptor *Manager::findById(const QString &id) const {
	for (const auto &plugin : _plugins) {
		if (plugin.manifest.id == id) {
			return &plugin;
		}
	}
	return nullptr;
}

bool Manager::setPluginEnabled(const QString &id, bool enabled) {
	loadPersistentState();
	const auto *current = findById(id);
	if (!current || current->state == PluginState::Error) {
		return false;
	}

	if (enabled) {
		switch (current->manifest.type) {
		case PluginType::Theme:
		case PluginType::UiExtension:
			if (!ManifestHasPermission(
					current->manifest,
					PluginPermission::UiModify)) {
				return false;
			}
			break;
		case PluginType::Utility:
			if (!ManifestHasPermission(
					current->manifest,
					PluginPermission::CommandsRegister)) {
				return false;
			}
			break;
		case PluginType::Unknown:
			return false;
		}
	}

	if (enabled && current->manifest.type == PluginType::Theme) {
		disableOtherThemes(id);
	}

	auto state = stateFor(id);
	if (!enabled && current->state == PluginState::Enabled) {
		_host->unapplyPlugin(*current);
	}

	state.enabled = enabled;
	_persistentState.insert(id, state);
	savePersistentState();
	refresh();

	if (enabled) {
		if (const auto *updated = findById(id)) {
			_host->applyPlugin(*updated);
		}
	} else {
		_host->rebuildUiExtensions();
	}
	if (id == QString::fromUtf8(kNoisePluginId)) {
		ApplyNoisePluginState(enabled);
	}
	return true;
}

bool Manager::setPluginFavorite(const QString &id, bool favorite) {
	loadPersistentState();
	if (!findById(id)) {
		return false;
	}
	auto state = stateFor(id);
	state.favorite = favorite;
	_persistentState.insert(id, state);
	savePersistentState();
	refresh();
	return true;
}

bool Manager::deletePlugin(const QString &id) {
	loadPersistentState();
	const auto *current = findById(id);
	if (!current || current->bundled) {
		return false;
	}
	const auto wasEnabled = (current->state == PluginState::Enabled);
	const auto type = current->manifest.type;
	const auto root = current->rootPath;
	const auto resetThemeLater = wasEnabled && (type == PluginType::Theme);

	ScriptHost::Instance().stop(id);

	// Theme reset rebuilds chrome — never do it inline while Plugins UI is open.
	if (wasEnabled && !resetThemeLater) {
		_host->unapplyPlugin(*current);
	}

	_persistentState.remove(id);
	savePersistentState();
	if (!root.isEmpty()) {
		QDir(root).removeRecursively();
	}
	refresh();

	if (resetThemeLater) {
		crl::on_main([] {
			UnapplyThemePlugin();
		});
	}
	return true;
}

bool Manager::reloadPlugin(const QString &id) {
	Q_UNUSED(id);
	refresh();
	_host->applyEnabledPlugins();
	return true;
}

bool Manager::runUtilityCommand(
		const QString &pluginId,
		const QString &commandId) {
	const auto *plugin = findById(pluginId);
	if (!plugin || plugin->state != PluginState::Enabled) {
		return false;
	}
	if (!ManifestHasPermission(
			plugin->manifest,
			PluginPermission::CommandsRegister)) {
		return false;
	}
	if (ScriptHost::Instance().isRunning(pluginId)) {
		auto error = QString();
		if (ScriptHost::Instance().invokeAction(
				pluginId,
				commandId,
				&error)) {
			return true;
		}
	}
	for (const auto &command : plugin->manifest.commands) {
		if (command.id == commandId) {
			return true;
		}
	}
	return false;
}

void Manager::disableOtherThemes(const QString &exceptId) {
	for (const auto &plugin : _plugins) {
		if (plugin.manifest.type != PluginType::Theme
			|| plugin.manifest.id == exceptId
			|| plugin.state != PluginState::Enabled) {
			continue;
		}
		auto state = stateFor(plugin.manifest.id);
		state.enabled = false;
		_persistentState.insert(plugin.manifest.id, state);
		_host->unapplyPlugin(plugin);
	}
	savePersistentState();
}

void Manager::removeDuplicateStorePlugins(
		const QString &keepId,
		const QString &repoUrl) {
	const auto keepKey = CanonicalGitHubRepoKey(repoUrl);
	if (keepKey.isEmpty() || keepId.isEmpty()) {
		return;
	}
	auto toRemove = std::vector<QString>();
	for (const auto &plugin : _plugins) {
		if (plugin.manifest.id == keepId) {
			continue;
		}
		auto meta = QFile(
			plugin.rootPath + QStringLiteral("/.plugingram-store.json"));
		if (!meta.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto document = QJsonDocument::fromJson(meta.readAll());
		if (!document.isObject()) {
			continue;
		}
		const auto object = document.object();
		auto key = CanonicalGitHubRepoKey(
			object.value(QStringLiteral("repoUrl")).toString());
		if (key.isEmpty()) {
			key = CanonicalGitHubRepoKey(
				object.value(QStringLiteral("packageUrl")).toString());
		}
		if (key == keepKey) {
			toRemove.push_back(plugin.manifest.id);
		}
	}
	for (const auto &id : toRemove) {
		ScriptHost::Instance().stop(id);
		_persistentState.remove(id);
		if (const auto *plugin = findById(id)) {
			const auto root = plugin->rootPath;
			if (!root.isEmpty()) {
				QDir(root).removeRecursively();
			}
		}
	}
	if (!toRemove.empty()) {
		savePersistentState();
	}
}

QString Manager::statePath() const {
	return pluginsRoot() + QStringLiteral("/state.json");
}

void Manager::loadPersistentState() {
	_persistentState.clear();

	auto file = QFile(statePath());
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		return;
	}

	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return;
	}

	const auto plugins = document.object().value(QStringLiteral("plugins"));
	if (!plugins.isObject()) {
		return;
	}

	const auto pluginsObject = plugins.toObject();
	for (auto i = pluginsObject.begin(); i != pluginsObject.end(); ++i) {
		if (!i->isObject()) {
			continue;
		}
		const auto object = i->toObject();
		_persistentState.insert(i.key(), PersistentState{
			object.value(QStringLiteral("enabled")).toBool(false),
			object.value(QStringLiteral("favorite")).toBool(false),
		});
	}
}

void Manager::savePersistentState() const {
	auto pluginsObject = QJsonObject();
	for (auto i = _persistentState.begin(); i != _persistentState.end(); ++i) {
		pluginsObject.insert(i.key(), QJsonObject{
			{ QStringLiteral("enabled"), i.value().enabled },
			{ QStringLiteral("favorite"), i.value().favorite },
		});
	}

	const auto document = QJsonDocument(QJsonObject{
		{ QStringLiteral("schemaVersion"), QStringLiteral("1") },
		{ QStringLiteral("plugins"), pluginsObject },
	});

	auto root = QDir(pluginsRoot());
	if (!root.exists()) {
		root.mkpath(QStringLiteral("."));
	}

	auto file = QFile(statePath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(document.toJson(QJsonDocument::Indented));
	}
}

Manager::PersistentState Manager::stateFor(const QString &id) const {
	return _persistentState.value(id, PersistentState{ false, false });
}

bool Manager::hasEntryFile(
		const QString &rootPath,
		const PluginManifest &manifest) const {
	if (manifest.entry.isEmpty()) {
		return true;
	}
	const auto path = rootPath + QStringLiteral("/") + manifest.entry;
	if (!PathIsInsideRoot(rootPath, path)) {
		return false;
	}
	return QFileInfo(path).exists();
}

QString Manager::ResolveIconPath(const QString &rootPath) const {
	// Explicit icon from plugingram.json / plugin.json.
	const auto metaFiles = {
		QStringLiteral("/plugingram.json"),
		QStringLiteral("/plugin.json"),
		QStringLiteral("/.plugingram-store.json"),
	};
	for (const auto &metaSuffix : metaFiles) {
		auto file = QFile(rootPath + metaSuffix);
		if (!file.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject()) {
			continue;
		}
		const auto icon = document.object()
			.value(QStringLiteral("icon"))
			.toString()
			.trimmed();
		if (icon.isEmpty()) {
			continue;
		}
		if (!icon.contains(QStringLiteral("://"))) {
			const auto local = QFileInfo(rootPath + QChar('/') + icon)
				.absoluteFilePath();
			if (PathIsInsideRoot(rootPath, local)
				&& QFileInfo::exists(local)
				&& QFileInfo(local).isFile()) {
				return local;
			}
		}
	}

	const auto candidates = {
		QStringLiteral("/icon.png"),
		QStringLiteral("/icon.jpg"),
		QStringLiteral("/icon.jpeg"),
		QStringLiteral("/icon.webp"),
		QStringLiteral("/icon.gif"),
		QStringLiteral("/logo.png"),
		QStringLiteral("/assets/icon.png"),
		QStringLiteral("/assets/logo.png"),
	};
	for (const auto &suffix : candidates) {
		const auto path = rootPath + suffix;
		if (QFileInfo::exists(path)) {
			return path;
		}
	}
	return {};
}

void Manager::sortPlugins() {
	std::sort(begin(_plugins), end(_plugins), [](
			const PluginDescriptor &a,
			const PluginDescriptor &b) {
		if (a.favorite != b.favorite) {
			return a.favorite && !b.favorite;
		}
		return a.manifest.name.localeAwareCompare(b.manifest.name) < 0;
	});
}

} // namespace PluginSystem
