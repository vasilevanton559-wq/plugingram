#pragma once

#include "plugin_system/plugins_common.h"
#include "plugin_system/plugins_host.h"
#include "plugin_system/plugins_store.h"

#include <QtCore/QHash>

#include <memory>
#include <vector>

namespace PluginSystem {

class Manager final {
public:
	Manager();

	void refresh();
	void refreshStore();
	void applyEnabledPlugins();

	[[nodiscard]] const std::vector<PluginDescriptor> &plugins() const;
	[[nodiscard]] const RemotePluginIndex &remoteIndex() const;
	[[nodiscard]] HostCapabilities hostCapabilities() const;
	[[nodiscard]] Host &host();
	[[nodiscard]] StoreService &store();

	[[nodiscard]] QString pluginApiVersion() const;
	[[nodiscard]] QString pluginsRoot() const;
	[[nodiscard]] QString storeIndexPath() const;
	[[nodiscard]] QString storeCatalogUrl() const;

	[[nodiscard]] const PluginDescriptor *findById(const QString &id) const;

	bool setPluginEnabled(const QString &id, bool enabled);
	bool setPluginFavorite(const QString &id, bool favorite);
	bool deletePlugin(const QString &id);
	bool reloadPlugin(const QString &id);
	bool runUtilityCommand(const QString &pluginId, const QString &commandId);

	void fetchStoreCatalog(Fn<void(bool ok, QString error)> done);
	void installStoreEntry(
		const RemotePluginEntry &entry,
		Fn<void(float64 progress)> progress,
		Fn<void(bool ok, QString error)> done,
		bool preserveState = false);
	void syncInstalledFromStore(Fn<void()> done);

private:
	struct PersistentState {
		bool enabled = false;
		bool favorite = false;
	};

	void syncInstalledFromStoreStep(
		std::shared_ptr<std::vector<RemotePluginEntry>> queue,
		int index,
		Fn<void()> done);

	[[nodiscard]] QString statePath() const;
	void loadPersistentState();
	void savePersistentState() const;
	[[nodiscard]] PersistentState stateFor(const QString &id) const;
	[[nodiscard]] bool hasEntryFile(
		const QString &rootPath,
		const PluginManifest &manifest) const;
	[[nodiscard]] QString ResolveIconPath(const QString &rootPath) const;
	void sortPlugins();
	void disableOtherThemes(const QString &exceptId);
	void removeDuplicateStorePlugins(
		const QString &keepId,
		const QString &repoUrl);

	std::vector<PluginDescriptor> _plugins;
	RemotePluginIndex _remoteIndex;
	QHash<QString, PersistentState> _persistentState;
	std::unique_ptr<Host> _host;
	std::unique_ptr<StoreService> _store;
};

} // namespace PluginSystem
