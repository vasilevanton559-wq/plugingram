// Plugingram developer diagnostics: breadcrumbs, error logs, crash flush.
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace Core::DeveloperDiagnostics {

void Start();
void Finish();

[[nodiscard]] QString LogsDirectory();
[[nodiscard]] QString ErrorsLogPath();
[[nodiscard]] QString ActionsLogPath();

void Note(const QString &tag, const QString &text);
void Error(const QString &tag, const QString &text);

[[nodiscard]] QStringList RecentActions(int limit = 64);
[[nodiscard]] QStringList RecentErrors(int limit = 48);

// Snapshot for overlay panels.
struct Snapshot {
	QString version;
	QString workingDir;
	QString exeDir;
	QString logsDir;
	QString memoryText;
	QString uptimeText;
	QString sessionText;
	QString connectionText;
	QString pluginsText;
	QStringList actions;
	QStringList errors;
	int windowCount = 0;
	qint64 logBytes = 0;
	qint64 errorLogBytes = 0;
};
[[nodiscard]] Snapshot CollectSnapshot();

void OpenLogsFolder();

} // namespace Core::DeveloperDiagnostics
