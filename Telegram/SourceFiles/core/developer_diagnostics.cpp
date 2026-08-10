#include "core/developer_diagnostics.h"

#include "core/application.h"
#include "core/version.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "plugin_system/plugins_common.h"
#include "plugin_system/plugins_manager.h"
#include "settings.h"
#include "data/data_user.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QStandardPaths>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <vector>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <Psapi.h>
#endif // Q_OS_WIN

namespace Core::DeveloperDiagnostics {
namespace {

constexpr auto kMaxActions = 256;
constexpr auto kMaxErrors = 128;
constexpr auto kLineMax = 480;

std::atomic<bool> Started = false;
crl::time StartedAt = 0;

QMutex RingMutex;
std::deque<QString> Actions;
std::deque<QString> Errors;

std::mutex FlushMutex;
std::terminate_handler PreviousTerminate = nullptr;

[[nodiscard]] QString SanitizeLine(QString line) {
	line.replace('\n', ' ');
	line.replace('\r', ' ');
	if (line.size() > kLineMax) {
		line = line.left(kLineMax) + u"…"_q;
	}
	return line;
}

[[nodiscard]] QString Stamp() {
	return QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss.zzz"_q);
}

void EnsureLogsDir() {
	QDir().mkpath(LogsDirectory());
}

void AppendFile(const QString &path, const QString &line) {
	EnsureLogsDir();
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		return;
	}
	file.write(line.toUtf8());
	file.write("\n", 1);
}

void PushRing(std::deque<QString> &ring, int max, const QString &line) {
	ring.push_back(line);
	while (int(ring.size()) > max) {
		ring.pop_front();
	}
}

QStringList CopyRing(const std::deque<QString> &ring, int limit) {
	QStringList out;
	const auto from = std::max(0, int(ring.size()) - limit);
	for (auto i = from; i < int(ring.size()); ++i) {
		out.push_back(ring[i]);
	}
	return out;
}

void FlushCrashLocked(const char *reason) {
	EnsureLogsDir();
	const auto path = LogsDirectory()
		+ u"/crash_"_q
		+ QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_q)
		+ u".log"_q;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		return;
	}
	auto write = [&](const QString &line) {
		file.write(line.toUtf8());
		file.write("\n", 1);
	};
	write(u"=== Plugingram crash / terminate ==="_q);
	write(u"reason: %1"_q.arg(QString::fromUtf8(reason ? reason : "unknown")));
	write(u"time: %1"_q.arg(Stamp()));
	write(u"version: %1"_q.arg(QString::fromUtf8(AppVersionStr)));
	write(u"workingDir: %1"_q.arg(cWorkingDir()));
	write(u""_q);
	write(u"--- last actions ---"_q);
	{
		QMutexLocker lock(&RingMutex);
		for (const auto &line : Actions) {
			write(line);
		}
		write(u""_q);
		write(u"--- last errors ---"_q);
		for (const auto &line : Errors) {
			write(line);
		}
	}
	file.flush();

	// Also mirror into last_actions for quick inspection.
	QFile::remove(ActionsLogPath());
	QFile::copy(path, ActionsLogPath());
}

void FlushCrash(const char *reason) {
	std::lock_guard lock(FlushMutex);
	FlushCrashLocked(reason);
}

void OnTerminate() {
	FlushCrash("std::terminate");
	if (PreviousTerminate) {
		PreviousTerminate();
	} else {
		std::abort();
	}
}

void OnSignal(int sig) {
	char buf[64];
	std::snprintf(buf, sizeof(buf), "signal %d", sig);
	FlushCrash(buf);
	std::_Exit(128 + sig);
}

#ifdef Q_OS_WIN
LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS *) {
	FlushCrash("UnhandledExceptionFilter");
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif // Q_OS_WIN

[[nodiscard]] QString MemoryText() {
#ifdef Q_OS_WIN
	PROCESS_MEMORY_COUNTERS data = { sizeof(data) };
	if (GetProcessMemoryInfo(GetCurrentProcess(), &data, sizeof(data))) {
		const auto mb = 1024. * 1024.;
		return u"RSS %1 MB · peak %2 MB · pagefile %3 MB"_q
			.arg(data.WorkingSetSize / mb, 0, 'f', 1)
			.arg(data.PeakWorkingSetSize / mb, 0, 'f', 1)
			.arg(data.PagefileUsage / mb, 0, 'f', 1);
	}
#endif // Q_OS_WIN
	return u"n/a"_q;
}

[[nodiscard]] QString FormatUptime() {
	if (!StartedAt) {
		return u"0s"_q;
	}
	const auto sec = (crl::now() - StartedAt) / 1000;
	const auto h = sec / 3600;
	const auto m = (sec % 3600) / 60;
	const auto s = sec % 60;
	if (h > 0) {
		return u"%1h %2m %3s"_q.arg(h).arg(m).arg(s);
	}
	if (m > 0) {
		return u"%1m %2s"_q.arg(m).arg(s);
	}
	return u"%1s"_q.arg(s);
}

[[nodiscard]] QString SessionText() {
	if (!IsAppLaunched() || !Core::App().domain().started()) {
		return u"(no domain)"_q;
	}
	auto &domain = Core::App().domain();
	auto &account = domain.active();
	if (!account.sessionExists()) {
		return u"accounts=%1 · active not logged in"_q.arg(domain.accounts().size());
	}
	const auto &user = account.session().user();
	return u"accounts=%1 · @%2 · id=%3"_q
		.arg(domain.accountsAuthedCount())
		.arg(user->username().isEmpty() ? user->name() : user->username())
		.arg(user->id.value);
}

[[nodiscard]] QString ConnectionText() {
	if (!IsAppLaunched() || !Core::App().domain().started()) {
		return u"(offline / starting)"_q;
	}
	auto &account = Core::App().domain().active();
	if (!account.sessionExists()) {
		return u"no session"_q;
	}
	const auto state = account.mtp().dcstate();
	const auto label = (state == MTP::ConnectedState)
		? u"Connected"_q
		: (state == MTP::ConnectingState)
		? u"Connecting"_q
		: (state == MTP::DisconnectedState)
		? u"Disconnected"_q
		: u"state=%1"_q.arg(state);
	return u"MTP %1"_q.arg(label);
}

[[nodiscard]] QString PluginsText() {
	if (!IsAppLaunched()) {
		return u"(app not ready)"_q;
	}
	const auto &list = Core::App().plugins().plugins();
	auto enabled = 0;
	auto errors = 0;
	QStringList lines;
	for (const auto &plugin : list) {
		const auto state = (plugin.state == PluginSystem::PluginState::Enabled)
			? u"ON"_q
			: (plugin.state == PluginSystem::PluginState::Error)
			? u"ERR"_q
			: u"off"_q;
		if (plugin.state == PluginSystem::PluginState::Enabled) {
			++enabled;
		}
		if (plugin.state == PluginSystem::PluginState::Error) {
			++errors;
		}
		auto line = u"[%1] %2 v%3"_q
			.arg(state, plugin.manifest.name, plugin.manifest.version);
		if (!plugin.error.isEmpty()) {
			line += u" — %1"_q.arg(plugin.error);
		}
		lines.push_back(line);
	}
	if (lines.isEmpty()) {
		return u"plugins: 0"_q;
	}
	return u"plugins: %1 total · %2 enabled · %3 errors\n%4"_q
		.arg(list.size())
		.arg(enabled)
		.arg(errors)
		.arg(lines.join('\n'));
}

qint64 FileSize(const QString &path) {
	const auto info = QFileInfo(path);
	return info.exists() ? info.size() : 0;
}

} // namespace

QString LogsDirectory() {
	return cWorkingDir() + u"logs"_q;
}

QString ErrorsLogPath() {
	return LogsDirectory() + u"/errors.log"_q;
}

QString ActionsLogPath() {
	return LogsDirectory() + u"/last_actions.log"_q;
}

void Start() {
	if (Started.exchange(true)) {
		return;
	}
	StartedAt = crl::now();
	EnsureLogsDir();
	Note(u"boot"_q, u"DeveloperDiagnostics started"_q);

	PreviousTerminate = std::set_terminate(OnTerminate);
	std::atexit([] {
		if (Started.load()) {
			FlushCrash("atexit");
		}
	});
	std::signal(SIGSEGV, OnSignal);
	std::signal(SIGABRT, OnSignal);
#ifdef Q_OS_WIN
	SetUnhandledExceptionFilter(UnhandledFilter);
#endif // Q_OS_WIN
}

void Finish() {
	if (!Started.exchange(false)) {
		return;
	}
	Note(u"boot"_q, u"DeveloperDiagnostics finished"_q);
}

void Note(const QString &tag, const QString &text) {
	const auto line = SanitizeLine(
		u"[%1] [%2] %3"_q.arg(Stamp(), tag, text));
	auto snapshot = QStringList();
	{
		QMutexLocker lock(&RingMutex);
		PushRing(Actions, kMaxActions, line);
		snapshot = CopyRing(Actions, kMaxActions);
	}
	AppendFile(
		LogsDirectory()
			+ u"/session_"_q
			+ QDateTime::currentDateTime().toString(u"yyyyMMdd"_q)
			+ u".log"_q,
		line);
	// Keep a rolling last_actions mirror for crash inspection.
	EnsureLogsDir();
	QFile file(ActionsLogPath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		for (const auto &item : snapshot) {
			file.write(item.toUtf8());
			file.write("\n", 1);
		}
	}
}

void Error(const QString &tag, const QString &text) {
	const auto line = SanitizeLine(
		u"[%1] [%2] %3"_q.arg(Stamp(), tag, text));
	{
		QMutexLocker lock(&RingMutex);
		PushRing(Errors, kMaxErrors, line);
		PushRing(Actions, kMaxActions, u"ERROR "_q + line);
	}
	AppendFile(ErrorsLogPath(), line);
	AppendFile(
		LogsDirectory()
			+ u"/session_"_q
			+ QDateTime::currentDateTime().toString(u"yyyyMMdd"_q)
			+ u".log"_q,
		u"ERROR "_q + line);
}

QStringList RecentActions(int limit) {
	QMutexLocker lock(&RingMutex);
	return CopyRing(Actions, limit);
}

QStringList RecentErrors(int limit) {
	QMutexLocker lock(&RingMutex);
	return CopyRing(Errors, limit);
}

Snapshot CollectSnapshot() {
	Snapshot s;
	s.version = QString::fromUtf8(AppVersionStr)
		+ u" ("_q
		+ QString::number(AppVersion)
		+ u")"_q;
	s.workingDir = cWorkingDir();
	s.exeDir = cExeDir();
	s.logsDir = LogsDirectory();
	s.memoryText = MemoryText();
	s.uptimeText = FormatUptime();
	s.sessionText = SessionText();
	s.connectionText = ConnectionText();
	s.pluginsText = PluginsText();
	s.actions = RecentActions(80);
	s.errors = RecentErrors(40);
	s.logBytes = FileSize(
		LogsDirectory()
			+ u"/session_"_q
			+ QDateTime::currentDateTime().toString(u"yyyyMMdd"_q)
			+ u".log"_q);
	s.errorLogBytes = FileSize(ErrorsLogPath());
	if (IsAppLaunched()) {
		s.windowCount = Core::App().activeWindow() ? 1 : 0;
	}
	return s;
}

void OpenLogsFolder() {
	EnsureLogsDir();
	QDesktopServices::openUrl(QUrl::fromLocalFile(LogsDirectory()));
}

} // namespace Core::DeveloperDiagnostics
