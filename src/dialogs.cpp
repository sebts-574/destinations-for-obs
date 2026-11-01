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
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

#include <vector>

namespace Dialogs {

QScreen *FindScreen(const std::string &name)
{
	if (name.empty())
		return nullptr;
	for (QScreen *screen : QGuiApplication::screens()) {
		if (screen->name().toStdString() == name)
			return screen;
	}
	return nullptr;
}

QString ScreenLabel(QScreen *screen, int index)
{
	const QRect g = screen->geometry();
	const qreal ratio = screen->devicePixelRatio();
	return QString("%1 %2 : %3 (%4×%5)")
		.arg(Tr("MDS.Screen"))
		.arg(index + 1)
		.arg(screen->name())
		.arg(int(g.width() * ratio))
		.arg(int(g.height() * ratio));
}

bool EditScene(QWidget *parent, obs_source_t *scene, std::string &name, std::set<Destination *> &coverage, bool isNew)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(isNew ? Tr("MDS.NewScene") : Tr("MDS.SceneSettings"));
	dialog.setMinimumWidth(380);
	auto *layout = new QVBoxLayout(&dialog);

	auto *form = new QFormLayout();
	auto *nameEdit = new QLineEdit(QString::fromStdString(name), &dialog);
	nameEdit->selectAll();
	form->addRow(Tr("MDS.Name"), nameEdit);
	layout->addLayout(form);

	auto *group = new QGroupBox(Tr("MDS.ShowSceneOn"), &dialog);
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
		auto *note = new QLabel(Tr("MDS.UncheckWarning"), &dialog);
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

static QComboBox *ScreenCombo(const std::string &current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	combo->addItem(Tr("MDS.NoScreen"), QString());
	const QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		combo->addItem(ScreenLabel(screens[i], i), screens[i]->name());
		if (screens[i]->name().toStdString() == current)
			combo->setCurrentIndex(combo->count() - 1);
	}
	/* Écran configuré mais débranché : on le garde dans la liste. */
	if (!current.empty() && !FindScreen(current)) {
		combo->addItem(Tr("MDS.ScreenMissing").arg(QString::fromStdString(current)),
			       QString::fromStdString(current));
		combo->setCurrentIndex(combo->count() - 1);
	}
	return combo;
}

bool EditDestination(QWidget *parent, Destination *dest, DestinationSettings &settings)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(dest ? Tr("MDS.DestinationSettingsTitle").arg(QString::fromStdString(dest->Name()))
				   : Tr("MDS.NewDestination"));
	dialog.setMinimumWidth(420);
	auto *form = new QFormLayout(&dialog);

	const bool isMain = dest && dest->IsMain();
	QLineEdit *nameEdit = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;

	if (isMain) {
		form->addRow(Tr("MDS.Name"), new QLabel(QString::fromStdString(dest->Name()), &dialog));
		auto *res = new QLabel(QString("%1×%2").arg(dest->Width()).arg(dest->Height()), &dialog);
		form->addRow(Tr("MDS.Resolution"), res);
		auto *info = new QLabel(Tr("MDS.MainInfo"), &dialog);
		info->setWordWrap(true);
		form->addRow(info);
	} else {
		nameEdit = new QLineEdit(QString::fromStdString(settings.name), &dialog);
		form->addRow(Tr("MDS.Name"), nameEdit);

		widthSpin = new QSpinBox(&dialog);
		widthSpin->setRange(16, 16384);
		widthSpin->setValue(int(settings.width));
		heightSpin = new QSpinBox(&dialog);
		heightSpin->setRange(16, 16384);
		heightSpin->setValue(int(settings.height));
		auto *resRow = new QHBoxLayout();
		resRow->addWidget(widthSpin);
		resRow->addWidget(new QLabel("×", &dialog));
		resRow->addWidget(heightSpin);
		resRow->addStretch(1);
		form->addRow(Tr("MDS.Resolution"), resRow);
	}

	QComboBox *screenCombo = ScreenCombo(settings.outputScreen, &dialog);
	form->addRow(Tr("MDS.OutputScreen"), screenCombo);

	/* Préremplir la résolution avec celle de l'écran choisi. */
	if (widthSpin && heightSpin) {
		QObject::connect(screenCombo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
			QScreen *screen = FindScreen(screenCombo->currentData().toString().toStdString());
			if (!screen)
				return;
			const qreal ratio = screen->devicePixelRatio();
			widthSpin->setValue(int(screen->geometry().width() * ratio));
			heightSpin->setValue(int(screen->geometry().height() * ratio));
		});
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	if (!dest)
		buttons->button(QDialogButtonBox::Ok)->setText(Tr("MDS.Create"));
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;

	if (nameEdit)
		settings.name = nameEdit->text().trimmed().toStdString();
	if (widthSpin)
		settings.width = uint32_t(widthSpin->value());
	if (heightSpin)
		settings.height = uint32_t(heightSpin->value());
	settings.outputScreen = screenCombo->currentData().toString().toStdString();
	return isMain || !settings.name.empty();
}

bool ConfirmDeleteDestination(QWidget *parent, Destination *dest)
{
	QMessageBox box(parent);
	box.setIcon(QMessageBox::Warning);
	box.setWindowTitle(Tr("MDS.DeleteDestinationTitle"));
	box.setText(Tr("MDS.DeleteDestinationText").arg(QString::fromStdString(dest->Name())));
	box.setInformativeText(Tr("MDS.DeleteDestinationInfo"));
	QPushButton *yes = box.addButton(Tr("MDS.Delete"), QMessageBox::DestructiveRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();
	return box.clickedButton() == yes;
}

} // namespace Dialogs
