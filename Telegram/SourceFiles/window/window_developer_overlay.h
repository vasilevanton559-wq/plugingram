// Plugingram: fullscreen developer diagnostics overlay (Ctrl+Shift+I).
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"

namespace Window {

class Controller;

class DeveloperOverlay final : public Ui::RpWidget {
public:
	DeveloperOverlay(
		not_null<Ui::RpWidget*> parent,
		not_null<Controller*> controller);
	~DeveloperOverlay();

	void showAnimated();
	void hideAnimated();
	[[nodiscard]] bool isShown() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupUi();
	void updateLayout();
	void refresh();
	void animationCallback();

	const not_null<Controller*> _controller;
	Ui::Animations::Simple _opacity;

	base::unique_qptr<Ui::RpWidget> _panel;
	base::unique_qptr<Ui::FlatLabel> _title;
	base::unique_qptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::FlatLabel> _body;
	base::unique_qptr<Ui::RoundButton> _openLogs;
	base::unique_qptr<Ui::RoundButton> _refreshBtn;
	base::unique_qptr<Ui::IconButton> _close;

	base::Timer _timer;
	bool _hiding = false;
	bool _shown = false;
};

} // namespace Window
