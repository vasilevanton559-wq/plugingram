#include "plugin_system/plugins_store.h"

#include "base/assertion.h"
#include "base/zlib_help.h"

#include <crl/crl.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <memory>

namespace PluginSystem {
namespace {

constexpr auto kMaxCatalogBytes = 2 * 1024 * 1024;
constexpr auto kMaxPackageBytes = 24 * 1024 * 1024;
constexpr auto kMaxZipEntryBytes = 8 * 1024 * 1024;
constexpr auto kRequestTimeoutMs = 20000;
constexpr auto kMaxDiscoverRepos = 40;

// Authors mark repos with this topic so Plugingram can find them on GitHub.
// NOTE: GitHub Search often returns 0 for complex OR queries — keep them simple.
constexpr auto kDiscoverTopic = "plugingram-plugin";
[[nodiscard]] QStringList DiscoverSearchQueries() {
	return {
		QStringLiteral("topic:plugingram-plugin"),
		QStringLiteral("topic:plugingram"),
		QStringLiteral("plugingram-plugin in:name,description"),
	};
}

struct RepoContext {
	QString fullName;
	QString htmlUrl;
	QString defaultBranch;
	QString owner;
	QString description;
	int stars = 0;
};

[[nodiscard]] bool IsTrustedGitHubHost(const QString &host) {
	const auto h = host.toLower();
	// github.com archive/zipball downloads redirect to codeload.github.com.
	// Release assets redirect to *.githubusercontent.com.
	return h == QStringLiteral("github.com")
		|| h == QStringLiteral("www.github.com")
		|| h == QStringLiteral("api.github.com")
		|| h == QStringLiteral("codeload.github.com")
		|| h == QStringLiteral("raw.githubusercontent.com")
		|| h.endsWith(QStringLiteral(".githubusercontent.com"));
}

[[nodiscard]] QString WithCacheBust(QString url) {
	auto parsed = QUrl(url);
	if (!parsed.isValid()) {
		return url;
	}
	auto query = QUrlQuery(parsed.query());
	while (query.hasQueryItem(QStringLiteral("_pg"))) {
		query.removeQueryItem(QStringLiteral("_pg"));
	}
	query.addQueryItem(
		QStringLiteral("_pg"),
		QString::number(crl::now()));
	parsed.setQuery(query);
	return parsed.toString(QUrl::FullyEncoded);
}

void WriteInstalledStoreMeta(
		const QString &pluginsRoot,
		const RemotePluginEntry &entry) {
	const auto pluginRoot = pluginsRoot + QChar('/') + entry.id;
	const auto path = pluginRoot + QStringLiteral("/.plugingram-store.json");
	auto iconRelative = QString();
	{
		auto pg = QFile(pluginRoot + QStringLiteral("/plugingram.json"));
		if (pg.open(QIODevice::ReadOnly)) {
			const auto document = QJsonDocument::fromJson(pg.readAll());
			if (document.isObject()) {
				iconRelative = document.object()
					.value(QStringLiteral("icon"))
					.toString()
					.trimmed();
			}
		}
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	auto object = QJsonObject{
		{ QStringLiteral("id"), entry.id },
		{ QStringLiteral("version"), entry.version },
		{ QStringLiteral("name"), entry.name },
		{ QStringLiteral("description"), entry.description },
		{ QStringLiteral("author"), entry.author },
		{ QStringLiteral("type"), SerializePluginType(entry.type) },
		{ QStringLiteral("packageUrl"), entry.packageUrl },
		{ QStringLiteral("repoUrl"), entry.repoUrl },
		{ QStringLiteral("updatedAt"), QString::number(crl::now()) },
	};
	if (!iconRelative.isEmpty()) {
		object.insert(QStringLiteral("icon"), iconRelative);
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void EnsurePlugingramJsonFromEntry(
		const QString &pluginsRoot,
		const RemotePluginEntry &entry) {
	const auto pluginRoot = pluginsRoot + QChar('/') + entry.id;
	const auto path = pluginRoot + QStringLiteral("/plugingram.json");
	if (QFileInfo::exists(path)) {
		return;
	}
	QDir().mkpath(pluginRoot);
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	file.write(QJsonDocument(QJsonObject{
		{ QStringLiteral("id"), entry.id },
		{ QStringLiteral("name"), entry.name },
		{ QStringLiteral("version"), entry.version },
		{ QStringLiteral("author"), entry.author },
		{ QStringLiteral("description"), entry.description },
		{ QStringLiteral("type"), SerializePluginType(entry.type) },
	}).toJson(QJsonDocument::Indented));
}

[[nodiscard]] std::vector<PluginPermission> ParsePermissions(
		const QJsonArray &permissions,
		QString *error) {
	auto result = std::vector<PluginPermission>();
	for (const auto &value : permissions) {
		if (!value.isString()) {
			*error = QStringLiteral("Store permissions must be strings.");
			return {};
		}
		auto parsed = PluginPermission::UiModify;
		if (!ParsePluginPermission(value.toString(), &parsed)) {
			*error = QStringLiteral("Unsupported store permission: %1")
				.arg(value.toString());
			return {};
		}
		result.push_back(parsed);
	}
	return result;
}

[[nodiscard]] QString SanitizePluginId(QString id) {
	id = id.trimmed().toLower();
	id.replace(QChar('/'), QChar('-'));
	id.replace(QChar('\\'), QChar('-'));
	id.replace(QStringLiteral(".."), QStringLiteral(""));
	while (id.contains(QStringLiteral("--"))) {
		id.replace(QStringLiteral("--"), QStringLiteral("-"));
	}
	return id;
}

[[nodiscard]] QString ResolveRelativePackageUrl(
		const QString &packageUrl,
		const RepoContext &repo) {
	if (packageUrl.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
		|| packageUrl.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)) {
		return packageUrl;
	}
	if (repo.fullName.isEmpty() || repo.defaultBranch.isEmpty()) {
		return {};
	}
	auto relative = packageUrl;
	while (relative.startsWith(QChar('/'))) {
		relative = relative.mid(1);
	}
	return QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3")
		.arg(repo.fullName, repo.defaultBranch, relative);
}

[[nodiscard]] bool ParseEntryObject(
		const QJsonObject &object,
		RemotePluginEntry *out,
		QString *error,
		const RepoContext &repo = {}) {
	Expects(out != nullptr);
	Expects(error != nullptr);

	auto entry = RemotePluginEntry();
	entry.id = object.value(QStringLiteral("id")).toString();
	entry.name = object.value(QStringLiteral("name")).toString();
	entry.version = object.value(QStringLiteral("version")).toString();
	entry.author = object.value(QStringLiteral("author")).toString();
	entry.description = object.value(QStringLiteral("description")).toString();
	entry.type = ParsePluginType(
		object.value(QStringLiteral("type")).toString());
	entry.minApiVersion = object.value(QStringLiteral("minApiVersion"))
		.toString(QStringLiteral("1"));
	entry.packageUrl = object.value(QStringLiteral("packageUrl")).toString();
	if (entry.packageUrl.isEmpty()) {
		entry.packageUrl = object.value(QStringLiteral("url")).toString();
	}
	entry.repoUrl = object.value(QStringLiteral("repoUrl")).toString();
	if (entry.repoUrl.isEmpty()) {
		entry.repoUrl = object.value(QStringLiteral("repository")).toString();
	}
	if (entry.repoUrl.isEmpty()) {
		entry.repoUrl = repo.htmlUrl;
	}
	entry.icon = object.value(QStringLiteral("icon")).toString().trimmed();
	if (!entry.icon.isEmpty()
		&& !entry.icon.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)
		&& !repo.fullName.isEmpty()) {
		entry.icon = ResolveRelativePackageUrl(entry.icon, repo);
	}

	if (entry.id.isEmpty() && !repo.fullName.isEmpty()) {
		entry.id = SanitizePluginId(repo.fullName);
	}
	if (entry.name.isEmpty() && !repo.fullName.isEmpty()) {
		entry.name = repo.fullName.section(QChar('/'), 1);
	}
	if (entry.author.isEmpty()) {
		entry.author = repo.owner.isEmpty()
			? QStringLiteral("community")
			: (QStringLiteral("@") + repo.owner);
	}
	if (entry.description.isEmpty()) {
		entry.description = repo.description;
	}

	if (!entry.packageUrl.isEmpty()
		&& !entry.packageUrl.startsWith(
			QStringLiteral("http"),
			Qt::CaseInsensitive)) {
		entry.packageUrl = ResolveRelativePackageUrl(entry.packageUrl, repo);
	}
	if (entry.packageUrl.isEmpty() && !repo.fullName.isEmpty()) {
		// Whole-repo archive as a last resort (GitHub zipball).
		entry.packageUrl = QStringLiteral(
			"https://github.com/%1/archive/refs/heads/%2.zip")
			.arg(repo.fullName, repo.defaultBranch.isEmpty()
				? QStringLiteral("main")
				: repo.defaultBranch);
	}

	entry.id = SanitizePluginId(entry.id);

	if (entry.id.isEmpty() || entry.name.isEmpty()) {
		*error = QStringLiteral("Store entry needs id and name.");
		return false;
	}
	if (entry.packageUrl.isEmpty()) {
		*error = QStringLiteral("Store entry \"%1\" needs packageUrl/url.")
			.arg(entry.id);
		return false;
	}
	if (!IsTrustedPluginDownloadUrl(entry.packageUrl)) {
		*error = QStringLiteral(
			"Store entry \"%1\" package URL must be a trusted GitHub HTTPS URL.")
			.arg(entry.id);
		return false;
	}
	if (entry.type == PluginType::Unknown) {
		entry.type = PluginType::Utility;
	}
	if (entry.version.isEmpty()) {
		entry.version = QStringLiteral("1.0.0");
	}

	const auto tags = object.value(QStringLiteral("tags"));
	if (tags.isArray()) {
		for (const auto &tag : tags.toArray()) {
			if (tag.isString()) {
				entry.tags.push_back(tag.toString());
			}
		}
	}
	if (!entry.tags.contains(QString::fromUtf8(kDiscoverTopic))) {
		entry.tags.push_back(QString::fromUtf8(kDiscoverTopic));
	}

	const auto permissions = object.value(QStringLiteral("permissions"));
	if (permissions.isArray()) {
		entry.permissions = ParsePermissions(permissions.toArray(), error);
		if (!error->isEmpty()) {
			return false;
		}
	}

	*out = std::move(entry);
	return true;
}

[[nodiscard]] bool IsSafeZipPath(const QString &name) {
	if (name.isEmpty()
		|| name.contains(QStringLiteral(".."))
		|| name.startsWith(QChar('/'))
		|| name.startsWith(QChar('\\'))
		|| (name.size() >= 2 && name[1] == QChar(':'))) {
		return false;
	}
	return true;
}

[[nodiscard]] RemotePluginEntry SynthesizeFromRepo(const RepoContext &repo) {
	auto entry = RemotePluginEntry();
	entry.id = SanitizePluginId(repo.fullName);
	entry.name = repo.fullName.section(QChar('/'), 1);
	entry.version = QStringLiteral("1.0.0");
	entry.author = repo.owner.isEmpty()
		? QStringLiteral("community")
		: (QStringLiteral("@") + repo.owner);
	entry.description = repo.description.isEmpty()
		? QStringLiteral("GitHub plugin repository (%1 ★).")
			.arg(repo.stars)
		: repo.description;
	entry.type = PluginType::Utility;
	entry.minApiVersion = QStringLiteral("1");
	entry.repoUrl = repo.htmlUrl;
	entry.packageUrl = QStringLiteral(
		"https://github.com/%1/archive/refs/heads/%2.zip")
		.arg(repo.fullName, repo.defaultBranch.isEmpty()
			? QStringLiteral("main")
			: repo.defaultBranch);
	entry.tags.clear();
	entry.tags.push_back(QString::fromUtf8(kDiscoverTopic));
	entry.tags.push_back(QStringLiteral("github"));
	return entry;
}

} // namespace

bool IsTrustedPluginDownloadUrl(const QString &url) {
	const auto parsed = QUrl(url);
	if (!parsed.isValid()
		|| parsed.scheme().compare(
			QStringLiteral("https"),
			Qt::CaseInsensitive) != 0) {
		return false;
	}
	return IsTrustedGitHubHost(parsed.host());
}

bool IsTrustedPluginRepoUrl(const QString &url) {
	return !CanonicalGitHubRepoKey(url).isEmpty();
}

QString CanonicalGitHubRepoKey(const QString &url) {
	const auto parsed = QUrl(url.trimmed());
	if (!parsed.isValid()
		|| parsed.scheme().compare(
			QStringLiteral("https"),
			Qt::CaseInsensitive) != 0) {
		return {};
	}
	const auto host = parsed.host().toLower();
	const auto githubHost = (host == QStringLiteral("github.com")
		|| host == QStringLiteral("www.github.com")
		|| host == QStringLiteral("raw.githubusercontent.com")
		|| host == QStringLiteral("codeload.github.com"));
	if (!githubHost) {
		return {};
	}
	auto parts = parsed.path().split(QChar('/'), Qt::SkipEmptyParts);
	if (parts.size() < 2) {
		return {};
	}
	auto owner = parts.at(0).trimmed().toLower();
	auto repo = parts.at(1).trimmed().toLower();
	if (repo.endsWith(QStringLiteral(".git"))) {
		repo.chop(4);
	}
	if (owner.isEmpty()
		|| repo.isEmpty()
		|| owner == QStringLiteral(".")
		|| owner == QStringLiteral("..")
		|| repo == QStringLiteral(".")
		|| repo == QStringLiteral("..")) {
		return {};
	}
	return owner + QChar('/') + repo;
}

[[nodiscard]] QString ReadInstalledStoreRepoKey(const QString &pluginRoot) {
	auto file = QFile(pluginRoot + QStringLiteral("/.plugingram-store.json"));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return {};
	}
	const auto object = document.object();
	auto key = CanonicalGitHubRepoKey(
		object.value(QStringLiteral("repoUrl")).toString());
	if (key.isEmpty()) {
		// Legacy installs may only have packageUrl pinned.
		key = CanonicalGitHubRepoKey(
			object.value(QStringLiteral("packageUrl")).toString());
	}
	return key;
}

bool CanInstallOrUpdateStoreEntry(
		const QString &pluginsRoot,
		const RemotePluginEntry &entry,
		QString *error) {
	const auto id = SanitizePluginId(entry.id);
	if (id.isEmpty()
		|| id == QStringLiteral("store")
		|| id == QStringLiteral(".")
		|| id == QStringLiteral("..")) {
		if (error) {
			*error = QStringLiteral("Invalid or reserved plugin id.");
		}
		return false;
	}
	const auto remoteKey = CanonicalGitHubRepoKey(entry.repoUrl);
	if (remoteKey.isEmpty()) {
		if (error) {
			*error = QStringLiteral(
				"Store entry must have a trusted github.com repository URL.");
		}
		return false;
	}
	if (!IsTrustedPluginDownloadUrl(entry.packageUrl)) {
		if (error) {
			*error = QStringLiteral(
				"Package URL must be a trusted GitHub HTTPS link.");
		}
		return false;
	}

	const auto targetRoot = pluginsRoot + QChar('/') + id;
	if (!QDir(targetRoot).exists()) {
		return true;
	}
	const auto installedKey = ReadInstalledStoreRepoKey(targetRoot);
	if (installedKey.isEmpty()) {
		if (error) {
			*error = QStringLiteral(
				"Refusing to overwrite a local plugin that was not "
				"installed from the store.");
		}
		return false;
	}
	if (installedKey != remoteKey) {
		if (error) {
			*error = QStringLiteral(
				"Plugin id is pinned to another GitHub repository.");
		}
		return false;
	}
	return true;
}

QString ResolveStoreIconUrl(const RemotePluginEntry &entry) {
	auto icon = entry.icon.trimmed();
	if (icon.isEmpty()) {
		icon = QStringLiteral("icon.png");
	}
	if (icon.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
		return IsTrustedPluginDownloadUrl(icon) ? icon : QString();
	}
	// Relative path → raw.githubusercontent.com via repoUrl.
	// repoUrl: https://github.com/owner/repo
	const auto repo = entry.repoUrl.trimmed();
	QRegularExpression re(
		QStringLiteral(R"(^https?://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$)"),
		QRegularExpression::CaseInsensitiveOption);
	const auto match = re.match(repo);
	if (!match.hasMatch()) {
		return {};
	}
	auto relative = icon;
	while (relative.startsWith(QChar('/'))) {
		relative = relative.mid(1);
	}
	return QStringLiteral("https://raw.githubusercontent.com/%1/%2/main/%3")
		.arg(match.captured(1), match.captured(2), relative);
}

QString DefaultStoreCatalogUrl() {
	return QStringLiteral("github-search://%1")
		.arg(QString::fromUtf8(kDiscoverTopic));
}

QString ResolveStoreCatalogUrl(const QString &pluginsRoot) {
	if (const auto env = qEnvironmentVariable("PLUGINGRAM_STORE_URL");
		!env.isEmpty()) {
		return env.trimmed();
	}
	const auto overridePath = pluginsRoot
		+ QStringLiteral("/store/catalog.url");
	auto file = QFile(overridePath);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		const auto line = QString::fromUtf8(file.readLine()).trimmed();
		if (!line.isEmpty() && !line.startsWith(QChar('#'))) {
			return line;
		}
	}
	return DefaultStoreCatalogUrl();
}

RemoteIndexParseResult ParseRemoteIndexBytes(const QByteArray &bytes) {
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		return {
			{},
			QStringLiteral("Invalid store JSON: %1")
				.arg(parseError.errorString()),
		};
	}

	auto index = RemotePluginIndex();
	QJsonArray entriesArray;

	if (document.isArray()) {
		index.schemaVersion = QStringLiteral("1");
		index.catalogTitle = QStringLiteral("GitHub Plugin Store");
		entriesArray = document.array();
	} else if (document.isObject()) {
		const auto root = document.object();
		index.schemaVersion = root.value(QStringLiteral("schemaVersion"))
			.toString(QStringLiteral("1"));
		index.catalogTitle = root.value(QStringLiteral("catalogTitle"))
			.toString(QStringLiteral("GitHub Plugin Store"));
		const auto entries = root.value(QStringLiteral("entries"));
		if (!entries.isArray()) {
			return {
				std::move(index),
				QStringLiteral("Store JSON needs an \"entries\" array."),
			};
		}
		entriesArray = entries.toArray();
	} else {
		return { {}, QStringLiteral("Store JSON must be an object or array.") };
	}

	for (const auto &value : entriesArray) {
		if (!value.isObject()) {
			return { {}, QStringLiteral("Store entries must be objects.") };
		}
		auto entry = RemotePluginEntry();
		auto error = QString();
		if (!ParseEntryObject(value.toObject(), &entry, &error)) {
			// Skip bad cache rows instead of failing the whole catalog.
			continue;
		}
		index.entries.push_back(std::move(entry));
	}
	return { std::move(index), QString() };
}

RemoteIndexParseResult ParseRemoteIndexFile(const QString &path) {
	auto file = QFile(path);
	if (!file.exists()) {
		return { {}, QString() };
	}
	if (!file.open(QIODevice::ReadOnly)) {
		return { {}, QStringLiteral("Could not open store index file.") };
	}
	return ParseRemoteIndexBytes(file.readAll());
}

bool SaveRemoteIndexFile(
		const QString &path,
		const RemotePluginIndex &index,
		QString *error) {
	auto entries = QJsonArray();
	for (const auto &entry : index.entries) {
		auto permissions = QJsonArray();
		for (const auto permission : entry.permissions) {
			permissions.push_back(SerializePluginPermission(permission));
		}
		auto tags = QJsonArray();
		for (const auto &tag : entry.tags) {
			tags.push_back(tag);
		}
		entries.push_back(QJsonObject{
			{ QStringLiteral("id"), entry.id },
			{ QStringLiteral("name"), entry.name },
			{ QStringLiteral("version"), entry.version },
			{ QStringLiteral("author"), entry.author },
			{ QStringLiteral("description"), entry.description },
			{ QStringLiteral("type"), SerializePluginType(entry.type) },
			{ QStringLiteral("minApiVersion"), entry.minApiVersion },
			{ QStringLiteral("packageUrl"), entry.packageUrl },
			{ QStringLiteral("repoUrl"), entry.repoUrl },
			{ QStringLiteral("icon"), entry.icon },
			{ QStringLiteral("tags"), tags },
			{ QStringLiteral("permissions"), permissions },
		});
	}
	const auto document = QJsonDocument(QJsonObject{
		{ QStringLiteral("schemaVersion"),
			index.schemaVersion.isEmpty()
				? QStringLiteral("1")
				: index.schemaVersion },
		{ QStringLiteral("catalogTitle"),
			index.catalogTitle.isEmpty()
				? QStringLiteral("GitHub Plugin Store")
				: index.catalogTitle },
		{ QStringLiteral("entries"), entries },
	});

	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error) {
			*error = QStringLiteral("Could not write store index.");
		}
		return false;
	}
	file.write(document.toJson(QJsonDocument::Indented));
	return true;
}

bool InstallPluginArchive(
		const QByteArray &zipBytes,
		const QString &pluginsRoot,
		const QString &pluginId,
		QString *error) {
	if (zipBytes.isEmpty()) {
		if (error) {
			*error = QStringLiteral("Empty plugin package.");
		}
		return false;
	}
	if (pluginId.isEmpty()
		|| pluginId == QStringLiteral("store")
		|| pluginId.contains(QChar('/'))
		|| pluginId.contains(QChar('\\'))
		|| pluginId.contains(QStringLiteral(".."))) {
		if (error) {
			*error = QStringLiteral("Invalid plugin id.");
		}
		return false;
	}

	auto topLevels = QStringList();
	auto fileNames = QStringList();
	{
		auto zip = zlib::FileToRead(zipBytes);
		if (zip.goToFirstFile() != UNZ_OK) {
			if (error) {
				*error = QStringLiteral("Not a valid zip plugin package.");
			}
			return false;
		}
		do {
			const auto name = zip.getCurrentFileName();
			if (!IsSafeZipPath(name)) {
				if (error) {
					*error = QStringLiteral("Unsafe path inside plugin zip.");
				}
				return false;
			}
			fileNames.push_back(name);
			const auto slash = name.indexOf(QChar('/'));
			const auto top = (slash >= 0) ? name.left(slash) : name;
			if (!top.isEmpty() && !topLevels.contains(top)) {
				topLevels.push_back(top);
			}
			const auto jump = zip.goToNextFile();
			if (jump == UNZ_END_OF_LIST_OF_FILE) {
				break;
			} else if (jump != UNZ_OK) {
				if (error) {
					*error = QStringLiteral("Failed to read plugin zip.");
				}
				return false;
			}
		} while (true);
	}

	auto stripPrefix = QString();
	if (topLevels.size() == 1) {
		const auto folder = topLevels.front() + QChar('/');
		auto allInside = true;
		for (const auto &name : fileNames) {
			if (name != topLevels.front() && !name.startsWith(folder)) {
				allInside = false;
				break;
			}
		}
		if (allInside) {
			stripPrefix = folder;
		}
	}

	const auto targetRoot = pluginsRoot + QChar('/') + pluginId;
	if (QDir(targetRoot).exists()) {
		QDir(targetRoot).removeRecursively();
	}
	QDir().mkpath(targetRoot);

	{
		auto zip = zlib::FileToRead(zipBytes);
		if (zip.goToFirstFile() != UNZ_OK) {
			if (error) {
				*error = QStringLiteral("Could not reopen plugin zip.");
			}
			return false;
		}
		do {
			const auto name = zip.getCurrentFileName();
			if (name.endsWith(QChar('/'))) {
				const auto jump = zip.goToNextFile();
				if (jump == UNZ_END_OF_LIST_OF_FILE) {
					break;
				}
				continue;
			}
			auto relative = name;
			if (!stripPrefix.isEmpty()) {
				if (relative == stripPrefix.left(stripPrefix.size() - 1)) {
					const auto jump = zip.goToNextFile();
					if (jump == UNZ_END_OF_LIST_OF_FILE) {
						break;
					}
					continue;
				}
				if (relative.startsWith(stripPrefix)) {
					relative = relative.mid(stripPrefix.size());
				}
			}
			if (relative.isEmpty() || !IsSafeZipPath(relative)) {
				if (error) {
					*error = QStringLiteral("Unsafe path inside plugin zip.");
				}
				return false;
			}

			const auto content = zip.readCurrentFileContent(kMaxZipEntryBytes);
			if (content.isEmpty() && zip.error() != UNZ_OK) {
				if (error) {
					*error = QStringLiteral("Failed to extract \"%1\".").arg(name);
				}
				return false;
			}
			const auto outPath = targetRoot + QChar('/') + relative;
			if (!PathIsInsideRoot(targetRoot, outPath)) {
				if (error) {
					*error = QStringLiteral(
						"Zip entry escapes the plugin install directory.");
				}
				return false;
			}
			QDir().mkpath(QFileInfo(outPath).absolutePath());
			auto out = QFile(outPath);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				if (error) {
					*error = QStringLiteral("Could not write plugin files.");
				}
				return false;
			}
			out.write(content);

			const auto jump = zip.goToNextFile();
			if (jump == UNZ_END_OF_LIST_OF_FILE) {
				break;
			} else if (jump != UNZ_OK) {
				if (error) {
					*error = QStringLiteral("Failed to read plugin zip.");
				}
				return false;
			}
		} while (true);
	}

	return true;
}

StoreService::StoreService(Fn<QString()> pluginsRoot)
: _pluginsRoot(std::move(pluginsRoot)) {
}

bool StoreService::busy() const {
	return _inflight > 0;
}

void StoreService::fetchTrustedBytes(
		const QString &url,
		int maxBytes,
		Fn<void(QByteArray bytes, QString error)> done) {
	// Icon previews must not block Install via busy().
	getBytes(url, maxBytes, nullptr, std::move(done), {}, false);
}

void StoreService::getBytes(
		const QString &url,
		int maxBytes,
		Fn<void(float64 progress)> progress,
		Fn<void(QByteArray bytes, QString error)> done,
		const QByteArray &accept,
		bool trackBusy) {
	const auto report = [=](float64 value) {
		if (progress) {
			progress(std::clamp(value, 0., 1.));
		}
	};

	if (!IsTrustedPluginDownloadUrl(url)) {
		done({}, QStringLiteral(
			"Only trusted GitHub HTTPS downloads are allowed."));
		return;
	}

	if (trackBusy) {
		++_inflight;
	}
	report(0.02);
	// Bust CDN/browser-style caches so store refresh sees latest GitHub files.
	auto request = QNetworkRequest(QUrl(WithCacheBust(url)));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setAttribute(
		QNetworkRequest::CacheLoadControlAttribute,
		QNetworkRequest::AlwaysNetwork);
	request.setHeader(
		QNetworkRequest::UserAgentHeader,
		QStringLiteral("PlugingramStore/1.0 (+https://github.com/plugingram)"));
	request.setRawHeader("Cache-Control", "no-cache");
	request.setRawHeader("Pragma", "no-cache");
	if (!accept.isEmpty()) {
		request.setRawHeader("Accept", accept);
	}
	const auto reply = _nam.get(request);
	auto timer = new QTimer(reply);
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, reply, [reply] {
		reply->abort();
	});
	timer->start(kRequestTimeoutMs);

	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		this,
		[=](qint64 received, qint64 total) {
			if (total > 0) {
				report(0.05 + 0.85 * (double(received) / double(total)));
			} else if (received > 0) {
				report(std::min(0.05 + received / double(maxBytes), 0.88));
			}
		});

	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		timer->stop();
		if (trackBusy) {
			--_inflight;
		}
		auto bytes = QByteArray();
		auto errorText = QString();
		if (reply->error() != QNetworkReply::NoError) {
			errorText = reply->errorString();
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 403 || status == 429) {
				errorText = QStringLiteral(
					"GitHub rate limit reached. Try again later.");
			}
		} else if (!IsTrustedPluginDownloadUrl(reply->url().toString())) {
			// Redirects must stay on trusted GitHub hosts.
			errorText = QStringLiteral(
				"Download redirected outside trusted GitHub hosts.");
		} else {
			bytes = reply->readAll();
			if (bytes.size() > maxBytes) {
				bytes.clear();
				errorText = QStringLiteral("Downloaded file is too large.");
			} else {
				report(0.9);
			}
		}
		reply->deleteLater();
		done(std::move(bytes), std::move(errorText));
	});
}

void StoreService::finishDiscover(
		RemotePluginIndex index,
		const QString &cachePath,
		Fn<void(bool ok, QString error)> done,
		const QString &warning) {
	index.schemaVersion = QStringLiteral("1");
	if (index.catalogTitle.isEmpty()) {
		index.catalogTitle = QStringLiteral("GitHub · plugingram-plugin");
	}
	auto saveError = QString();
	if (!index.entries.empty()
		&& !SaveRemoteIndexFile(cachePath, index, &saveError)) {
		done(false, saveError);
		return;
	}
	if (index.entries.empty()) {
		done(false, warning.isEmpty()
			? QStringLiteral(
				"No GitHub plugins found yet. Authors should add topic "
				"\"plugingram-plugin\" and a plugingram.json file.")
			: warning);
		return;
	}
	done(true, warning);
}

void StoreService::discoverFromGitHub(Fn<void(bool ok, QString error)> done) {
	const auto root = _pluginsRoot();
	const auto cachePath = root + QStringLiteral("/store/index.json");
	const auto queries = DiscoverSearchQueries();

	struct SearchState {
		QHash<QString, QJsonObject> repos;
		QString lastError;
		int pending = 0;
		bool finished = false;
	};
	const auto search = std::make_shared<SearchState>();
	search->pending = queries.size();

	const auto hydrate = [=] {
		auto repos = std::vector<RepoContext>();
		for (auto it = search->repos.constBegin();
			it != search->repos.constEnd();
			++it) {
			const auto &obj = it.value();
			auto repo = RepoContext();
			repo.fullName = obj.value(QStringLiteral("full_name")).toString();
			repo.htmlUrl = obj.value(QStringLiteral("html_url")).toString();
			repo.defaultBranch = obj.value(QStringLiteral("default_branch"))
				.toString(QStringLiteral("main"));
			repo.description = obj.value(QStringLiteral("description")).toString();
			repo.stars = obj.value(QStringLiteral("stargazers_count")).toInt();
			repo.owner = obj.value(QStringLiteral("owner")).toObject()
				.value(QStringLiteral("login")).toString();
			if (repo.fullName.isEmpty() || repo.htmlUrl.isEmpty()) {
				continue;
			}
			repos.push_back(std::move(repo));
			if (int(repos.size()) >= kMaxDiscoverRepos) {
				break;
			}
		}

		if (repos.empty()) {
			const auto local = ParseRemoteIndexFile(cachePath);
			if (!local.index.entries.empty()) {
				done(true, search->lastError.isEmpty()
					? QStringLiteral("Using offline cache.")
					: QStringLiteral("Using offline cache (%1)")
						.arg(search->lastError));
				return;
			}
			finishDiscover({}, cachePath, done, search->lastError);
			return;
		}

		struct SharedState {
			RemotePluginIndex index;
			int pending = 0;
			bool finished = false;
		};
		const auto state = std::make_shared<SharedState>();
		state->index.catalogTitle = QStringLiteral(
			"GitHub · plugingram-plugin");
		state->pending = int(repos.size());

		const auto completeOne = [=] {
			if (state->finished) {
				return;
			}
			--state->pending;
			if (state->pending > 0) {
				return;
			}
			state->finished = true;
			std::sort(
				state->index.entries.begin(),
				state->index.entries.end(),
				[](const RemotePluginEntry &a, const RemotePluginEntry &b) {
					return a.name.localeAwareCompare(b.name) < 0;
				});
			finishDiscover(std::move(state->index), cachePath, done);
		};

		for (const auto &repo : repos) {
			const auto manifestUrl = QStringLiteral(
				"https://raw.githubusercontent.com/%1/%2/plugingram.json")
				.arg(repo.fullName, repo.defaultBranch);
			getBytes(manifestUrl, 256 * 1024, nullptr, [=](
					QByteArray manifestBytes,
					QString manifestError) {
				if (manifestError.isEmpty() && !manifestBytes.isEmpty()) {
					QJsonParseError jsonError;
					const auto doc = QJsonDocument::fromJson(
						manifestBytes,
						&jsonError);
					if (jsonError.error == QJsonParseError::NoError
						&& doc.isObject()) {
						auto entry = RemotePluginEntry();
						auto parseErr = QString();
						if (ParseEntryObject(
								doc.object(),
								&entry,
								&parseErr,
								repo)) {
							state->index.entries.push_back(std::move(entry));
							completeOne();
							return;
						}
					}
				}
				state->index.entries.push_back(SynthesizeFromRepo(repo));
				completeOne();
			});
		}
	};

	const auto completeSearch = [=] {
		if (search->finished) {
			return;
		}
		--search->pending;
		if (search->pending > 0) {
			return;
		}
		search->finished = true;
		hydrate();
	};

	for (const auto &q : queries) {
		auto query = QUrlQuery();
		query.addQueryItem(QStringLiteral("q"), q);
		query.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
		query.addQueryItem(QStringLiteral("order"), QStringLiteral("desc"));
		query.addQueryItem(
			QStringLiteral("per_page"),
			QString::number(kMaxDiscoverRepos));
		auto searchUrl = QUrl(
			QStringLiteral("https://api.github.com/search/repositories"));
		searchUrl.setQuery(query);

		getBytes(
			searchUrl.toString(QUrl::FullyEncoded),
			kMaxCatalogBytes,
			nullptr,
			[=](QByteArray bytes, QString error) {
				if (!error.isEmpty() || bytes.isEmpty()) {
					if (!error.isEmpty()) {
						search->lastError = error;
					}
					completeSearch();
					return;
				}
				QJsonParseError parseError;
				const auto document = QJsonDocument::fromJson(bytes, &parseError);
				if (parseError.error == QJsonParseError::NoError
					&& document.isObject()) {
					const auto items = document.object()
						.value(QStringLiteral("items"));
					if (items.isArray()) {
						for (const auto &value : items.toArray()) {
							if (!value.isObject()) {
								continue;
							}
							const auto obj = value.toObject();
							const auto fullName = obj
								.value(QStringLiteral("full_name"))
								.toString();
							if (!fullName.isEmpty()) {
								search->repos.insert(fullName, obj);
							}
						}
					}
				}
				completeSearch();
			},
			QByteArray("application/vnd.github+json"));
	}
}

void StoreService::fetchCatalog(Fn<void(bool ok, QString error)> done) {
	const auto root = _pluginsRoot();
	const auto cachePath = root + QStringLiteral("/store/index.json");
	const auto url = ResolveStoreCatalogUrl(root);

	// Explicit debug override only.
	if (url.compare(QStringLiteral("local"), Qt::CaseInsensitive) == 0
		|| url.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)
		|| QUrl(url).isLocalFile()) {
		const auto localUrl = (url.compare(
				QStringLiteral("local"),
				Qt::CaseInsensitive) == 0)
			? QUrl::fromLocalFile(cachePath).toString()
			: url;
		// Local override is debug-only and bypasses GitHub host checks via file.
		auto file = QFile(QUrl(localUrl).toLocalFile());
		if (!file.open(QIODevice::ReadOnly)) {
			const auto local = ParseRemoteIndexFile(cachePath);
			done(!local.index.entries.empty(), local.error);
			return;
		}
		auto parsed = ParseRemoteIndexBytes(file.readAll());
		if (!parsed.error.isEmpty() && parsed.index.entries.empty()) {
			done(false, parsed.error);
			return;
		}
		SaveRemoteIndexFile(cachePath, parsed.index, nullptr);
		done(true, {});
		return;
	}

	if (url.startsWith(QStringLiteral("github-search:"), Qt::CaseInsensitive)
		|| url.contains(QStringLiteral("api.github.com/search"))
		|| url == DefaultStoreCatalogUrl()) {
		discoverFromGitHub(std::move(done));
		return;
	}

	// Legacy single plugins.json raw URL still supported.
	if (!IsTrustedPluginDownloadUrl(url)) {
		done(false, QStringLiteral("Catalog URL is not a trusted GitHub HTTPS URL."));
		return;
	}
	getBytes(url, kMaxCatalogBytes, nullptr, [=](QByteArray bytes, QString error) {
		if (!error.isEmpty() || bytes.isEmpty()) {
			const auto local = ParseRemoteIndexFile(cachePath);
			if (!local.index.entries.empty()) {
				done(true, error.isEmpty()
					? QString()
					: QStringLiteral("Using offline cache (%1)").arg(error));
				return;
			}
			done(false, error.isEmpty()
				? QStringLiteral("Catalog is empty.")
				: error);
			return;
		}
		auto parsed = ParseRemoteIndexBytes(bytes);
		if (parsed.index.entries.empty()) {
			done(false, parsed.error.isEmpty()
				? QStringLiteral("Catalog is empty.")
				: parsed.error);
			return;
		}
		auto saveError = QString();
		if (!SaveRemoteIndexFile(cachePath, parsed.index, &saveError)) {
			done(false, saveError);
			return;
		}
		done(true, {});
	});
}

void StoreService::installEntry(
		const RemotePluginEntry &entry,
		Fn<void(float64 progress)> progress,
		Fn<void(bool ok, QString error)> done) {
	const auto root = _pluginsRoot();
	auto entryCopy = entry;
	entryCopy.id = SanitizePluginId(entry.id);
	auto gateError = QString();
	if (!CanInstallOrUpdateStoreEntry(root, entryCopy, &gateError)) {
		done(false, gateError);
		return;
	}
	const auto url = entryCopy.packageUrl;
	if (!IsTrustedPluginDownloadUrl(url)) {
		done(false, QStringLiteral(
			"Refusing download: package URL is not a trusted GitHub HTTPS link."));
		return;
	}
	const auto pluginId = entryCopy.id;
	getBytes(url, kMaxPackageBytes, progress, [=](QByteArray bytes, QString error) {
		if (!error.isEmpty() || bytes.isEmpty()) {
			done(false, error.isEmpty()
				? QStringLiteral("Empty package download.")
				: error);
			return;
		}
		if (progress) {
			progress(0.93);
		}
		QTimer::singleShot(0, this, [=] {
			if (progress) {
				progress(0.97);
			}
			auto installError = QString();
			const auto ok = InstallPluginArchive(
				bytes,
				root,
				pluginId,
				&installError);
			if (!ok) {
				if (progress) {
					progress(0.97);
				}
				done(false, installError);
				return;
			}
			EnsurePlugingramJsonFromEntry(root, entryCopy);
			WriteInstalledStoreMeta(root, entryCopy);

			// Optional remote icon from plugingram.json / store entry.
			auto iconRef = entryCopy.icon;
			if (iconRef.isEmpty()) {
				auto pg = QFile(
					root
						+ QChar('/')
						+ pluginId
						+ QStringLiteral("/plugingram.json"));
				if (pg.open(QIODevice::ReadOnly)) {
					const auto doc = QJsonDocument::fromJson(pg.readAll());
					if (doc.isObject()) {
						iconRef = doc.object()
							.value(QStringLiteral("icon"))
							.toString()
							.trimmed();
					}
				}
			}
			if (iconRef.startsWith(
					QStringLiteral("https://"),
					Qt::CaseInsensitive)
				&& IsTrustedPluginDownloadUrl(iconRef)) {
				getBytes(iconRef, 2 * 1024 * 1024, nullptr, [=](
						QByteArray iconBytes,
						QString iconError) {
					if (iconError.isEmpty() && !iconBytes.isEmpty()) {
						auto ext = QStringLiteral("png");
						if (iconRef.contains(
								QStringLiteral(".jpg"),
								Qt::CaseInsensitive)
							|| iconRef.contains(
								QStringLiteral(".jpeg"),
								Qt::CaseInsensitive)) {
							ext = QStringLiteral("jpg");
						} else if (iconRef.contains(
								QStringLiteral(".webp"),
								Qt::CaseInsensitive)) {
							ext = QStringLiteral("webp");
						}
						const auto outPath = root
							+ QChar('/')
							+ pluginId
							+ QStringLiteral("/icon.")
							+ ext;
						auto out = QFile(outPath);
						if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
							out.write(iconBytes);
						}
					}
					if (progress) {
						progress(1.);
					}
					done(true, {});
				});
				return;
			}
			if (progress) {
				progress(1.);
			}
			done(true, {});
		});
	});
}

} // namespace PluginSystem
