/*
Destinations for OBS
Copyright (C) 2026 Sébastien Tombre

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "dialogs.hpp"
#include "destination.hpp"
#include "manager.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <vector>

#define T(key) QString::fromUtf8(obs_module_text(key))

namespace Dialogs {

bool EditScene(QWidget *parent, obs_source_t *scene, std::string &name, std::set<Destination *> &coverage, bool isNew)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(isNew ? T("Destinations.AddScene") : T("Destinations.SceneProperties"));
	auto *layout = new QVBoxLayout(&dialog);

	auto *form = new QFormLayout();
	auto *nameEdit = new QLineEdit(QString::fromStdString(name), &dialog);
	nameEdit->selectAll();
	form->addRow(T("Destinations.SceneName"), nameEdit);
	layout->addLayout(form);

	auto *group = new QGroupBox(T("Destinations.CoveredDestinations"), &dialog);
	auto *groupLayout = new QVBoxLayout(group);
	std::vector<std::pair<Destination *, QCheckBox *>> boxes;
	for (auto *d : Manager::Get().Destinations()) {
		auto *box = new QCheckBox(
			QString("%1  (%2×%3)").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height()),
			group);
		box->setChecked(isNew || !scene ? true : d->Covers(scene));
		groupLayout->addWidget(box);
		boxes.emplace_back(d, box);
	}
	layout->addWidget(group);

	if (!isNew) {
		auto *note = new QLabel(T("Destinations.CoverageNote"), &dialog);
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;

	name = nameEdit->text().trimmed().toStdString();
	coverage.clear();
	for (auto &[d, box] : boxes) {
		if (box->isChecked())
			coverage.insert(d);
	}
	return !name.empty();
}

bool EditDestination(QWidget *parent, Destination *dest, DestinationSettings &settings)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(dest ? T("Destinations.DestinationSettings") : T("Destinations.AddDestination"));
	auto *form = new QFormLayout(&dialog);

	if (dest && dest->IsMain()) {
		auto *info = new QLabel(T("Destinations.MainInfo").arg(dest->Width()).arg(dest->Height()), &dialog);
		info->setWordWrap(true);
		form->addRow(info);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		form->addRow(buttons);
		dialog.exec();
		return false;
	}

	auto *nameEdit = new QLineEdit(QString::fromStdString(settings.name), &dialog);
	form->addRow(T("Destinations.Name"), nameEdit);

	auto *widthSpin = new QSpinBox(&dialog);
	widthSpin->setRange(16, 16384);
	widthSpin->setValue(int(settings.width));
	auto *heightSpin = new QSpinBox(&dialog);
	heightSpin->setRange(16, 16384);
	heightSpin->setValue(int(settings.height));
	form->addRow(T("Destinations.Width"), widthSpin);
	form->addRow(T("Destinations.Height"), heightSpin);

	auto *transitionCombo = new QComboBox(&dialog);
	for (const auto &[id, display] : Destination::TransitionTypes()) {
		transitionCombo->addItem(QString::fromStdString(display), QString::fromStdString(id));
		if (id == settings.transition)
			transitionCombo->setCurrentIndex(transitionCombo->count() - 1);
	}
	form->addRow(T("Destinations.Transition"), transitionCombo);

	auto *durationSpin = new QSpinBox(&dialog);
	durationSpin->setRange(0, 20000);
	durationSpin->setSingleStep(100);
	durationSpin->setSuffix(" ms");
	durationSpin->setValue(settings.duration);
	form->addRow(T("Destinations.Duration"), durationSpin);

	QCheckBox *coverAll = nullptr;
	if (!dest) {
		coverAll = new QCheckBox(T("Destinations.CoverAllScenes"), &dialog);
		coverAll->setChecked(settings.coverAll);
		form->addRow(coverAll);
	} else {
		auto *note = new QLabel(T("Destinations.ResolutionNote"), &dialog);
		note->setWordWrap(true);
		form->addRow(note);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;

	settings.name = nameEdit->text().trimmed().toStdString();
	settings.width = uint32_t(widthSpin->value());
	settings.height = uint32_t(heightSpin->value());
	settings.transition = transitionCombo->currentData().toString().toStdString();
	settings.duration = durationSpin->value();
	if (coverAll)
		settings.coverAll = coverAll->isChecked();
	return !settings.name.empty();
}

} // namespace Dialogs
