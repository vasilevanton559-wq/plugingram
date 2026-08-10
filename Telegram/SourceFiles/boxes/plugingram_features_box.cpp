/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/plugingram_features_box.h"

#include "core/click_handler_types.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

void AddSection(
		not_null<Ui::VerticalLayout*> layout,
		const QString &title,
		const QString &body) {
	layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			rpl::single(title),
			st::defaultSubsectionTitle),
		st::defaultSubsectionTitlePadding);
	const auto label = layout->add(
		object_ptr<Ui::FlatLabel>(
			layout,
			rpl::single(TextWithEntities{ body }),
			st::aboutLabel),
		st::boxRowPadding);
	label->setSelectable(true);
	Ui::AddSkip(layout, st::aboutSkip);
}

} // namespace

void PlugingramFeaturesBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"Возможности Plugingram"_q));
	box->setWidth(st::boxWideWidth);

	auto layout = box->verticalLayout();

	const auto lead = layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(TextWithEntities{
				u"Pre-beta клиент на базе Telegram Desktop 7.0.9. "
				"Ниже — что даёт именно Plugingram поверх обычного Telegram."_q
			}),
			st::aboutLabel),
		st::boxRowPadding);
	lead->setSelectable(true);
	Ui::AddSkip(layout, st::aboutSkip);

	AddSection(
		layout,
		u"База Telegram"_q,
		u"Чаты, группы, каналы, папки, архив, звонки, темы, прокси и остальной "
		"функционал Telegram Desktop — как в официальном клиенте."_q);

	AddSection(
		layout,
		u"Плагины"_q,
		u"Раздел «Настройки → Плагины»: установка из Store (GitHub), "
		"включение и выключение, избранное. Плагины могут менять оформление, "
		"добавлять панели и кнопки, настраивать прозрачность окна."_q);

	AddSection(
		layout,
		u"Noise"_q,
		u"Встроенный плагин: телефоны и коды в профилях закрыты шумом спойлера. "
		"Клик по блюру — показать. Нельзя удалить, только выключить."_q);

	AddSection(
		layout,
		u"Диагностика"_q,
		u"Ctrl+Shift+I — оверлей с версией, памятью, сессией, плагинами и "
		"последними ошибками. Логи пишутся в папку logs рядом с данными клиента."_q);

	auto channelText = TextWithEntities{ u"Канал: "_q };
	channelText.append(tr::link(
		u"@plugingram_official"_q,
		u"https://t.me/plugingram_official"_q));
	const auto channel = layout->add(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(std::move(channelText)),
			st::aboutLabel),
		st::boxRowPadding);
	channel->setLinksTrusted();

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
