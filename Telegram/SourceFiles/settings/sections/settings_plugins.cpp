/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_plugins.h"

#include "base/flat_set.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "plugin_system/plugins_manager.h"
#include "plugin_system/plugins_ui_extension.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtGui/QFontMetrics>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>
#include <QtGui/QTextOption>
#include <QtGui/QtEvents>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kRainbowPeriodMs = crl::time(4500);
constexpr auto kPagePad = 12;
constexpr auto kCardH = 72;
constexpr auto kCardRadius = 10;
constexpr auto kCardGap = 8;
constexpr auto kTypeIcon = 40;
constexpr auto kCtrl = 36;
constexpr auto kGoldStar = QColor(242, 189, 55);
constexpr auto kDescCollapsedChars = 96;
constexpr auto kToolbarH = 40;
constexpr auto kExpandMinDuration = crl::time(220);
constexpr auto kExpandMaxDuration = crl::time(480);

[[nodiscard]] QString CollapsedDescription(const QString &full) {
	if (full.size() <= kDescCollapsedChars) {
		return full;
	}
	return full.left(kDescCollapsedChars).trimmed() + QStringLiteral("…");
}

[[nodiscard]] float64 RainbowPhase() {
	return (crl::now() % kRainbowPeriodMs) / float64(kRainbowPeriodMs);
}

[[nodiscard]] QLinearGradient MakeRainbowGradient(
		float64 widthHint,
		float64 phase) {
	const auto span = std::max(widthHint * 2.2, 48.);
	const auto shift = -span + (phase * span * 2.);
	auto gradient = QLinearGradient(shift, 0., shift + span, 0.);
	constexpr auto kStops = 10;
	for (auto i = 0; i <= kStops; ++i) {
		const auto at = i / float64(kStops);
		const auto hue = std::fmod(at + phase, 1.);
		gradient.setColorAt(at, QColor::fromHsvF(hue, 0.92, 1.0));
	}
	return gradient;
}

void PaintRainbowIcon(
		QPainter &p,
		not_null<const style::icon*> icon,
		QPoint pos,
		float64 phase) {
	const auto ratio = style::DevicePixelRatio();
	auto mask = QImage(
		icon->size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	mask.setDevicePixelRatio(ratio);
	mask.fill(Qt::transparent);
	{
		auto ip = QPainter(&mask);
		icon->paint(ip, 0, 0, icon->width());
	}
	auto colored = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	colored.setDevicePixelRatio(ratio);
	colored.fill(Qt::transparent);
	{
		auto cp = QPainter(&colored);
		cp.fillRect(
			QRect(QPoint(), icon->size()),
			MakeRainbowGradient(icon->width(), phase));
		cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		cp.drawImage(0, 0, mask);
	}
	p.drawImage(pos, colored);
}

void PaintRainbowText(
		QPainter &p,
		const QString &text,
		const style::font &font,
		QPointF baseline,
		float64 widthHint,
		float64 phase) {
	auto path = QPainterPath();
	path.addText(baseline, font->f, text);
	p.fillPath(path, MakeRainbowGradient(widthHint, phase));
}

[[nodiscard]] const style::icon *IconForType(PluginSystem::PluginType type) {
	switch (type) {
	case PluginSystem::PluginType::Theme:
		return &st::menuIconPalette;
	case PluginSystem::PluginType::UiExtension:
		return &st::menuIconCustomize;
	case PluginSystem::PluginType::Utility:
		return &st::menuIconBotCommands;
	case PluginSystem::PluginType::Unknown:
		break;
	}
	return &st::menuIconSettings;
}

[[nodiscard]] QString TypeLabel(PluginSystem::PluginType type) {
	switch (type) {
	case PluginSystem::PluginType::Theme:
		return tr::lng_settings_plugins_type_theme(tr::now);
	case PluginSystem::PluginType::UiExtension:
		return tr::lng_settings_plugins_type_ui_extension(tr::now);
	case PluginSystem::PluginType::Utility:
		return tr::lng_settings_plugins_type_utility(tr::now);
	case PluginSystem::PluginType::Unknown:
		break;
	}
	return tr::lng_settings_plugins_type_unknown(tr::now);
}

class PluginCard final : public Ui::RpWidget {
public:
	PluginCard(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		PluginSystem::PluginDescriptor plugin,
		Fn<void()> requestReload,
		Fn<void()> onToggled);

	[[nodiscard]] const QString &pluginId() const {
		return _plugin.manifest.id;
	}
	void setEnabledVisual(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void setup();
	void updateLayout();
	void applyFavoriteVisual();
	void setExpanded(bool expanded);
	[[nodiscard]] QString visibleDescription() const;
	[[nodiscard]] int textWidthFor(int w) const;
	[[nodiscard]] int measureDescriptionHeight(int textW) const;
	[[nodiscard]] int contentHeightForWidth(int w) const;
	[[nodiscard]] bool canExpand() const;

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _requestReload;
	const Fn<void()> _onToggled;
	PluginSystem::PluginDescriptor _plugin;
	QPixmap _iconPix;
	QString _fullDescription;
	bool _expanded = false;
	bool _favorite = false;
	int _height = kCardH;
	QRect _descRect;

	Ui::FlatLabel *_title = nullptr;
	Ui::FlatLabel *_meta = nullptr;
	Ui::LinkButton *_moreButton = nullptr;
	Ui::IconButton *_favoriteBtn = nullptr;
	Ui::IconButton *_remove = nullptr;
	Ui::RippleButton *_toggle = nullptr;
	std::unique_ptr<Ui::ToggleView> _toggleView;
	Ui::Animations::Simple _expandAnimation;
};

PluginCard::PluginCard(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	PluginSystem::PluginDescriptor plugin,
	Fn<void()> requestReload,
	Fn<void()> onToggled)
: RpWidget(parent)
, _controller(controller)
, _requestReload(std::move(requestReload))
, _onToggled(std::move(onToggled))
, _plugin(std::move(plugin))
, _favorite(_plugin.favorite) {
	setup();
}

void PluginCard::setEnabledVisual(bool enabled) {
	if (_toggleView && _toggleView->checked() != enabled) {
		_toggleView->setChecked(enabled, anim::type::normal);
	}
}

void PluginCard::setup() {
	if (!_plugin.iconPath.isEmpty()) {
		auto image = QImage(_plugin.iconPath);
		if (!image.isNull()) {
			_iconPix = QPixmap::fromImage(
				image.scaled(
					QSize(kTypeIcon, kTypeIcon) * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation));
			_iconPix.setDevicePixelRatio(style::DevicePixelRatio());
		}
	}

	_title = Ui::CreateChild<Ui::FlatLabel>(
		this,
		_plugin.manifest.name,
		st::defaultFlatLabel);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto version = _plugin.manifest.version.isEmpty()
		? QStringLiteral("—")
		: _plugin.manifest.version;
	auto metaText = TypeLabel(_plugin.manifest.type)
		+ QStringLiteral(" · v")
		+ version;
	if (!_plugin.manifest.author.isEmpty()) {
		metaText += QStringLiteral(" · ") + _plugin.manifest.author;
	}
	_meta = Ui::CreateChild<Ui::FlatLabel>(
		this,
		metaText,
		st::defaultFlatLabel);
	_meta->setTextColorOverride(st::windowSubTextFg->c);
	_meta->setAttribute(Qt::WA_TransparentForMouseEvents);

	_fullDescription = _plugin.manifest.description;
	if (!_plugin.error.isEmpty()) {
		_fullDescription = _fullDescription.isEmpty()
			? _plugin.error
			: (_fullDescription + QStringLiteral("\n\n") + _plugin.error);
	}

	_moreButton = Ui::CreateChild<Ui::LinkButton>(
		this,
		QStringLiteral("Показать ещё"),
		st::defaultLinkButton);
	_moreButton->setClickedCallback([=] {
		setExpanded(!_expanded);
	});
	if (canExpand()) {
		_moreButton->show();
	} else {
		_moreButton->hide();
	}

	const auto id = _plugin.manifest.id;

	_favoriteBtn = Ui::CreateChild<Ui::IconButton>(this, st::infoTopBarDelete);
	_favoriteBtn->resize(kCtrl, kCtrl);
	_favoriteBtn->setIconOverride(&st::menuIconStar, &st::menuIconStar);
	applyFavoriteVisual();
	_favoriteBtn->setClickedCallback([=] {
		_favorite = !_favorite;
		Core::App().plugins().setPluginFavorite(id, _favorite);
		applyFavoriteVisual();
	});

	_remove = Ui::CreateChild<Ui::IconButton>(this, st::infoTopBarDelete);
	_remove->resize(kCtrl, kCtrl);
	_remove->setIconOverride(
		&st::menuIconDeleteAttention,
		&st::menuIconDeleteAttention);
	if (_plugin.bundled) {
		_remove->hide();
	} else {
		_remove->setClickedCallback([=] {
			const auto controller = _controller;
			const auto reload = _requestReload;
			const auto name = _plugin.manifest.name;
			controller->show(Ui::MakeConfirmBox({
				.text = QStringLiteral("Delete \"%1\"?").arg(name),
				// Fn<void(Fn<void()>)> form closes the box via close().
				.confirmed = [=](Fn<void()> close) {
					// Close first so the confirm layer cannot be clicked again.
					close();
					crl::on_main([=] {
						Core::App().plugins().deletePlugin(id);
						controller->showToast(QStringLiteral("Plugin deleted"));
						if (reload) {
							reload();
						}
					});
				},
				.confirmText = tr::lng_box_delete(),
			}));
		});
	}

	const auto enabled = (_plugin.state == PluginSystem::PluginState::Enabled);
	const auto hasError = (_plugin.state == PluginSystem::PluginState::Error);
	// Same Toggle style/animation as SettingsButton in Telegram.
	_toggleView = std::make_unique<Ui::ToggleView>(
		st::settingsButton.toggle,
		enabled,
		[=] { if (_toggle) _toggle->update(); });
	_toggleView->setLocked(hasError);
	const auto toggleSize = _toggleView->getSize();

	_toggle = Ui::CreateChild<Ui::RippleButton>(
		this,
		st::defaultRippleAnimation);
	_toggle->setCursor(style::cur_pointer);
	_toggle->resize(
		toggleSize.width() + st::settingsButton.toggle.rippleAreaPadding,
		kCtrl);
	_toggle->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_toggle);
		p.setRenderHint(QPainter::Antialiasing);
		const auto size = _toggleView->getSize();
		_toggleView->paint(
			p,
			(_toggle->width() - size.width()) / 2,
			(_toggle->height() - size.height()) / 2,
			_toggle->width());
	}, _toggle->lifetime());
	if (!hasError) {
		_toggle->setClickedCallback([=] {
			if (!_toggleView || _toggleView->animating()) {
				return;
			}
			const auto next = !_toggleView->checked();
			// Animate immediately; apply plugin changes after the click stack
			// unwinds (theme apply / layout rebuild must not run mid-click).
			_toggleView->setChecked(next, anim::type::normal);
			Ui::PostponeCall(this, [=] {
				const auto guard = QPointer<PluginCard>(this);
				Core::App().plugins().setPluginEnabled(id, next);
				// Theme apply may rebuild chrome and destroy this card.
				if (!guard || !_toggleView) {
					return;
				}
				_plugin.state = next
					? PluginSystem::PluginState::Enabled
					: PluginSystem::PluginState::Disabled;
				_controller->showToast(next
					? tr::lng_settings_plugins_enabled(tr::now)
					: tr::lng_settings_plugins_disabled(tr::now));
				if (_onToggled) {
					_onToggled();
				}
			});
		});
	}

	widthValue(
	) | rpl::on_next([=](int) {
		updateLayout();
	}, lifetime());
	updateLayout();
}

void PluginCard::applyFavoriteVisual() {
	_favoriteBtn->setIconOverride(&st::menuIconStar, &st::menuIconStar);
	_favoriteBtn->setIconColorOverride(
		_favorite ? std::make_optional(kGoldStar) : std::nullopt);
	_favoriteBtn->update();
}

bool PluginCard::canExpand() const {
	return _fullDescription.size() > kDescCollapsedChars;
}

QString PluginCard::visibleDescription() const {
	if (_fullDescription.isEmpty()) {
		return {};
	}
	return (_expanded || !canExpand())
		? _fullDescription
		: CollapsedDescription(_fullDescription);
}

int PluginCard::textWidthFor(int w) const {
	if (w <= 0 || !_toggle) {
		return 40;
	}
	const auto pad = 10;
	const auto removeW = (_plugin.bundled
		|| !_remove
		|| _remove->isHidden()) ? 0 : kCtrl;
	const auto controlsW = kCtrl + removeW + _toggle->width() + 4;
	const auto textLeft = pad + kTypeIcon + 10;
	return std::max(w - textLeft - controlsW - pad, 40);
}

int PluginCard::measureDescriptionHeight(int textW) const {
	const auto text = visibleDescription();
	if (text.isEmpty() || textW <= 0) {
		return 0;
	}
	const auto &font = st::defaultFlatLabel.style.font;
	const auto bounds = QFontMetrics(font->f).boundingRect(
		QRect(0, 0, textW, 100000),
		Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
		text);
	return std::max(bounds.height(), font->height);
}

int PluginCard::contentHeightForWidth(int w) const {
	if (w <= 0 || !_title || !_meta || !_toggle) {
		return kCardH;
	}
	const auto textW = textWidthFor(w);
	const auto descH = measureDescriptionHeight(textW);
	const auto moreH = (canExpand() && _moreButton && !_moreButton->isHidden())
		? (_moreButton->height() + 4)
		: 0;
	const auto textBlock = _title->height()
		+ 2
		+ _meta->height()
		+ (descH ? (4 + descH) : 0)
		+ moreH;
	return std::max(std::max(textBlock, kTypeIcon) + 20, kCardH);
}

void PluginCard::setExpanded(bool expanded) {
	if (!canExpand() || _expanded == expanded) {
		return;
	}
	const auto fromH = std::max(height(), _height);
	_expanded = expanded;
	_moreButton->setText(_expanded
		? QStringLiteral("Скрыть")
		: QStringLiteral("Показать ещё"));

	const auto w = std::max(width(), 1);
	const auto textW = textWidthFor(w);
	_title->resizeToWidth(textW);
	_meta->resizeToWidth(textW);
	_moreButton->resizeToNaturalWidth(textW);
	const auto toH = contentHeightForWidth(w);

	const auto duration = std::clamp(
		crl::time(std::abs(toH - fromH) * 0.55),
		kExpandMinDuration,
		kExpandMaxDuration);

	_expandAnimation.stop();
	_expandAnimation.start([=] {
		_height = anim::interpolate(fromH, toH, _expandAnimation.value(1.));
		resize(width(), _height);
		updateLayout();
		update();
	}, 0., 1., duration, anim::sineInOut);
}

int PluginCard::resizeGetHeight(int newWidth) {
	if (newWidth > 0 && !_expandAnimation.animating()) {
		const auto textW = textWidthFor(newWidth);
		_title->resizeToWidth(textW);
		_meta->resizeToWidth(textW);
		if (_moreButton && canExpand()) {
			_moreButton->show();
			_moreButton->resizeToNaturalWidth(textW);
		}
		_height = contentHeightForWidth(newWidth);
	}
	return _height;
}

void PluginCard::updateLayout() {
	const auto w = width();
	if (w <= 0) {
		return;
	}

	const auto pad = 10;
	const auto textW = textWidthFor(w);
	const auto textLeft = pad + kTypeIcon + 10;
	const auto removeW = (_plugin.bundled
		|| !_remove
		|| _remove->isHidden()) ? 0 : kCtrl;
	const auto controlsLeft = w
		- pad
		- (kCtrl + removeW + _toggle->width() + 4);

	_title->resizeToWidth(textW);
	_meta->resizeToWidth(textW);
	if (canExpand()) {
		_moreButton->show();
		_moreButton->setText(_expanded
			? QStringLiteral("Скрыть")
			: QStringLiteral("Показать ещё"));
		_moreButton->resizeToNaturalWidth(textW);
	} else {
		_moreButton->hide();
	}

	if (!_expandAnimation.animating()) {
		_height = contentHeightForWidth(w);
	}

	const auto textTop = 10;
	_title->moveToLeft(textLeft, textTop);
	_meta->moveToLeft(textLeft, textTop + _title->height() + 2);

	auto y = textTop + _title->height() + 2 + _meta->height();
	const auto descH = measureDescriptionHeight(textW);
	if (descH > 0) {
		y += 4;
		_descRect = QRect(textLeft, y, textW, descH);
		y += descH;
		if (canExpand() && !_moreButton->isHidden()) {
			_moreButton->moveToLeft(textLeft, y + 4);
			y += _moreButton->height() + 4;
		}
	} else {
		_descRect = {};
		_moreButton->hide();
	}

	// Keep controls top-aligned so they never cover the description.
	const auto controlsTop = textTop;
	_favoriteBtn->moveToLeft(controlsLeft, controlsTop);
	auto nextLeft = controlsLeft + kCtrl;
	if (!_plugin.bundled && _remove && !_remove->isHidden()) {
		_remove->moveToLeft(nextLeft, controlsTop);
		nextLeft += kCtrl;
		_remove->raise();
	}
	_toggle->moveToLeft(
		nextLeft + 4,
		controlsTop + (kCtrl - _toggle->height()) / 2);
	_favoriteBtn->raise();
	_toggle->raise();
	if (!_moreButton->isHidden()) {
		_moreButton->raise();
	}

	if (!_expandAnimation.animating()) {
		resize(w, _height);
	}
}

void PluginCard::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setClipRect(e->rect());

	auto fill = st::windowBg->c;
	const auto over = st::windowBgOver->c;
	fill = QColor(
		(fill.red() * 3 + over.red()) / 4,
		(fill.green() * 3 + over.green()) / 4,
		(fill.blue() * 3 + over.blue()) / 4);
	p.setPen(QPen(over, 1.));
	p.setBrush(fill);
	p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kCardRadius, kCardRadius);

	const auto iconRect = QRect(10, 10, kTypeIcon, kTypeIcon);
	{
		auto iconBg = st::windowBgActive->c;
		iconBg.setAlpha(40);
		p.setPen(Qt::NoPen);
		p.setBrush(iconBg);
		p.drawRoundedRect(iconRect, 10, 10);
	}
	if (!_iconPix.isNull()) {
		const auto pw = _iconPix.width() / style::DevicePixelRatio();
		const auto ph = _iconPix.height() / style::DevicePixelRatio();
		p.drawPixmap(
			iconRect.x() + (iconRect.width() - pw) / 2,
			iconRect.y() + (iconRect.height() - ph) / 2,
			_iconPix);
	} else if (const auto *icon = IconForType(_plugin.manifest.type)) {
		icon->paintInCenter(p, iconRect);
	}

	const auto text = visibleDescription();
	if (!text.isEmpty() && !_descRect.isEmpty()) {
		p.setPen(st::windowSubTextFg->c);
		p.setFont(st::defaultFlatLabel.style.font);
		auto option = QTextOption(Qt::AlignLeft | Qt::AlignTop);
		option.setWrapMode(QTextOption::WordWrap);
		p.drawText(_descRect, text, option);
	}
}

constexpr auto kStoreActionW = 96;
constexpr auto kStoreActionH = 30;
constexpr auto kStoreFadeMs = crl::time(220);

class StoreInstallButton final : public Ui::RippleButton {
public:
	enum class Mode {
		Install,
		Update,
		Installed,
	};

	StoreInstallButton(QWidget *parent, Mode mode)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _mode(mode)
	, _labelOpacity(1.)
	, _progressOpacity(0.)
	, _label(LabelForMode(mode)) {
		resize(kStoreActionW, kStoreActionH);
		const auto locked = (mode == Mode::Installed);
		setCursor(locked ? style::cur_default : style::cur_pointer);
		setAttribute(Qt::WA_TransparentForMouseEvents, locked);
	}

	void setProgress(float64 progress) {
		progress = std::clamp(progress, 0., 1.);
		if (std::abs(progress - _targetProgress) < 0.001) {
			return;
		}
		const auto from = _displayProgress;
		_targetProgress = progress;
		const auto delta = std::abs(_targetProgress - from);
		// Adaptive duration: tiny network ticks are short, big jumps are longer.
		const auto duration = crl::time(std::clamp(
			320. + delta * 1100.,
			320.,
			1600.));
		_progressAnim.stop();
		_progressAnim.start([=] {
			_displayProgress = anim::interpolateF(
				from,
				_targetProgress,
				_progressAnim.value(1.));
			_percent = int(std::lround(_displayProgress * 100.));
			update();
		}, 0., 1., duration, anim::easeOutCubic);
	}

	void beginDownload() {
		if (_busy) {
			return;
		}
		_busy = true;
		_percent = 0;
		_displayProgress = 0.;
		_targetProgress = 0.;
		_progressAnim.stop();
		setCursor(style::cur_default);
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		animateToProgressMode(true);
		update();
	}

	void finishSuccess() {
		_busy = false;
		_mode = Mode::Installed;
		_targetProgress = 1.;
		_displayProgress = 1.;
		_percent = 100;
		_progressAnim.stop();
		setCursor(style::cur_default);
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		_label = LabelForMode(_mode);
		animateToProgressMode(false);
		update();
	}

	void finishFailed() {
		_busy = false;
		_displayProgress = 0.;
		_targetProgress = 0.;
		_percent = 0;
		_progressAnim.stop();
		_label = LabelForMode(_mode == Mode::Installed ? Mode::Install : _mode);
		if (_mode == Mode::Installed) {
			_mode = Mode::Install;
		}
		setCursor(style::cur_pointer);
		setAttribute(Qt::WA_TransparentForMouseEvents, false);
		animateToProgressMode(false);
		update();
	}

	[[nodiscard]] bool busy() const {
		return _busy;
	}
	[[nodiscard]] bool installed() const {
		return _mode == Mode::Installed && !_busy;
	}
	[[nodiscard]] bool canStart() const {
		return !_busy && _mode != Mode::Installed;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setClipRect(e->rect());

		const auto radius = height() / 2.;
		auto fill = st::windowBgActive->c;
		if (_mode != Mode::Installed && !_busy) {
			fill.setAlpha(230);
		} else if (_busy) {
			fill.setAlpha(210);
		}

		// Clip everything to the pill so the fill bar never escapes.
		auto clip = QPainterPath();
		clip.addRoundedRect(QRectF(rect()), radius, radius);
		p.setClipPath(clip);

		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawRect(rect());

		if (_busy && _displayProgress > 0.) {
			auto bar = fill.lighter(125);
			bar.setAlpha(255);
			const auto barW = std::max(
				1,
				int(std::lround(width() * _displayProgress)));
			p.setBrush(bar);
			p.drawRect(0, 0, barW, height());
		}

		p.setClipping(false);
		p.setClipRect(e->rect());
		paintRipple(p, 0, 0);

		const auto &font = st::semiboldFont;
		p.setFont(font);
		auto textColor = st::windowFgActive->c;

		if (_labelOpacity > 0.01) {
			auto c = textColor;
			c.setAlphaF(c.alphaF() * _labelOpacity);
			p.setPen(c);
			p.drawText(rect(), _label, style::al_center);
		}
		if (_progressOpacity > 0.01) {
			auto c = textColor;
			c.setAlphaF(c.alphaF() * _progressOpacity);
			p.setPen(c);
			p.drawText(
				rect(),
				QString::number(_percent) + QChar('%'),
				style::al_center);
		}
	}

private:
	[[nodiscard]] static QString LabelForMode(Mode mode) {
		switch (mode) {
		case Mode::Update: return QStringLiteral("Update");
		case Mode::Installed: return QStringLiteral("Installed");
		case Mode::Install: return QStringLiteral("Install");
		}
		return QStringLiteral("Install");
	}

	void animateToProgressMode(bool progressMode) {
		const auto fromLabel = _labelOpacity;
		const auto toLabel = progressMode ? 0. : 1.;
		const auto fromProgress = _progressOpacity;
		const auto toProgress = progressMode ? 1. : 0.;
		_labelOpacityAnimation.stop();
		_progressOpacityAnimation.stop();
		_labelOpacityAnimation.start([=] {
			_labelOpacity = anim::interpolateF(
				fromLabel,
				toLabel,
				_labelOpacityAnimation.value(1.));
			update();
		}, 0., 1., kStoreFadeMs, anim::sineInOut);
		_progressOpacityAnimation.start([=] {
			_progressOpacity = anim::interpolateF(
				fromProgress,
				toProgress,
				_progressOpacityAnimation.value(1.));
			update();
		}, 0., 1., kStoreFadeMs, anim::sineInOut);
	}

	Mode _mode = Mode::Install;
	bool _busy = false;
	float64 _displayProgress = 0.;
	float64 _targetProgress = 0.;
	int _percent = 0;
	float64 _labelOpacity = 1.;
	float64 _progressOpacity = 0.;
	QString _label = QStringLiteral("Install");
	Ui::Animations::Simple _labelOpacityAnimation;
	Ui::Animations::Simple _progressOpacityAnimation;
	Ui::Animations::Simple _progressAnim;
};

class StoreCard final : public Ui::RpWidget {
public:
	StoreCard(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		PluginSystem::RemotePluginEntry entry,
		StoreInstallButton::Mode actionMode,
		Fn<void()> onInstalled);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void setup();
	void updateLayout();
	void setExpanded(bool expanded);
	void startInstall();
	[[nodiscard]] QString visibleDescription() const;
	[[nodiscard]] int textWidthFor(int w) const;
	[[nodiscard]] int measureDescriptionHeight(int textW) const;
	[[nodiscard]] int contentHeightForWidth(int w) const;
	[[nodiscard]] bool canExpand() const;

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _onInstalled;
	PluginSystem::RemotePluginEntry _entry;
	StoreInstallButton::Mode _actionMode = StoreInstallButton::Mode::Install;
	bool _expanded = false;
	int _height = kCardH;
	QRect _descRect;

	void loadIcon();

	Ui::FlatLabel *_title = nullptr;
	Ui::FlatLabel *_meta = nullptr;
	Ui::LinkButton *_moreButton = nullptr;
	Ui::IconButton *_repoButton = nullptr;
	StoreInstallButton *_action = nullptr;
	QPixmap _iconPix;
	Ui::Animations::Simple _expandAnimation;
};

StoreCard::StoreCard(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	PluginSystem::RemotePluginEntry entry,
	StoreInstallButton::Mode actionMode,
	Fn<void()> onInstalled)
: RpWidget(parent)
, _controller(controller)
, _onInstalled(std::move(onInstalled))
, _entry(std::move(entry))
, _actionMode(actionMode) {
	setup();
}

void StoreCard::loadIcon() {
	if (const auto *installed = Core::App().plugins().findById(_entry.id)) {
		if (!installed->iconPath.isEmpty()) {
			auto image = QImage(installed->iconPath);
			if (!image.isNull()) {
				_iconPix = QPixmap::fromImage(
					image.scaled(
						QSize(kTypeIcon, kTypeIcon)
							* style::DevicePixelRatio(),
						Qt::KeepAspectRatio,
						Qt::SmoothTransformation));
				_iconPix.setDevicePixelRatio(style::DevicePixelRatio());
				update();
				return;
			}
		}
	}

	const auto url = PluginSystem::ResolveStoreIconUrl(_entry);
	if (url.isEmpty()) {
		return;
	}
	const auto weak = QPointer<StoreCard>(this);
	Core::App().plugins().store().fetchTrustedBytes(
		url,
		512 * 1024,
		[=](QByteArray bytes, QString error) {
			if (!weak || !error.isEmpty() || bytes.isEmpty()) {
				return;
			}
			auto image = QImage::fromData(bytes);
			if (image.isNull()) {
				return;
			}
			weak->_iconPix = QPixmap::fromImage(
				image.scaled(
					QSize(kTypeIcon, kTypeIcon) * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation));
			weak->_iconPix.setDevicePixelRatio(style::DevicePixelRatio());
			weak->update();
		});
}

void StoreCard::setup() {
	loadIcon();

	_title = Ui::CreateChild<Ui::FlatLabel>(
		this,
		_entry.name,
		st::defaultFlatLabel);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto version = _entry.version.isEmpty()
		? QStringLiteral("1.0.0")
		: _entry.version;
	const auto author = _entry.author.isEmpty()
		? QStringLiteral("community")
		: _entry.author;
	_meta = Ui::CreateChild<Ui::FlatLabel>(
		this,
		TypeLabel(_entry.type)
			+ QStringLiteral(" · v")
			+ version
			+ QStringLiteral(" · ")
			+ author,
		st::defaultFlatLabel);
	_meta->setTextColorOverride(st::windowSubTextFg->c);
	_meta->setAttribute(Qt::WA_TransparentForMouseEvents);

	_moreButton = Ui::CreateChild<Ui::LinkButton>(
		this,
		QStringLiteral("Показать ещё"),
		st::defaultLinkButton);
	_moreButton->setClickedCallback([=] {
		setExpanded(!_expanded);
	});
	if (canExpand()) {
		_moreButton->show();
	} else {
		_moreButton->hide();
	}

	_action = Ui::CreateChild<StoreInstallButton>(this, _actionMode);
	_action->setClickedCallback([=] {
		startInstall();
	});

	_repoButton = Ui::CreateChild<Ui::IconButton>(this, st::infoTopBarDelete);
	_repoButton->resize(kCtrl, kCtrl);
	_repoButton->setIconOverride(&st::menuIconLink, &st::menuIconLink);
	if (!PluginSystem::IsTrustedPluginRepoUrl(_entry.repoUrl)) {
		_repoButton->hide();
	} else {
		const auto repoUrl = _entry.repoUrl;
		_repoButton->setClickedCallback([=] {
			if (PluginSystem::IsTrustedPluginRepoUrl(repoUrl)) {
				File::OpenUrl(repoUrl);
			}
		});
		_repoButton->show();
	}

	widthValue(
	) | rpl::on_next([=](int) {
		updateLayout();
	}, lifetime());
	updateLayout();
}

void StoreCard::startInstall() {
	if (!_action || !_action->canStart()) {
		return;
	}
	if (Core::App().plugins().store().busy()) {
		_controller->showToast(QStringLiteral("Store is busy…"));
		return;
	}

	const auto updating = (_actionMode == StoreInstallButton::Mode::Update);
	_action->beginDownload();
	const auto entry = _entry;
	const auto weak = QPointer<StoreCard>(this);
	const auto controller = _controller;
	Core::App().plugins().installStoreEntry(
		entry,
		[=](float64 progress) {
			if (!weak || !weak->_action) {
				return;
			}
			weak->_action->setProgress(progress);
		},
		[=](bool ok, QString error) {
			if (!weak || !weak->_action) {
				return;
			}
			if (!ok) {
				weak->_action->finishFailed();
				controller->showToast(
					QStringLiteral("Install failed: %1").arg(error));
				return;
			}
			weak->_action->setProgress(1.);
			Ui::PostponeCall(weak.data(), [=] {
				if (!weak || !weak->_action) {
					return;
				}
				weak->_action->finishSuccess();
				weak->_actionMode = StoreInstallButton::Mode::Installed;
				controller->showToast(updating
					? QStringLiteral("Plugin updated from GitHub.")
					: QStringLiteral(
						"Installed. Enable it in Installed plugins."));
			});
		},
		updating);
}

bool StoreCard::canExpand() const {
	return _entry.description.size() > kDescCollapsedChars;
}

QString StoreCard::visibleDescription() const {
	if (_entry.description.isEmpty()) {
		return {};
	}
	return (_expanded || !canExpand())
		? _entry.description
		: CollapsedDescription(_entry.description);
}

int StoreCard::textWidthFor(int w) const {
	if (w <= 0 || !_action) {
		return 40;
	}
	const auto pad = 10;
	const auto textLeft = pad + kTypeIcon + 10;
	const auto repoW = (_repoButton && !_repoButton->isHidden())
		? (kCtrl + 4)
		: 0;
	return std::max(w - textLeft - repoW - kStoreActionW - pad - 8, 40);
}

int StoreCard::measureDescriptionHeight(int textW) const {
	const auto text = visibleDescription();
	if (text.isEmpty() || textW <= 0) {
		return 0;
	}
	const auto &font = st::defaultFlatLabel.style.font;
	const auto bounds = QFontMetrics(font->f).boundingRect(
		QRect(0, 0, textW, 100000),
		Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
		text);
	return std::max(bounds.height(), font->height);
}

int StoreCard::contentHeightForWidth(int w) const {
	if (w <= 0 || !_title || !_meta || !_action) {
		return kCardH;
	}
	const auto textW = textWidthFor(w);
	const auto descH = measureDescriptionHeight(textW);
	const auto moreH = (canExpand() && _moreButton && !_moreButton->isHidden())
		? (_moreButton->height() + 4)
		: 0;
	const auto textBlock = _title->height()
		+ 2
		+ _meta->height()
		+ (descH ? (4 + descH) : 0)
		+ moreH;
	return std::max(
		std::max({ textBlock, kTypeIcon, kStoreActionH }) + 20,
		kCardH);
}

void StoreCard::setExpanded(bool expanded) {
	if (!canExpand() || _expanded == expanded) {
		return;
	}
	const auto fromH = std::max(height(), _height);
	_expanded = expanded;
	_moreButton->setText(_expanded
		? QStringLiteral("Скрыть")
		: QStringLiteral("Показать ещё"));

	const auto w = std::max(width(), 1);
	const auto textW = textWidthFor(w);
	_title->resizeToWidth(textW);
	_meta->resizeToWidth(textW);
	_moreButton->resizeToNaturalWidth(textW);
	const auto toH = contentHeightForWidth(w);
	const auto duration = std::clamp(
		crl::time(std::abs(toH - fromH) * 0.55),
		kExpandMinDuration,
		kExpandMaxDuration);

	_expandAnimation.stop();
	_expandAnimation.start([=] {
		_height = anim::interpolate(fromH, toH, _expandAnimation.value(1.));
		resize(width(), _height);
		updateLayout();
		update();
	}, 0., 1., duration, anim::sineInOut);
}

int StoreCard::resizeGetHeight(int newWidth) {
	if (newWidth > 0 && !_expandAnimation.animating()) {
		const auto textW = textWidthFor(newWidth);
		_title->resizeToWidth(textW);
		_meta->resizeToWidth(textW);
		if (_moreButton && canExpand()) {
			_moreButton->show();
			_moreButton->resizeToNaturalWidth(textW);
		}
		_height = contentHeightForWidth(newWidth);
	}
	return _height;
}

void StoreCard::updateLayout() {
	const auto w = width();
	if (w <= 0 || !_action) {
		return;
	}

	const auto pad = 10;
	_action->resize(kStoreActionW, kStoreActionH);
	const auto textW = textWidthFor(w);
	const auto textLeft = pad + kTypeIcon + 10;

	_title->resizeToWidth(textW);
	_meta->resizeToWidth(textW);
	if (canExpand()) {
		_moreButton->show();
		_moreButton->setText(_expanded
			? QStringLiteral("Скрыть")
			: QStringLiteral("Показать ещё"));
		_moreButton->resizeToNaturalWidth(textW);
	} else {
		_moreButton->hide();
	}

	if (!_expandAnimation.animating()) {
		_height = contentHeightForWidth(w);
	}

	const auto textTop = 10;
	_title->moveToLeft(textLeft, textTop);
	_meta->moveToLeft(textLeft, textTop + _title->height() + 2);

	auto y = textTop + _title->height() + 2 + _meta->height();
	const auto descH = measureDescriptionHeight(textW);
	if (descH > 0) {
		y += 4;
		_descRect = QRect(textLeft, y, textW, descH);
		y += descH;
		if (canExpand() && !_moreButton->isHidden()) {
			_moreButton->moveToLeft(textLeft, y + 4);
		}
	} else {
		_descRect = {};
		_moreButton->hide();
	}

	_action->moveToLeft(
		w - pad - _action->width(),
		textTop + (kCtrl - _action->height()) / 2);
	if (_repoButton && !_repoButton->isHidden()) {
		_repoButton->moveToLeft(
			_action->x() - 4 - _repoButton->width(),
			textTop + (kCtrl - _repoButton->height()) / 2);
		_repoButton->raise();
	}
	_action->raise();
	if (!_moreButton->isHidden()) {
		_moreButton->raise();
	}

	if (!_expandAnimation.animating()) {
		resize(w, _height);
	}
}

void StoreCard::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setClipRect(e->rect());

	auto fill = st::windowBg->c;
	const auto over = st::windowBgOver->c;
	fill = QColor(
		(fill.red() * 3 + over.red()) / 4,
		(fill.green() * 3 + over.green()) / 4,
		(fill.blue() * 3 + over.blue()) / 4);
	p.setPen(QPen(over, 1.));
	p.setBrush(fill);
	p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kCardRadius, kCardRadius);

	const auto iconRect = QRect(10, 10, kTypeIcon, kTypeIcon);
	{
		auto iconBg = st::windowBgActive->c;
		iconBg.setAlpha(40);
		p.setPen(Qt::NoPen);
		p.setBrush(iconBg);
		p.drawRoundedRect(iconRect, 10, 10);
	}
	if (!_iconPix.isNull()) {
		const auto pw = _iconPix.width() / style::DevicePixelRatio();
		const auto ph = _iconPix.height() / style::DevicePixelRatio();
		p.drawPixmap(
			iconRect.x() + (iconRect.width() - pw) / 2,
			iconRect.y() + (iconRect.height() - ph) / 2,
			_iconPix);
	} else if (const auto *icon = IconForType(_entry.type)) {
		icon->paintInCenter(p, iconRect);
	}

	const auto text = visibleDescription();
	if (!text.isEmpty() && !_descRect.isEmpty()) {
		p.setPen(st::windowSubTextFg->c);
		p.setFont(st::defaultFlatLabel.style.font);
		auto option = QTextOption(Qt::AlignLeft | Qt::AlignTop);
		option.setWrapMode(QTextOption::WordWrap);
		p.drawText(_descRect, text, option);
	}
}

class PluginsToolbar final : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

protected:
	int resizeGetHeight(int newWidth) override {
		Q_UNUSED(newWidth);
		return kToolbarH;
	}
};

class Plugins : public Section<Plugins> {
public:
	Plugins(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void setupToolbar();
	void rebuildList();
	void rebuildInstalledList();
	void rebuildStoreList();
	void rebuildPanels();
	void syncCardToggles();
	void requestRebuild();
	void handleCardToggled();
	void setStoreMode(bool storeMode);
	void refreshStoreCatalog(bool silent);

	Ui::VerticalLayout *_root = nullptr;
	PluginsToolbar *_toolbar = nullptr;
	Ui::FlatLabel *_toolbarHint = nullptr;
	Ui::IconButton *_storeButton = nullptr;
	Ui::IconButton *_refreshButton = nullptr;
	Ui::IconButton *_folderButton = nullptr;
	Ui::VerticalLayout *_list = nullptr;
	Ui::VerticalLayout *_panels = nullptr;
	std::vector<QPointer<PluginCard>> _cards;
	bool _rebuildQueued = false;
	bool _storeMode = false;
	bool _storeLoading = false;
};

Plugins::Plugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Plugins::title() {
	return tr::lng_settings_plugins();
}

const auto kMeta = BuildHelper({
	.id = Plugins::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_plugins,
	.icon = &st::menuIconShop,
}, [](SectionBuilder &builder) {
	builder.addButton({
		.id = QStringLiteral("plugins/refresh"),
		.title = tr::lng_settings_plugins_refresh(),
		.keywords = { QStringLiteral("plugin"), QStringLiteral("refresh") },
	});
	builder.addButton({
		.id = QStringLiteral("plugins/open-folder"),
		.title = tr::lng_settings_plugins_open_folder(),
		.keywords = { QStringLiteral("plugin"), QStringLiteral("folder") },
	});
});

void Plugins::setupToolbar() {
	_toolbar = _root->add(
		object_ptr<PluginsToolbar>(_root),
		style::margins(kPagePad, 4, kPagePad, 8));

	_toolbarHint = Ui::CreateChild<Ui::FlatLabel>(
		_toolbar,
		QStringLiteral("Installed plugins"),
		st::defaultFlatLabel);
	_toolbarHint->setTextColorOverride(st::windowSubTextFg->c);

	_folderButton = Ui::CreateChild<Ui::IconButton>(
		_toolbar,
		st::infoTopBarDelete);
	_folderButton->setIconOverride(
		&st::menuIconShowInFolder,
		&st::menuIconShowInFolder);
	_folderButton->setClickedCallback([=] {
		const auto path = Core::App().plugins().pluginsRoot();
		QDir().mkpath(path);
		File::Launch(path);
	});

	_refreshButton = Ui::CreateChild<Ui::IconButton>(
		_toolbar,
		st::infoTopBarDelete);
	_refreshButton->setIconOverride(
		&st::menuIconRestore,
		&st::menuIconRestore);
	_refreshButton->setClickedCallback([=] {
		if (_storeMode) {
			refreshStoreCatalog(false);
		} else {
			requestRebuild();
			controller()->showToast(
				tr::lng_settings_plugins_reloaded(tr::now));
		}
	});

	_storeButton = Ui::CreateChild<Ui::IconButton>(
		_toolbar,
		st::infoTopBarDelete);
	_storeButton->setIconOverride(&st::menuIconShop, &st::menuIconShop);
	_storeButton->setClickedCallback([=] {
		setStoreMode(!_storeMode);
	});

	const auto layoutBar = [=](int w) {
		if (w <= 0 || !_toolbar || !_folderButton || !_refreshButton
			|| !_storeButton || !_toolbarHint) {
			return;
		}
		_toolbar->resize(w, kToolbarH);
		_folderButton->moveToLeft(
			w - _folderButton->width(),
			(kToolbarH - _folderButton->height()) / 2);
		_refreshButton->moveToLeft(
			_folderButton->x() - _refreshButton->width(),
			(kToolbarH - _refreshButton->height()) / 2);
		_storeButton->moveToLeft(
			_refreshButton->x() - _storeButton->width(),
			(kToolbarH - _storeButton->height()) / 2);
		_toolbarHint->resizeToWidth(std::max(_storeButton->x() - 8, 40));
		_toolbarHint->moveToLeft(
			0,
			(kToolbarH - _toolbarHint->height()) / 2);
		_toolbarHint->show();
		_folderButton->show();
		_refreshButton->show();
		_storeButton->show();
	};
	_toolbar->widthValue(
	) | rpl::on_next(layoutBar, _toolbar->lifetime());
}

void Plugins::setStoreMode(bool storeMode) {
	if (_storeMode == storeMode) {
		if (storeMode) {
			refreshStoreCatalog(true);
		}
		return;
	}
	_storeMode = storeMode;
	if (_toolbarHint) {
		_toolbarHint->setText(_storeMode
			? QStringLiteral("Plugin Store")
			: QStringLiteral("Installed plugins"));
	}
	if (_storeButton) {
		_storeButton->setIconOverride(
			_storeMode ? &st::menuIconShop : &st::menuIconShop,
			_storeMode ? &st::menuIconShop : &st::menuIconShop);
		_storeButton->setIconColorOverride(_storeMode
			? std::make_optional(st::windowBgActive->c)
			: std::nullopt);
		_storeButton->update();
	}
	if (_panels) {
		_panels->setVisible(!_storeMode);
	}
	rebuildList();
	if (_storeMode) {
		refreshStoreCatalog(true);
	}
}

void Plugins::refreshStoreCatalog(bool silent) {
	if (_storeLoading) {
		return;
	}
	_storeLoading = true;
	if (_storeMode) {
		rebuildStoreList();
	}
	const auto weak = base::make_weak(this);
	Core::App().plugins().fetchStoreCatalog([=](bool ok, QString error) {
		if (!weak) {
			return;
		}
		_storeLoading = false;
		if (_storeMode) {
			rebuildStoreList();
		}
		if (!silent) {
			if (ok) {
				controller()->showToast(error.isEmpty()
					? QStringLiteral(
						"Store updated from GitHub (including installed plugins).")
					: error);
			} else {
				controller()->showToast(error.isEmpty()
					? QStringLiteral("Could not load store.")
					: error);
			}
		} else if (ok && !error.isEmpty()) {
			controller()->showToast(error);
		}
	});
}

void Plugins::requestRebuild() {
	if (_rebuildQueued) {
		return;
	}
	_rebuildQueued = true;
	Ui::PostponeCall(this, [=] {
		_rebuildQueued = false;
		rebuildList();
	});
}

void Plugins::syncCardToggles() {
	auto enabledById = base::flat_map<QString, bool>();
	for (const auto &plugin : Core::App().plugins().plugins()) {
		enabledById.emplace(
			plugin.manifest.id,
			plugin.state == PluginSystem::PluginState::Enabled);
	}
	for (const auto &card : _cards) {
		if (!card) {
			continue;
		}
		if (const auto it = enabledById.find(card->pluginId())
			; it != enabledById.end()) {
			card->setEnabledVisual(it->second);
		}
	}
}

void Plugins::rebuildPanels() {
	Expects(_panels != nullptr);

	_panels->clear();
	Core::App().plugins().host().rebuildUiExtensions();
	for (const auto &panel
			: PluginSystem::UiExtensionRegistry::Instance().panels()) {
		if (panel.placement != u"settings.sidebar"_q) {
			continue;
		}
		const auto title = panel.title;
		const auto pluginId = panel.pluginId;
		const auto actions = panel.actions;
		const auto button = _panels->add(
			object_ptr<Ui::SettingsButton>(
				_panels,
				rpl::single(title),
				st::settingsButton));
		button->setClickedCallback([=, c = controller()] {
			if (actions.empty()) {
				c->showToast(title);
				return;
			}
			for (const auto &action : actions) {
				if (!Core::App().plugins().runUtilityCommand(
						pluginId,
						action.id)) {
					c->showToast(action.title);
				}
			}
		});
	}
	if (_root) {
		_root->resizeToWidth(std::max(width(), 1));
	}
}

void Plugins::handleCardToggled() {
	// Do not destroy cards here — ToggleView animation must finish.
	syncCardToggles();
	rebuildPanels();
}

void Plugins::rebuildInstalledList() {
	Expects(_list != nullptr);

	_cards.clear();
	_list->clear();

	auto &manager = Core::App().plugins();
	manager.refresh();

	const auto reload = [=] { requestRebuild(); };
	const auto onToggled = [=] { handleCardToggled(); };
	const auto &plugins = manager.plugins();
	if (plugins.empty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				QStringLiteral("No plugins yet. Drop a folder into plugins."),
				st::defaultFlatLabel),
			style::margins(0, 8, 0, 8));
	} else {
		for (const auto &plugin : plugins) {
			const auto card = _list->add(
				object_ptr<PluginCard>(
					_list,
					controller(),
					plugin,
					reload,
					onToggled),
				style::margins(0, 0, 0, kCardGap));
			_cards.emplace_back(card);
		}
	}
}

void Plugins::rebuildStoreList() {
	Expects(_list != nullptr);

	_cards.clear();
	_list->clear();

	auto &manager = Core::App().plugins();
	manager.refreshStore();

	if (_storeLoading && manager.remoteIndex().entries.empty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				QStringLiteral("Loading store…"),
				st::defaultFlatLabel),
			style::margins(0, 8, 0, 8));
		return;
	}

	const auto &index = manager.remoteIndex();
	if (index.entries.empty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				QStringLiteral(
					"No GitHub plugins found. Authors: add topic "
					"\"plugingram-plugin\" and a plugingram.json file, "
					"then tap refresh."),
				st::defaultFlatLabel),
			style::margins(0, 8, 0, 8));
		return;
	}

	auto installedVersion = base::flat_map<QString, QString>();
	for (const auto &plugin : manager.plugins()) {
		installedVersion.emplace(
			plugin.manifest.id,
			plugin.manifest.version);
	}

	const auto onInstalled = [=] {
		rebuildStoreList();
	};
	for (const auto &entry : index.entries) {
		auto mode = StoreInstallButton::Mode::Install;
		if (const auto it = installedVersion.find(entry.id)
			; it != installedVersion.end()) {
			mode = (it->second != entry.version)
				? StoreInstallButton::Mode::Update
				: StoreInstallButton::Mode::Installed;
		}
		_list->add(
			object_ptr<StoreCard>(
				_list,
				controller(),
				entry,
				mode,
				onInstalled),
			style::margins(0, 0, 0, kCardGap));
	}
}

void Plugins::rebuildList() {
	Expects(_list != nullptr);
	Expects(_panels != nullptr);

	// Never clear _root / _toolbar — only the dynamic lists.
	if (_storeMode) {
		rebuildStoreList();
	} else {
		rebuildInstalledList();
		rebuildPanels();
	}
	_toolbar->show();
	if (_root) {
		_root->resizeToWidth(std::max(width(), 1));
	}
}

void Plugins::setupContent() {
	_root = Ui::CreateChild<Ui::VerticalLayout>(this);
	setupToolbar();
	_list = _root->add(
		object_ptr<Ui::VerticalLayout>(_root),
		style::margins(kPagePad, 0, kPagePad, 0));
	_panels = _root->add(
		object_ptr<Ui::VerticalLayout>(_root),
		style::margins(kPagePad / 2, 4, kPagePad / 2, 8));

	Ui::ResizeFitChild(this, _root);
	rebuildList();

	widthValue(
	) | rpl::filter([](int w) {
		return w > 0;
	}) | rpl::on_next([=](int w) {
		if (_root) {
			_root->resizeToWidth(w);
		}
	}, lifetime());
}

} // namespace

Type PluginsId() {
	return Plugins::Id();
}

void ApplyRainbowToButton(
		not_null<Ui::SettingsButton*> button,
		not_null<const style::icon*> icon) {
	if (button->property("pluginsRainbow").toBool()) {
		return;
	}
	button->setProperty("pluginsRainbow", true);
	button->setColorOverride(QColor(0, 0, 0, 0));

	for (const auto child : button->findChildren<Ui::RpWidget*>(
			QString(),
			Qt::FindDirectChildrenOnly)) {
		if (child->size() == icon->size()) {
			child->hide();
		}
	}

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(button.get());
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
	}, overlay->lifetime());

	const auto animation = overlay->lifetime().make_state<Ui::Animations::Basic>();
	animation->init([=] {
		overlay->update();
	});
	animation->start();

	overlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(overlay);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setClipRect(clip);

		const auto phase = RainbowPhase();
		const auto rtl = style::RightToLeft();
		const auto iconTop = (overlay->height() - icon->height()) / 2;
		const auto iconLeft = rtl
			? (overlay->width() - button->st().iconLeft - icon->width())
			: button->st().iconLeft;
		PaintRainbowIcon(p, icon, { iconLeft, iconTop }, phase);

		const auto text = button->accessibilityName();
		const auto &font = button->st().style.font;
		const auto &padding = button->st().padding;
		const auto textWidth = std::max(font->width(text), 1);
		const auto textLeft = rtl
			? (overlay->width() - padding.left() - textWidth)
			: padding.left();
		PaintRainbowText(
			p,
			text,
			font,
			QPointF(textLeft, padding.top() + font->ascent),
			textWidth,
			phase);
	}, overlay->lifetime());

	overlay->show();
	overlay->raise();
}

void ApplyPluginsRainbowToButton(not_null<Ui::SettingsButton*> button) {
	ApplyRainbowToButton(button, &st::menuIconShop);
}

void ApplyPlugingramFeaturesRainbowToButton(
		not_null<Ui::SettingsButton*> button) {
	if (button->property("pluginsRainbow").toBool()) {
		return;
	}
	button->setProperty("pluginsRainbow", true);
	button->setColorOverride(QColor(0, 0, 0, 0));

	// Never use a bitmap icon here — painted "P" only (fixes rainbow square).
	for (const auto child : button->findChildren<Ui::RpWidget*>(
			QString(),
			Qt::FindDirectChildrenOnly)) {
		const auto size = child->size();
		if (size.width() >= 20
			&& size.width() <= 28
			&& size.height() >= 20
			&& size.height() <= 28) {
			child->hide();
		}
	}

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(button.get());
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
	}, overlay->lifetime());

	const auto animation = overlay->lifetime().make_state<Ui::Animations::Basic>();
	animation->init([=] {
		overlay->update();
	});
	animation->start();

	overlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(overlay);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setClipRect(clip);

		const auto phase = RainbowPhase();
		const auto rtl = style::RightToLeft();
		constexpr auto kIcon = 24;
		const auto iconTop = (overlay->height() - kIcon) / 2;
		const auto iconLeft = rtl
			? (overlay->width() - button->st().iconLeft - kIcon)
			: button->st().iconLeft;

		{
			auto font = QFont(QStringLiteral("Segoe UI"));
			font.setPixelSize(20);
			font.setWeight(QFont::DemiBold);
			const auto letter = QStringLiteral("P");
			const auto metrics = QFontMetrics(font);
			const auto tw = metrics.horizontalAdvance(letter);
			const auto x = iconLeft + (kIcon - tw) / 2.;
			const auto y = iconTop
				+ (kIcon + metrics.ascent() - metrics.descent()) / 2.
				- 1.;
			auto path = QPainterPath();
			path.addText(QPointF(x, y), font, letter);
			p.fillPath(path, MakeRainbowGradient(kIcon, phase));
		}

		const auto text = button->accessibilityName();
		const auto &font = button->st().style.font;
		const auto &padding = button->st().padding;
		const auto textWidth = std::max(font->width(text), 1);
		const auto textLeft = rtl
			? (overlay->width() - padding.left() - textWidth)
			: padding.left();
		PaintRainbowText(
			p,
			text,
			font,
			QPointF(textLeft, padding.top() + font->ascent),
			textWidth,
			phase);
	}, overlay->lifetime());

	overlay->show();
	overlay->raise();
}

namespace Builder {

SectionBuildMethod PluginsSection = kMeta.build;

} // namespace Builder

} // namespace Settings
