/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/support_creator_box.h"

#include "lang/lang_keys.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

void SupportCreatorBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"Поддержать создателя"_q));
	box->setWidth(st::boxWidth);

	const auto wallet = u"4100119280554898"_q;
	auto layout = box->verticalLayout();

	const auto about = layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(u"Добровольная поддержка разработки Plugingram. "
				"Любая сумма помогает двигать клиент дальше.\n\n"
				"Перевод на кошелёк ЮMoney:"_q),
			st::boxLabel),
		st::boxRowPadding);
	about->setSelectable(true);

	Ui::AddSkip(layout);

	const auto walletLabel = layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(wallet),
			st::boxLabel),
		st::boxRowPadding);
	walletLabel->setSelectable(true);
	walletLabel->setBreakEverywhere(true);

	Ui::AddSkip(layout);

	layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(u"Спасибо за поддержку!"_q),
			st::boxLabel),
		st::boxRowPadding);

	box->addButton(rpl::single(u"Скопировать"_q), [=] {
		QGuiApplication::clipboard()->setText(wallet);
		box->showToast(u"Номер кошелька скопирован"_q);
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
