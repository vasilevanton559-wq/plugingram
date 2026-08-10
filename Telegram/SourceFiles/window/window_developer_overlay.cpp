#include "window/window_developer_overlay.h"

#include "core/developer_diagnostics.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "window/window_controller.h"

#include <QtGui/QKeyEvent>

namespace Window {
namespace {

constexpr auto kRefreshMs = 500;
constexpr auto kAnimMs = 220;

[[nodiscard]] QString BuildBodyText(
		const Core::DeveloperDiagnostics::Snapshot &s) {
	auto text = QString();
	auto section = [&](const QString &title, const QString &body) {
		text += u"— %1\n%2\n\n"_q.arg(title, body.trimmed());
	};
	section(u"Client"_q,
		u"Plugingram %1\nUptime %2\nWindows %3"_q
			.arg(s.version, s.uptimeText)
			.arg(s.windowCount));
	section(u"Resources"_q, s.memoryText);
	section(u"Session"_q, s.sessionText);
	section(u"Network"_q, s.connectionText);
	section(u"Plugins"_q, s.pluginsText);
	section(u"Paths"_q,
		u"workingDir  %1\nexeDir      %2\nlogsDir     %3\nsessionLog  %4 bytes\nerrorsLog   %5 bytes"_q
			.arg(s.workingDir, s.exeDir, s.logsDir)
			.arg(s.logBytes)
			.arg(s.errorLogBytes));
	section(u"Recent errors"_q,
		s.errors.isEmpty() ? u"(none)"_q : s.errors.join('\n'));
	section(u"Recent actions"_q,
		s.actions.isEmpty() ? u"(none)"_q : s.actions.join('\n'));
	text += u"Esc or Ctrl+Shift+I — close"_q;
	return text;
}

} // namespace

DeveloperOverlay::DeveloperOverlay(
	not_null<Ui::RpWidget*> parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _timer([=] { refresh(); }) {
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	hide();
	setupUi();
	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
		updateLayout();
	}, lifetime());
}

DeveloperOverlay::~DeveloperOverlay() = default;

void DeveloperOverlay::setupUi() {
	_panel = base::make_unique_q<Ui::RpWidget>(this);
	_panel->setAttribute(Qt::WA_OpaquePaintEvent, false);
	_panel->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_panel);
		p.setRenderHint(QPainter::Antialiasing);
		auto bg = st::boxBg->c;
		bg.setAlpha(235);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(_panel->rect().adjusted(0, 0, -1, -1), 14, 14);
		auto border = st::boxTextFg->c;
		border.setAlpha(40);
		p.setPen(border);
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(_panel->rect().adjusted(0, 0, -1, -1), 14, 14);
	}, _panel->lifetime());

	_title = base::make_unique_q<Ui::FlatLabel>(
		_panel.get(),
		u"Diagnostics"_q,
		st::boxTitle);
	_scroll = base::make_unique_q<Ui::ScrollArea>(
		_panel.get(),
		st::defaultScrollArea);
	_body = _scroll->setOwnedWidget(object_ptr<Ui::FlatLabel>(
		_scroll.get(),
		QString(),
		st::defaultFlatLabel));
	_body->setSelectable(true);

	_openLogs = base::make_unique_q<Ui::RoundButton>(
		_panel.get(),
		rpl::single(u"Open Logs"_q),
		st::defaultBoxButton);
	_openLogs->setClickedCallback([=] {
		Core::DeveloperDiagnostics::OpenLogsFolder();
		Core::DeveloperDiagnostics::Note(
			u"devtools"_q,
			u"Open logs folder"_q);
	});
	_refreshBtn = base::make_unique_q<Ui::RoundButton>(
		_panel.get(),
		rpl::single(u"Refresh"_q),
		st::defaultBoxButton);
	_refreshBtn->setClickedCallback([=] { refresh(); });

	_close = base::make_unique_q<Ui::IconButton>(
		_panel.get(),
		st::boxTitleClose);
	_close->setClickedCallback([=] { hideAnimated(); });
}

void DeveloperOverlay::updateLayout() {
	if (!_panel || !_openLogs || !_title || !_scroll || !_close) {
		return;
	}
	const auto margin = std::max(24, std::min(width(), height()) / 18);
	const auto panelW = std::max(480, width() - margin * 2);
	const auto panelH = std::max(320, height() - margin * 2);
	_panel->setGeometry(
		(width() - panelW) / 2,
		(height() - panelH) / 2,
		panelW,
		panelH);

	const auto pad = 18;
	_close->moveToRight(pad, pad);
	_title->moveToLeft(pad, pad + 4);
	_title->resizeToWidth(panelW - pad * 2 - _close->width());

	const auto buttonsY = panelH - pad - _openLogs->height();
	_openLogs->moveToLeft(pad, buttonsY);
	_refreshBtn->moveToLeft(pad + _openLogs->width() + 10, buttonsY);

	const auto scrollTop = pad + _title->height() + 14;
	const auto scrollBottom = buttonsY - 12;
	_scroll->setGeometry(
		pad,
		scrollTop,
		panelW - pad * 2,
		std::max(80, scrollBottom - scrollTop));
	if (_body) {
		_body->resizeToWidth(_scroll->width());
	}
}

void DeveloperOverlay::refresh() {
	if (!_body || !_scroll || !_panel) {
		return;
	}
	const auto snap = Core::DeveloperDiagnostics::CollectSnapshot();
	_body->setText(BuildBodyText(snap));
	_body->resizeToWidth(_scroll->width());
	updateLayout();
	_panel->update();
}

void DeveloperOverlay::showAnimated() {
	_hiding = false;
	_shown = true;
	show();
	raise();
	setFocus();
	refresh();
	_timer.callEach(kRefreshMs);
	_opacity.start(
		[=] { animationCallback(); },
		_opacity.value(0.),
		1.,
		kAnimMs);
	Core::DeveloperDiagnostics::Note(
		u"devtools"_q,
		u"Overlay opened"_q);
}

void DeveloperOverlay::hideAnimated() {
	if (_hiding || !_shown) {
		return;
	}
	_hiding = true;
	_timer.cancel();
	_opacity.start(
		[=] { animationCallback(); },
		_opacity.value(1.),
		0.,
		kAnimMs);
	Core::DeveloperDiagnostics::Note(
		u"devtools"_q,
		u"Overlay closed"_q);
}

bool DeveloperOverlay::isShown() const {
	return _shown && !_hiding;
}

void DeveloperOverlay::animationCallback() {
	update();
	if (_hiding && !_opacity.animating() && _opacity.value(0.) < 0.01) {
		_shown = false;
		_hiding = false;
		hide();
	}
}

void DeveloperOverlay::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto opacity = _opacity.value(_hiding ? 0. : 1.);
	if (opacity <= 0.) {
		return;
	}
	p.setOpacity(opacity);
	p.fillRect(rect(), QColor(8, 10, 14, 170));
}

void DeveloperOverlay::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideAnimated();
		return;
	}
	RpWidget::keyPressEvent(e);
}

void DeveloperOverlay::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateLayout();
}

} // namespace Window
