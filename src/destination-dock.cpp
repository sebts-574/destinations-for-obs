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

#include "destination-dock.hpp"
#include "display-helpers.hpp"
#include "projector-window.hpp"
#include "qt-display.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#define T(key) QString::fromUtf8(obs_module_text(key))

static const QColor PREVIEW_COLOR(40, 170, 80);
static const QColor PROGRAM_COLOR(220, 50, 50);

/* ------------------------------------------------------------------ */
/* Rendu                                                                */
/* ------------------------------------------------------------------ */

void DrawDestination(Destination *dest, bool program, uint32_t cx, uint32_t cy)
{
	if (!dest || !dest->Attached() || cx == 0 || cy == 0)
		return;

	const int w = int(dest->Width());
	const int h = int(dest->Height());
	if (w <= 0 || h <= 0)
		return;

	int x, y;
	float scale;
	GetScaleAndCenterPos(w, h, int(cx), int(cy), x, y, scale);
	const int newCX = int(scale * float(w));
	const int newCY = int(scale * float(h));

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);

	if (program) {
		dest->RenderProgram();
	} else {
		const bool previous = gs_set_linear_srgb(true);
		dest->RenderPreview();
		gs_set_linear_srgb(previous);
	}

	gs_projection_pop();
	gs_viewport_pop();
}

void DestinationDock::DrawPreviewCb(void *data, uint32_t cx, uint32_t cy)
{
	auto *dock = static_cast<DestinationDock *>(data);
	DrawDestination(dock->dest, false, cx, cy);
}

void DestinationDock::DrawProgramCb(void *data, uint32_t cx, uint32_t cy)
{
	auto *dock = static_cast<DestinationDock *>(data);
	DrawDestination(dock->dest, true, cx, cy);
}

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

DestinationDock::DestinationDock(Destination *destination, QWidget *parent) : QWidget(parent), dest(destination)
{
	BuildUi();

	tallyTimer = new QTimer(this);
	connect(tallyTimer, &QTimer::timeout, this, &DestinationDock::UpdateTally);
	tallyTimer->start(150);
}

DestinationDock::~DestinationDock()
{
	Shutdown();
}

void DestinationDock::Shutdown()
{
	CloseProjectors();
	if (tallyTimer)
		tallyTimer->stop();
	if (previewDisplay && previewDisplay->GetDisplay())
		obs_display_remove_draw_callback(previewDisplay->GetDisplay(), DrawPreviewCb, this);
	if (programDisplay && programDisplay->GetDisplay())
		obs_display_remove_draw_callback(programDisplay->GetDisplay(), DrawProgramCb, this);
}

static QWidget *MakeMonitor(OBSQTDisplay *&display, QLabel *&label, const QString &title, const QColor &color,
			    QWidget *parent)
{
	auto *box = new QWidget(parent);
	auto *layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	label = new QLabel(title, box);
	label->setStyleSheet(QString("QLabel { font-weight: bold; color: white; background: %1; padding: 2px 6px; }")
				     .arg(color.name()));
	layout->addWidget(label);

	display = new OBSQTDisplay(box);
	display->setMinimumSize(192, 108);
	display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(display, 1);
	return box;
}

void DestinationDock::BuildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	/* --- En-tête --- */
	auto *header = new QHBoxLayout();
	titleLabel = new QLabel(this);
	titleLabel->setStyleSheet("QLabel { font-weight: bold; }");
	header->addWidget(titleLabel, 1);

	auto *resButton = new QPushButton(T("Destinations.Resolution"), this);
	connect(resButton, &QPushButton::clicked, this, &DestinationDock::OnResolution);
	header->addWidget(resButton);

	projectorButton = new QToolButton(this);
	projectorButton->setText(T("Destinations.Projector"));
	projectorButton->setPopupMode(QToolButton::InstantPopup);
	auto *projectorMenu = new QMenu(projectorButton);
	projectorButton->setMenu(projectorMenu);
	connect(projectorMenu, &QMenu::aboutToShow, this, &DestinationDock::FillProjectorMenu);
	header->addWidget(projectorButton);
	root->addLayout(header);

	/* --- Moniteurs PVW / PGM --- */
	auto *monitors = new QSplitter(Qt::Horizontal, this);
	monitors->addWidget(MakeMonitor(previewDisplay, previewLabel, "PREVIEW", PREVIEW_COLOR, monitors));
	monitors->addWidget(MakeMonitor(programDisplay, programLabel, "PROGRAM", PROGRAM_COLOR, monitors));
	root->addWidget(monitors, 3);

	connect(previewDisplay, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), DrawPreviewCb, this); });
	connect(programDisplay, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), DrawProgramCb, this); });

	/* --- Transition --- */
	auto *transRow = new QHBoxLayout();
	transitionCombo = new QComboBox(this);
	connect(transitionCombo, &QComboBox::currentIndexChanged, this, &DestinationDock::OnTransitionChanged);
	transRow->addWidget(transitionCombo, 1);

	durationSpin = new QSpinBox(this);
	durationSpin->setRange(0, 20000);
	durationSpin->setSingleStep(100);
	durationSpin->setSuffix(" ms");
	connect(durationSpin, &QSpinBox::valueChanged, this, &DestinationDock::OnDurationChanged);
	transRow->addWidget(durationSpin);

	cutButton = new QPushButton("CUT", this);
	cutButton->setMinimumHeight(32);
	connect(cutButton, &QPushButton::clicked, this, &DestinationDock::OnCut);
	transRow->addWidget(cutButton);

	takeButton = new QPushButton("AUTO", this);
	takeButton->setMinimumHeight(32);
	takeButton->setStyleSheet(QString("QPushButton { font-weight: bold; }"));
	connect(takeButton, &QPushButton::clicked, this, &DestinationDock::OnTake);
	transRow->addWidget(takeButton);
	root->addLayout(transRow);

	/* --- Scènes et sources --- */
	auto *lists = new QHBoxLayout();

	auto *sceneCol = new QVBoxLayout();
	sceneCol->addWidget(new QLabel(T("Destinations.Scenes"), this));
	sceneList = new QListWidget(this);
	connect(sceneList, &QListWidget::itemSelectionChanged, this, &DestinationDock::OnSceneSelected);
	sceneCol->addWidget(sceneList, 1);
	auto *sceneButtons = new QHBoxLayout();
	auto *addScene = new QPushButton("+", this);
	auto *removeScene = new QPushButton("−", this);
	connect(addScene, &QPushButton::clicked, this, &DestinationDock::OnAddScene);
	connect(removeScene, &QPushButton::clicked, this, &DestinationDock::OnRemoveScene);
	sceneButtons->addWidget(addScene);
	sceneButtons->addWidget(removeScene);
	sceneButtons->addStretch(1);
	sceneCol->addLayout(sceneButtons);
	lists->addLayout(sceneCol, 1);

	auto *itemCol = new QVBoxLayout();
	itemCol->addWidget(new QLabel(T("Destinations.Sources"), this));
	itemList = new QListWidget(this);
	itemCol->addWidget(itemList, 1);
	auto *itemButtons = new QHBoxLayout();
	addSourceButton = new QToolButton(this);
	addSourceButton->setText("+");
	addSourceButton->setPopupMode(QToolButton::InstantPopup);
	auto *sourceMenu = new QMenu(addSourceButton);
	addSourceButton->setMenu(sourceMenu);
	connect(sourceMenu, &QMenu::aboutToShow, this, &DestinationDock::FillAddSourceMenu);
	auto *removeItem = new QPushButton("−", this);
	connect(removeItem, &QPushButton::clicked, this, &DestinationDock::OnRemoveItem);
	itemButtons->addWidget(addSourceButton);
	itemButtons->addWidget(removeItem);
	itemButtons->addStretch(1);
	itemCol->addLayout(itemButtons);
	lists->addLayout(itemCol, 1);

	root->addLayout(lists, 2);
}

/* ------------------------------------------------------------------ */
/* Rafraîchissement                                                     */
/* ------------------------------------------------------------------ */

void DestinationDock::RefreshAll()
{
	titleLabel->setText(
		QString("%1 — %2×%3").arg(QString::fromStdString(dest->Name())).arg(dest->Width()).arg(dest->Height()));
	RefreshTransitions();
	RefreshScenes();
	lastPreview.clear();
	lastProgram.clear();
	UpdateTally();
}

void DestinationDock::ClearUi()
{
	const QSignalBlocker blocker(sceneList);
	sceneList->clear();
	itemList->clear();
	lastPreview.clear();
	lastProgram.clear();
}

void DestinationDock::RefreshScenes()
{
	const QSignalBlocker blocker(sceneList);
	sceneList->clear();

	const std::string preview = dest->PreviewSceneName();
	for (const auto &name : dest->SceneNames()) {
		auto *item = new QListWidgetItem(QString::fromStdString(name), sceneList);
		if (name == preview)
			sceneList->setCurrentItem(item);
	}
	RefreshItems();
}

void DestinationDock::RefreshItems()
{
	itemList->clear();

	OBSSourceAutoRelease source = dest->GetSceneSource(dest->PreviewSceneName());
	obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
	if (!scene)
		return;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *list = static_cast<QListWidget *>(param);
			obs_source_t *s = obs_sceneitem_get_source(item);
			auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(s)));
			row->setData(Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
			/* obs_scene_enum_items va du bas vers le haut : on affiche le
			 * haut de la pile en premier, comme OBS. */
			list->insertItem(0, row);
			return true;
		},
		itemList);
}

void DestinationDock::RefreshTransitions()
{
	const QSignalBlocker blocker(transitionCombo);
	transitionCombo->clear();

	const std::string current = dest->TransitionType();
	for (const auto &[id, display] : Destination::TransitionTypes()) {
		/* Le stinger a besoin d'un fichier vidéo : réglages en V0.2. */
		if (id == "obs_stinger_transition")
			continue;
		transitionCombo->addItem(QString::fromStdString(display), QString::fromStdString(id));
		if (id == current)
			transitionCombo->setCurrentIndex(transitionCombo->count() - 1);
	}

	const QSignalBlocker blocker2(durationSpin);
	durationSpin->setValue(dest->TransitionDuration());
}

void DestinationDock::UpdateTally()
{
	const std::string preview = dest->PreviewSceneName();
	const std::string program = dest->ProgramSceneName();
	if (preview == lastPreview && program == lastProgram)
		return;
	lastPreview = preview;
	lastProgram = program;

	previewLabel->setText(QString("PREVIEW  %1").arg(QString::fromStdString(preview)));
	programLabel->setText(QString("PROGRAM  %1").arg(QString::fromStdString(program)));

	for (int i = 0; i < sceneList->count(); i++) {
		QListWidgetItem *item = sceneList->item(i);
		const std::string name = item->text().toStdString();
		QFont font = item->font();
		font.setBold(name == program || name == preview);
		item->setFont(font);
		if (name == program)
			item->setForeground(PROGRAM_COLOR);
		else if (name == preview)
			item->setForeground(PREVIEW_COLOR);
		else
			item->setForeground(palette().color(QPalette::Text));
	}
}

/* ------------------------------------------------------------------ */
/* Actions                                                              */
/* ------------------------------------------------------------------ */

void DestinationDock::OnSceneSelected()
{
	QListWidgetItem *item = sceneList->currentItem();
	if (!item)
		return;
	dest->SetPreviewScene(item->text().toStdString());
	RefreshItems();
	UpdateTally();
}

void DestinationDock::OnAddScene()
{
	bool ok = false;
	const QString suggestion = T("Destinations.Scene") + QString(" %1").arg(sceneList->count() + 1);
	const QString name = QInputDialog::getText(this, T("Destinations.AddScene"), T("Destinations.SceneName"),
						   QLineEdit::Normal, suggestion, &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;

	if (!dest->CreateScene(name.toStdString())) {
		QMessageBox::warning(this, T("Destinations.AddScene"), T("Destinations.SceneExists"));
		return;
	}
	dest->SetPreviewScene(name.toStdString());
	RefreshScenes();
	UpdateTally();
}

void DestinationDock::OnRemoveScene()
{
	QListWidgetItem *item = sceneList->currentItem();
	if (!item)
		return;

	const QString name = item->text();
	if (QMessageBox::question(this, T("Destinations.RemoveScene"), T("Destinations.ConfirmRemove").arg(name)) !=
	    QMessageBox::Yes)
		return;

	dest->RemoveScene(name.toStdString());
	RefreshScenes();
	UpdateTally();
}

void DestinationDock::OnRemoveItem()
{
	QListWidgetItem *row = itemList->currentItem();
	if (!row)
		return;

	OBSSourceAutoRelease source = dest->GetSceneSource(dest->PreviewSceneName());
	obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
	if (!scene)
		return;

	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, row->data(Qt::UserRole).toLongLong());
	if (item)
		obs_sceneitem_remove(item);
	RefreshItems();
}

void DestinationDock::FillAddSourceMenu()
{
	QMenu *menu = addSourceButton->menu();
	menu->clear();

	const std::string sceneName = dest->PreviewSceneName();
	if (sceneName.empty()) {
		menu->addAction(T("Destinations.SelectSceneFirst"))->setEnabled(false);
		return;
	}

	auto addEntry = [this, sceneName](QMenu *target, obs_source_t *source) {
		OBSWeakSource weak = OBSGetWeakRef(source);
		QAction *action = target->addAction(QString::fromUtf8(obs_source_get_name(source)));
		connect(action, &QAction::triggered, this, [this, weak, sceneName]() {
			OBSSource strong = OBSGetStrongRef(weak);
			if (strong && dest->AddSourceToScene(sceneName, strong))
				RefreshItems();
		});
	};

	/* Sources (entrées) d'OBS : caméras, NDI, médias, navigateurs… */
	QMenu *inputs = menu->addMenu(T("Destinations.Inputs"));
	struct Ctx {
		std::vector<OBSSource> list;
	} ctx;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				c->list.emplace_back(source);
			return true;
		},
		&ctx);
	for (auto &s : ctx.list)
		addEntry(inputs, s);
	if (ctx.list.empty())
		inputs->addAction(T("Destinations.None"))->setEnabled(false);

	/* Scènes du canvas principal, imbriquées telles quelles. */
	QMenu *scenes = menu->addMenu(T("Destinations.MainScenes"));
	struct obs_frontend_source_list mainScenes = {};
	obs_frontend_get_scenes(&mainScenes);
	for (size_t i = 0; i < mainScenes.sources.num; i++)
		addEntry(scenes, mainScenes.sources.array[i]);
	if (mainScenes.sources.num == 0)
		scenes->addAction(T("Destinations.None"))->setEnabled(false);
	obs_frontend_source_list_free(&mainScenes);
}

void DestinationDock::OnTransitionChanged(int index)
{
	if (index < 0)
		return;
	dest->SetTransitionType(transitionCombo->itemData(index).toString().toStdString());
}

void DestinationDock::OnDurationChanged(int value)
{
	dest->SetTransitionDuration(value);
}

void DestinationDock::OnCut()
{
	dest->Cut();
	UpdateTally();
}

void DestinationDock::OnTake()
{
	dest->Take();
	UpdateTally();
}

void DestinationDock::OnResolution()
{
	QDialog dialog(this);
	dialog.setWindowTitle(T("Destinations.Resolution"));
	auto *form = new QFormLayout(&dialog);

	auto *widthSpin = new QSpinBox(&dialog);
	widthSpin->setRange(16, 16384);
	widthSpin->setValue(int(dest->Width()));
	auto *heightSpin = new QSpinBox(&dialog);
	heightSpin->setRange(16, 16384);
	heightSpin->setValue(int(dest->Height()));
	form->addRow(T("Destinations.Width"), widthSpin);
	form->addRow(T("Destinations.Height"), heightSpin);

	auto *note = new QLabel(T("Destinations.ResolutionNote"), &dialog);
	note->setWordWrap(true);
	form->addRow(note);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return;

	dest->Attach(uint32_t(widthSpin->value()), uint32_t(heightSpin->value()));
	RefreshAll();
}

/* ------------------------------------------------------------------ */
/* Projecteurs                                                          */
/* ------------------------------------------------------------------ */

void DestinationDock::FillProjectorMenu()
{
	QMenu *menu = projectorButton->menu();
	menu->clear();

	const QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		const QRect g = screens[i]->geometry();
		const qreal ratio = screens[i]->devicePixelRatio();
		const QString label = QString("%1 %2 : %3 (%4×%5)")
					      .arg(T("Destinations.Screen"))
					      .arg(i + 1)
					      .arg(screens[i]->name())
					      .arg(int(g.width() * ratio))
					      .arg(int(g.height() * ratio));
		connect(menu->addAction(label), &QAction::triggered, this, [this, i]() { OpenProjector(i); });
	}
	menu->addSeparator();
	connect(menu->addAction(T("Destinations.Windowed")), &QAction::triggered, this,
		[this]() { OpenProjector(-1); });
	menu->addSeparator();
	connect(menu->addAction(T("Destinations.CloseProjectors")), &QAction::triggered, this,
		&DestinationDock::CloseProjectors);
}

void DestinationDock::OpenProjector(int screenIndex)
{
	const QList<QScreen *> screens = QGuiApplication::screens();
	QScreen *screen = (screenIndex >= 0 && screenIndex < screens.size()) ? screens[screenIndex] : nullptr;

	/* Un seul projecteur par écran : on remplace l'ancien. */
	for (auto &p : projectors) {
		if (p && screen && p->TargetScreen() == screen)
			p->close();
	}

	auto *projector = new ProjectorWindow(dest, screen);
	projectors.emplace_back(projector);
	projector->Start();
}

void DestinationDock::CloseProjectors()
{
	for (auto &p : projectors) {
		if (p)
			p->close();
	}
	projectors.clear();
}
