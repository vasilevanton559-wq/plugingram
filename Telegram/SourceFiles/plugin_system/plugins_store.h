#pragma once

#include "base/basic_types.h"
#include "plugin_system/plugins_common.h"

#include <QtCore/QObject>
#include <QtNetwork/QNetworkAccessManager>

namespace PluginSystem {

struct RemoteIndexParseResult {
	RemotePluginIndex index;
	QString error;
};

[[nodiscard]] RemoteIndexParseResult ParseRemoteIndexFile(const QString &path);
[[nodiscard]] RemoteIndexParseResult ParseRemoteIndexBytes(const QByteArray &bytes);

[[nodiscard]] QString DefaultStoreCatalogUrl();
[[nodiscard]] QString ResolveStoreCatalogUrl(const QString &pluginsRoot);
[[nodiscard]] bool IsTrustedPluginDownloadUrl(const QString &url);
[[nodiscard]] bool IsTrustedPluginRepoUrl(const QString &url);
[[nodiscard]] QString CanonicalGitHubRepoKey(const QString &url);
[[nodiscard]] QString ResolveStoreIconUrl(const RemotePluginEntry &entry);
[[nodiscard]] bool CanInstallOrUpdateStoreEntry(
	const QString &pluginsRoot,
	const RemotePluginEntry &entry,
	QString *error = nullptr);

[[nodiscard]] bool SaveRemoteIndexFile(
	const QString &path,
	const RemotePluginIndex &index,
	QString *error = nullptr);

[[nodiscard]] bool InstallPluginArchive(
	const QByteArray &zipBytes,
	const QString &pluginsRoot,
	const QString &pluginId,
	QString *error = nullptr);

class StoreService final : public QObject {
public:
	explicit StoreService(Fn<QString()> pluginsRoot);

	void fetchCatalog(Fn<void(bool ok, QString error)> done);
	void installEntry(
		const RemotePluginEntry &entry,
		Fn<void(float64 progress)> progress,
		Fn<void(bool ok, QString error)> done);
	void fetchTrustedBytes(
		const QString &url,
		int maxBytes,
		Fn<void(QByteArray bytes, QString error)> done);

	[[nodiscard]] bool busy() const;

private:
	void getBytes(
		const QString &url,
		int maxBytes,
		Fn<void(float64 progress)> progress,
		Fn<void(QByteArray bytes, QString error)> done,
		const QByteArray &accept = QByteArray(),
		bool trackBusy = true);

	void discoverFromGitHub(Fn<void(bool ok, QString error)> done);
	void finishDiscover(
		RemotePluginIndex index,
		const QString &cachePath,
		Fn<void(bool ok, QString error)> done,
		const QString &warning = QString());

	Fn<QString()> _pluginsRoot;
	QNetworkAccessManager _nam;
	int _inflight = 0;
};

} // namespace PluginSystem
