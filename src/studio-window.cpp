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

#include "studio-window.hpp"
#include "destination.hpp"
#include "dialogs.hpp"
#include "manager.hpp"
#include "projector-window.hpp"
#include "scene-editor.hpp"
#include "tile-grid.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#define T(key) QString::fromUtf8(obs_module_text(key))

static const QColor PREVIEW_COLOR(40, 170, 80);
static const QColor PROGRAM_COLOR(220, 50, 50);

static QToolButton *SmallButton(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setMinimumWidth(28);
	return b;
}

static QWidget *Panel(const QString &title, QLabel **titleOut, QListWidget *list, QHBoxLayout *buttons, QWidget *parent)
{
	auto *panel = new QWidget(parent);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(2);
	auto *label = new QLabel(title, panel);
	label->setStyleSheet("QLabel { font-weight: bold; }");
	if (titleOut)
		*titleOut = label;
	layout->addWidget(label);
	layout->addWidget(list, 1);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	return panel;
}

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

StudioWindow::StudioWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setWindowTitle(T("Destinations.StudioTitle"));
	BuildUi();

	Manager &m = Manager::Get();
	connect(&m, &Manager::previewChanged, this, &StudioWindow::RebuildGrids);
	connect(&m, &Manager::scenesChanged, this, [this]() {
		RefreshScenes();
		RebuildGrids();
	});
	connect(&m, &Manager::destinationsChanged, this, [this]() {
		RefreshDestinations();
		UpdateGrids(true);
	});
	connect(&m, &Manager::programChanged, this, &StudioWindow::RefreshTally);
	connect(&m, &Manager::aboutToReset, this, &StudioWindow::OnReset);
	connect(&m, &Manager::loaded, this, [this]() {
		followObs->setChecked(Manager::Get().FollowObs());
		RefreshDestinations();
		RefreshScenes();
		RebuildGrids();
	});
	connect(&m, &Manager::aboutToRemove, this, [this](Destination *dest) {
		CloseProjectors(dest);
		if (activeDest == dest)
			activeDest = nullptr;
		pvwGrid->Clear();
		pgmGrid->Clear();
	});
	connect(&m, &Manager::sceneCreatedInObs, this, [this](OBSSource scene) { AskCoverage(scene); });

	/* Les changements faits ailleurs (OBS, transitions en cours) se voient
	 * dans les en-têtes sans attendre d'événement. */
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (isVisible())
			RefreshTally();
	});
	timer->start(500);
}

StudioWindow::~StudioWindow()
{
	Shutdown();
}

void StudioWindow::BuildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(4);

	/* --- Barre du haut --- */
	auto *top = new QHBoxLayout();
	auto *title = new QLabel(T("Destinations.StudioTitle"), this);
	title->setStyleSheet("QLabel { font-weight: bold; font-size: 14px; }");
	top->addWidget(title);
	top->addStretch(1);
	followObs = new QCheckBox(T("Destinations.FollowObs"), this);
	followObs->setToolTip(T("Destinations.FollowObsTip"));
	followObs->setChecked(Manager::Get().FollowObs());
	connect(followObs, &QCheckBox::toggled, this, [](bool on) { Manager::Get().SetFollowObs(on); });
	top->addWidget(followObs);
	auto *quit = new QPushButton(T("Destinations.ExitStudio"), this);
	connect(quit, &QPushButton::clicked, this, &StudioWindow::Close);
	top->addWidget(quit);
	root->addLayout(top);

	auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(escape, &QShortcut::activated, this, &StudioWindow::Close);

	/* --- Moniteurs --- */
	auto *vsplit = new QSplitter(Qt::Vertical, this);

	auto *monitors = new QWidget(vsplit);
	auto *monLayout = new QHBoxLayout(monitors);
	monLayout->setContentsMargins(0, 0, 0, 0);

	auto makeColumn = [monitors](const QString &label, const QColor &color, TileGrid *grid) {
		auto *col = new QWidget(monitors);
		auto *l = new QVBoxLayout(col);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(2);
		auto *t = new QLabel(label, col);
		t->setAlignment(Qt::AlignCenter);
		t->setStyleSheet(QString("QLabel { color: white; font-weight: bold; background: %1; padding: 2px; }")
					 .arg(color.name()));
		l->addWidget(t);
		grid->setParent(col);
		l->addWidget(grid, 1);
		return col;
	};

	pvwGrid = new TileGrid(false, monitors);
	pgmGrid = new TileGrid(true, monitors);
	monLayout->addWidget(makeColumn("PREVIEW", PREVIEW_COLOR, pvwGrid), 1);

	auto *controls = new QVBoxLayout();
	controls->addStretch(1);
	auto *take = new QPushButton(T("Destinations.Take"), monitors);
	take->setMinimumSize(90, 48);
	take->setStyleSheet("QPushButton { font-weight: bold; }");
	take->setToolTip(T("Destinations.TakeTip"));
	connect(take, &QPushButton::clicked, this, []() { Manager::Get().Take(false); });
	controls->addWidget(take);
	auto *cut = new QPushButton("CUT", monitors);
	cut->setMinimumSize(90, 32);
	connect(cut, &QPushButton::clicked, this, []() { Manager::Get().Take(true); });
	controls->addWidget(cut);
	auto *note = new QLabel(T("Destinations.TransitionNote"), monitors);
	note->setWordWrap(true);
	note->setMaximumWidth(110);
	note->setStyleSheet("QLabel { font-size: 10px; }");
	controls->addWidget(note);
	controls->addStretch(1);
	monLayout->addLayout(controls);

	monLayout->addWidget(makeColumn("PROGRAM", PROGRAM_COLOR, pgmGrid), 1);
	vsplit->addWidget(monitors);

	/* --- Listes --- */
	auto *bottom = new QSplitter(Qt::Horizontal, vsplit);

	sceneList = new QListWidget(bottom);
	connect(sceneList, &QListWidget::itemSelectionChanged, this, [this]() {
		OBSSource scene = SelectedScene();
		if (scene)
			Manager::Get().SetPreviewScene(scene);
	});
	sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sceneList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		OBSSource scene = SelectedScene();
		QMenu menu(this);
		connect(menu.addAction(T("Destinations.AddScene")), &QAction::triggered, this, &StudioWindow::AddScene);
		if (scene) {
			connect(menu.addAction(T("Destinations.SceneProperties")), &QAction::triggered, this,
				[this, scene]() { EditScene(scene); });
			connect(menu.addAction(T("Destinations.RemoveScene")), &QAction::triggered, this,
				[this, scene]() { RemoveScene(scene); });
		}
		menu.exec(sceneList->viewport()->mapToGlobal(pos));
	});
	auto *sceneButtons = new QHBoxLayout();
	auto *addScene = SmallButton("+", T("Destinations.AddScene"), bottom);
	auto *removeScene = SmallButton("−", T("Destinations.RemoveScene"), bottom);
	auto *editScene = SmallButton("⚙", T("Destinations.SceneProperties"), bottom);
	connect(addScene, &QToolButton::clicked, this, &StudioWindow::AddScene);
	connect(removeScene, &QToolButton::clicked, this, [this]() { RemoveScene(SelectedScene()); });
	connect(editScene, &QToolButton::clicked, this, [this]() { EditScene(SelectedScene()); });
	sceneButtons->addWidget(addScene);
	sceneButtons->addWidget(removeScene);
	sceneButtons->addWidget(editScene);
	bottom->addWidget(Panel(T("Destinations.Scenes"), nullptr, sceneList, sceneButtons, bottom));

	sourceList = new QListWidget(bottom);
	connect(sourceList, &QListWidget::itemSelectionChanged, this, [this]() {
		if (refreshingSources)
			return;
		TileWidget *tile = ActivePreviewTile();
		QListWidgetItem *row = sourceList->currentItem();
		if (tile)
			tile->SetSelectedItem(row ? row->data(Qt::UserRole).toLongLong() : -1);
	});
	connect(sourceList, &QListWidget::itemChanged, this, [this](QListWidgetItem *row) {
		if (refreshingSources)
			return;
		TileWidget *tile = ActivePreviewTile();
		OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
		obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
		obs_sceneitem_t *item = s ? obs_scene_find_sceneitem_by_id(s, row->data(Qt::UserRole).toLongLong())
					  : nullptr;
		if (item)
			obs_sceneitem_set_visible(item, row->checkState() == Qt::Checked);
	});
	connect(sourceList, &QListWidget::itemDoubleClicked, this, [this]() { OpenSelectedSourceProperties(false); });
	auto *sourceButtons = new QHBoxLayout();
	addSourceButton = SmallButton("+", T("Destinations.AddSource"), bottom);
	addSourceButton->setPopupMode(QToolButton::InstantPopup);
	auto *sourceMenu = new QMenu(addSourceButton);
	addSourceButton->setMenu(sourceMenu);
	connect(sourceMenu, &QMenu::aboutToShow, this, &StudioWindow::FillAddSourceMenu);
	auto *removeSource = SmallButton("−", T("Destinations.RemoveFromScene"), bottom);
	auto *upSource = SmallButton("↑", T("Destinations.MoveUp"), bottom);
	auto *downSource = SmallButton("↓", T("Destinations.MoveDown"), bottom);
	auto *propSource = SmallButton("⚙", T("Destinations.Properties"), bottom);
	auto *filterSource = SmallButton("FX", T("Destinations.Filters"), bottom);
	connect(removeSource, &QToolButton::clicked, this, &StudioWindow::RemoveSelectedSource);
	connect(upSource, &QToolButton::clicked, this, [this]() { MoveSelectedSource(OBS_ORDER_MOVE_UP); });
	connect(downSource, &QToolButton::clicked, this, [this]() { MoveSelectedSource(OBS_ORDER_MOVE_DOWN); });
	connect(propSource, &QToolButton::clicked, this, [this]() { OpenSelectedSourceProperties(false); });
	connect(filterSource, &QToolButton::clicked, this, [this]() { OpenSelectedSourceProperties(true); });
	sourceButtons->addWidget(addSourceButton);
	sourceButtons->addWidget(removeSource);
	sourceButtons->addWidget(upSource);
	sourceButtons->addWidget(downSource);
	sourceButtons->addWidget(propSource);
	sourceButtons->addWidget(filterSource);
	bottom->addWidget(Panel(T("Destinations.Sources"), &sourcesTitle, sourceList, sourceButtons, bottom));

	destList = new QListWidget(bottom);
	connect(destList, &QListWidget::itemDoubleClicked, this, [this]() { EditDestination(SelectedDestination()); });
	auto *destButtons = new QHBoxLayout();
	auto *addDest = SmallButton("+", T("Destinations.AddDestination"), bottom);
	auto *removeDest = SmallButton("−", T("Destinations.RemoveDestination"), bottom);
	auto *editDest = SmallButton("⚙", T("Destinations.DestinationSettings"), bottom);
	projectorButton = SmallButton(T("Destinations.Projector"), T("Destinations.Projector"), bottom);
	projectorButton->setPopupMode(QToolButton::InstantPopup);
	auto *projectorMenu = new QMenu(projectorButton);
	projectorButton->setMenu(projectorMenu);
	connect(projectorMenu, &QMenu::aboutToShow, this, &StudioWindow::FillProjectorMenu);
	connect(addDest, &QToolButton::clicked, this, &StudioWindow::AddDestination);
	connect(removeDest, &QToolButton::clicked, this, [this]() { RemoveDestination(SelectedDestination()); });
	connect(editDest, &QToolButton::clicked, this, [this]() { EditDestination(SelectedDestination()); });
	destButtons->addWidget(addDest);
	destButtons->addWidget(removeDest);
	destButtons->addWidget(editDest);
	destButtons->addWidget(projectorButton);
	bottom->addWidget(Panel(T("Destinations.DestinationsTitle"), nullptr, destList, destButtons, bottom));

	vsplit->addWidget(bottom);
	vsplit->setStretchFactor(0, 4);
	vsplit->setStretchFactor(1, 1);
	root->addWidget(vsplit, 1);
}

/* ------------------------------------------------------------------ */
/* Ouverture / fermeture                                                */
/* ------------------------------------------------------------------ */

void StudioWindow::Toggle()
{
	if (isVisible())
		Close();
	else
		Open();
}

void StudioWindow::Open()
{
	Manager::Get().Sync();
	RefreshDestinations();
	RefreshScenes();

	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (main && main->screen())
		setGeometry(main->screen()->availableGeometry());
	showMaximized();
	raise();
	activateWindow();

	UpdateGrids(true);
}

void StudioWindow::Close()
{
	pvwGrid->Clear();
	pgmGrid->Clear();
	UpdateShowing({});
	hide();
}

void StudioWindow::closeEvent(QCloseEvent *event)
{
	event->ignore();
	Close();
}

void StudioWindow::Shutdown()
{
	for (auto &p : projectors) {
		if (p.second)
			p.second->close();
	}
	projectors.clear();
	if (pvwGrid)
		pvwGrid->Clear();
	if (pgmGrid)
		pgmGrid->Clear();
	UpdateShowing({});
}

void StudioWindow::OnReset()
{
	for (auto &p : projectors) {
		if (p.second && p.first && !p.first->IsMain())
			p.second->close();
	}
	pvwGrid->Clear();
	pgmGrid->Clear();
	UpdateShowing({});
	activeDest = nullptr;
	const QSignalBlocker b1(sceneList);
	const QSignalBlocker b2(sourceList);
	sceneList->clear();
	sourceList->clear();
	destList->clear();
}

/* ------------------------------------------------------------------ */
/* Grilles                                                              */
/* ------------------------------------------------------------------ */

void StudioWindow::UpdateShowing(const std::vector<OBSSource> &next)
{
	/* Les scènes en Preview doivent être « showing » pour que leurs
	 * sources se rendent même hors antenne. */
	for (auto &s : next)
		obs_source_inc_showing(s);
	for (auto &s : showing)
		obs_source_dec_showing(s);
	showing = next;
}

void StudioWindow::UpdateGrids(bool force)
{
	if (!isVisible()) {
		RefreshScenes();
		return;
	}

	Manager &m = Manager::Get();
	OBSSource preview = m.PreviewScene();

	std::vector<Destination *> covered;
	std::vector<OBSSource> linked;
	if (preview) {
		for (auto *d : m.Destinations()) {
			OBSSource l = d->LinkedScene(preview);
			if (l) {
				covered.push_back(d);
				linked.push_back(l);
			}
		}
	}

	/* Rien à reconstruire si les mêmes destinations affichent les mêmes
	 * scènes (évite de recréer les affichages à chaque événement). */
	bool same = !force && pvwGrid->Tiles().size() == covered.size();
	for (size_t i = 0; same && i < covered.size(); i++) {
		TileWidget *t = pvwGrid->Tiles()[i];
		same = t->Dest() == covered[i] && t->LinkedScene().Get() == linked[i].Get();
	}
	if (same) {
		RefreshScenes();
		return;
	}

	pvwGrid->SetDestinations(covered);
	pgmGrid->SetDestinations(covered);
	for (size_t i = 0; i < covered.size(); i++) {
		TileWidget *pvw = pvwGrid->Tiles()[i];
		pvw->SetLinkedScene(linked[i]);
		ConnectTile(pvw);
		ConnectTile(pgmGrid->Tiles()[i]);
	}
	UpdateShowing(linked);

	bool activeStillHere = false;
	for (auto *d : covered)
		activeStillHere |= d == activeDest;
	SetActiveDestination(activeStillHere ? activeDest : (covered.empty() ? nullptr : covered.front()));
	RefreshSources();

	RefreshScenes();
	RefreshTally();
}

void StudioWindow::ConnectTile(TileWidget *tile)
{
	connect(tile, &TileWidget::activated, this, [this](TileWidget *t) { SetActiveDestination(t->Dest()); });
	connect(tile, &TileWidget::selectionChanged, this, [this](TileWidget *t, qint64 id) {
		if (t->Dest() != activeDest || t->IsProgram())
			return;
		const QSignalBlocker blocker(sourceList);
		sourceList->clearSelection();
		for (int i = 0; i < sourceList->count(); i++) {
			if (sourceList->item(i)->data(Qt::UserRole).toLongLong() == id)
				sourceList->setCurrentRow(i);
		}
	});
	connect(tile, &TileWidget::itemsChanged, this, [this](TileWidget *t) {
		if (t->Dest() == activeDest)
			RefreshSources();
	});
	connect(tile, &TileWidget::takeRequested, this,
		[](TileWidget *t) { Manager::Get().TakeOne(t->Dest(), false); });
	connect(tile, &TileWidget::lockChanged, this, [this](TileWidget *) { RefreshDestinations(); });
}

void StudioWindow::SetActiveDestination(Destination *dest)
{
	const bool changed = dest != activeDest;
	activeDest = dest;
	for (auto *t : pvwGrid->Tiles())
		t->SetActive(t->Dest() == dest);
	for (auto *t : pgmGrid->Tiles())
		t->SetActive(t->Dest() == dest);
	if (changed || sourceList->count() == 0)
		RefreshSources();
}

TileWidget *StudioWindow::ActivePreviewTile() const
{
	return activeDest ? pvwGrid->TileFor(activeDest) : nullptr;
}

void StudioWindow::RefreshTally()
{
	Manager &m = Manager::Get();
	for (auto *t : pgmGrid->Tiles())
		t->RefreshHeader();
	for (auto *t : pvwGrid->Tiles()) {
		OBSSource linked = t->LinkedScene();
		const char *n = linked ? obs_source_get_name(linked) : nullptr;
		t->SetTally(n && t->Dest()->ProgramSceneName() == n);
	}

	OBSSource preview = m.PreviewScene();
	OBSSource program = m.ProgramScene();
	for (int i = 0; i < sceneList->count(); i++) {
		QListWidgetItem *row = sceneList->item(i);
		const std::string uuid = row->data(Qt::UserRole).toString().toStdString();
		const bool isPgm = program && uuid == obs_source_get_uuid(program);
		const bool isPvw = preview && uuid == obs_source_get_uuid(preview);
		QFont font = row->font();
		font.setBold(isPgm || isPvw);
		row->setFont(font);
		row->setForeground(isPgm ? PROGRAM_COLOR : isPvw ? PREVIEW_COLOR : palette().color(QPalette::Text));
	}
}

/* ------------------------------------------------------------------ */
/* Scènes                                                               */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshScenes()
{
	Manager &m = Manager::Get();
	OBSSource preview = m.PreviewScene();
	auto dests = m.Destinations();

	const QSignalBlocker blocker(sceneList);
	sceneList->clear();
	for (auto &scene : Manager::MainScenes()) {
		auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(scene)), sceneList);
		row->setData(Qt::UserRole, QString::fromUtf8(obs_source_get_uuid(scene)));

		QStringList names;
		for (auto *d : dests) {
			if (d->Covers(scene))
				names << QString::fromStdString(d->Name());
		}
		row->setToolTip(T("Destinations.CoveredBy").arg(names.join(", ")));
		if (scene.Get() == preview.Get())
			sceneList->setCurrentItem(row);
	}
	RefreshTally();
}

OBSSource StudioWindow::SelectedScene() const
{
	QListWidgetItem *row = sceneList->currentItem();
	if (!row)
		return nullptr;
	OBSSourceAutoRelease s = obs_get_source_by_uuid(row->data(Qt::UserRole).toString().toUtf8().constData());
	return OBSSource(s.Get());
}

void StudioWindow::AddScene()
{
	std::string name = QString("%1 %2").arg(T("Destinations.Scene")).arg(sceneList->count() + 1).toStdString();
	std::set<Destination *> coverage;
	while (true) {
		if (!Dialogs::EditScene(this, nullptr, name, coverage, true))
			return;
		OBSSource scene = Manager::Get().CreateScene(name, coverage);
		if (scene) {
			Manager::Get().SetPreviewScene(scene);
			return;
		}
		QMessageBox::warning(this, T("Destinations.AddScene"), T("Destinations.SceneExists"));
	}
}

void StudioWindow::AskCoverage(obs_source_t *scene)
{
	if (!scene)
		return;
	const char *n = obs_source_get_name(scene);
	std::string name = n ? n : "";
	std::set<Destination *> coverage;
	QWidget *parent = isVisible() ? this : static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!Dialogs::EditScene(parent, scene, name, coverage, true))
		return;
	Manager::Get().SetCoverage(scene, coverage);
	if (n && name != n)
		Manager::Get().RenameScene(scene, name);
}

void StudioWindow::EditScene(obs_source_t *scene)
{
	if (!scene)
		return;
	const std::string oldName = obs_source_get_name(scene);
	std::string name = oldName;
	std::set<Destination *> coverage;
	if (!Dialogs::EditScene(this, scene, name, coverage, false))
		return;

	/* Retirer une destination supprime sa version de la scène. */
	std::vector<Destination *> dropped;
	for (auto *d : Manager::Get().Destinations()) {
		if (!d->IsMain() && d->Covers(scene) && !coverage.count(d))
			dropped.push_back(d);
	}
	if (!dropped.empty() && QMessageBox::question(this, T("Destinations.SceneProperties"),
						      T("Destinations.ConfirmUncover")) != QMessageBox::Yes)
		return;

	Manager::Get().SetCoverage(scene, coverage);
	if (name != oldName)
		Manager::Get().RenameScene(scene, name);
}

void StudioWindow::RemoveScene(obs_source_t *scene)
{
	if (!scene)
		return;
	const QString name = QString::fromUtf8(obs_source_get_name(scene));
	if (QMessageBox::question(this, T("Destinations.RemoveScene"), T("Destinations.ConfirmRemove").arg(name)) !=
	    QMessageBox::Yes)
		return;
	Manager::Get().RemoveScene(scene);
}

/* ------------------------------------------------------------------ */
/* Sources de la destination active                                    */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshSources()
{
	refreshingSources = true;
	sourceList->clear();

	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;

	sourcesTitle->setText(activeDest ? T("Destinations.SourcesOf").arg(QString::fromStdString(activeDest->Name()))
					 : T("Destinations.Sources"));
	addSourceButton->setEnabled(s != nullptr);

	if (s) {
		const int64_t selected = tile->SelectedItem();
		struct Ctx {
			QListWidget *list;
			int64_t selected;
		} ctx{sourceList, selected};
		obs_scene_enum_items(
			s,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *c = static_cast<Ctx *>(param);
				obs_source_t *src = obs_sceneitem_get_source(item);
				auto *row = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(src)));
				row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
				row->setCheckState(obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
				row->setData(Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
				/* Haut de la pile en premier, comme OBS. */
				c->list->insertItem(0, row);
				if (obs_sceneitem_get_id(item) == c->selected)
					c->list->setCurrentItem(row);
				return true;
			},
			&ctx);
	}
	refreshingSources = false;
}

void StudioWindow::FillAddSourceMenu()
{
	QMenu *menu = addSourceButton->menu();
	menu->clear();

	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	if (!scene) {
		menu->addAction(T("Destinations.SelectSceneFirst"))->setEnabled(false);
		return;
	}
	OBSWeakSource weakScene = OBSGetWeakRef(scene.Get());
	Destination *dest = tile->Dest();

	auto addEntry = [this, weakScene, dest](QMenu *target, obs_source_t *source) {
		OBSWeakSource weak = OBSGetWeakRef(source);
		QAction *action = target->addAction(QString::fromUtf8(obs_source_get_name(source)));
		connect(action, &QAction::triggered, this, [this, weak, weakScene, dest]() {
			OBSSource src = OBSGetStrongRef(weak);
			OBSSource sc = OBSGetStrongRef(weakScene);
			obs_scene_t *s = sc ? obs_scene_from_source(sc) : nullptr;
			if (!src || !s)
				return;
			obs_sceneitem_t *item = obs_scene_add(s, src);
			if (!item)
				return;
			obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
			SceneEditor::FitToCanvas(item, dest->Width(), dest->Height());
			if (TileWidget *t = ActivePreviewTile())
				t->SetSelectedItem(obs_sceneitem_get_id(item));
			RefreshSources();
		});
	};

	QMenu *inputs = menu->addMenu(T("Destinations.Inputs"));
	std::vector<OBSSource> list;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *l = static_cast<std::vector<OBSSource> *>(param);
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				l->emplace_back(source);
			return true;
		},
		&list);
	for (auto &s : list)
		addEntry(inputs, s);
	if (list.empty())
		inputs->addAction(T("Destinations.None"))->setEnabled(false);

	QMenu *scenes = menu->addMenu(T("Destinations.MainScenes"));
	OBSSource preview = Manager::Get().PreviewScene();
	int added = 0;
	for (auto &s : Manager::MainScenes()) {
		/* Une scène ne peut pas s'inclure elle-même. */
		if (s.Get() == scene.Get() || s.Get() == preview.Get())
			continue;
		addEntry(scenes, s);
		added++;
	}
	if (added == 0)
		scenes->addAction(T("Destinations.None"))->setEnabled(false);

	menu->addSeparator();
	menu->addAction(T("Destinations.NewSourceHint"))->setEnabled(false);
}

static OBSSceneItem SelectedItemIn(TileWidget *tile, QListWidget *list)
{
	QListWidgetItem *row = list->currentItem();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!row || !s)
		return nullptr;
	return obs_scene_find_sceneitem_by_id(s, row->data(Qt::UserRole).toLongLong());
}

void StudioWindow::RemoveSelectedSource()
{
	TileWidget *tile = ActivePreviewTile();
	OBSSceneItem item = SelectedItemIn(tile, sourceList);
	if (!item)
		return;
	obs_sceneitem_remove(item);
	tile->SetSelectedItem(-1);
	RefreshSources();
}

void StudioWindow::MoveSelectedSource(enum obs_order_movement movement)
{
	OBSSceneItem item = SelectedItemIn(ActivePreviewTile(), sourceList);
	if (!item)
		return;
	obs_sceneitem_set_order(item, movement);
	RefreshSources();
}

void StudioWindow::OpenSelectedSourceProperties(bool filters)
{
	OBSSceneItem item = SelectedItemIn(ActivePreviewTile(), sourceList);
	if (!item)
		return;
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (filters)
		obs_frontend_open_source_filters(src);
	else
		obs_frontend_open_source_properties(src);
}

/* ------------------------------------------------------------------ */
/* Destinations                                                         */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshDestinations()
{
	Destination *selected = SelectedDestination();
	destList->clear();
	for (auto *d : Manager::Get().Destinations()) {
		QString text =
			QString("%1  ·  %2×%3").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height());
		if (d->Locked())
			text += "  —  " + T("Destinations.Locked");
		auto *row = new QListWidgetItem(text, destList);
		row->setData(Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(d)));
		if (d == selected)
			destList->setCurrentItem(row);
	}
}

Destination *StudioWindow::SelectedDestination() const
{
	QListWidgetItem *row = destList->currentItem();
	if (!row)
		return nullptr;
	auto *ptr = reinterpret_cast<Destination *>(row->data(Qt::UserRole).value<quintptr>());
	for (auto *d : Manager::Get().Destinations()) {
		if (d == ptr)
			return d;
	}
	return nullptr;
}

void StudioWindow::AddDestination()
{
	Dialogs::DestinationSettings settings;
	settings.name = QString("%1 %2").arg(T("Destinations.Destination")).arg(destList->count()).toStdString();
	if (!Dialogs::EditDestination(this, nullptr, settings))
		return;

	Destination *dest = Manager::Get().Add(settings.name, settings.width, settings.height, settings.coverAll);
	if (!dest) {
		QMessageBox::warning(this, T("Destinations.AddDestination"), T("Destinations.AddFailed"));
		return;
	}
	dest->SetTransitionType(settings.transition);
	dest->SetTransitionDuration(settings.duration);
}

void StudioWindow::EditDestination(Destination *dest)
{
	if (!dest)
		return;
	Dialogs::DestinationSettings settings;
	settings.name = dest->Name();
	settings.width = dest->Width();
	settings.height = dest->Height();
	settings.transition = dest->TransitionType();
	settings.duration = dest->TransitionDuration();
	if (!Dialogs::EditDestination(this, dest, settings))
		return;

	dest->SetName(settings.name);
	dest->Attach(settings.width, settings.height);
	dest->SetTransitionType(settings.transition);
	dest->SetTransitionDuration(settings.duration);
	Manager::Get().NotifyDestinationsChanged();
}

void StudioWindow::RemoveDestination(Destination *dest)
{
	if (!dest)
		return;
	if (dest->IsMain()) {
		QMessageBox::information(this, T("Destinations.RemoveDestination"), T("Destinations.CannotRemoveMain"));
		return;
	}
	const QString name = QString::fromStdString(dest->Name());
	if (QMessageBox::question(this, T("Destinations.RemoveDestination"),
				  T("Destinations.ConfirmRemoveDestination").arg(name)) != QMessageBox::Yes)
		return;
	Manager::Get().Remove(dest);
}

void StudioWindow::FillProjectorMenu()
{
	QMenu *menu = projectorButton->menu();
	menu->clear();

	Destination *dest = SelectedDestination();
	if (!dest) {
		menu->addAction(T("Destinations.SelectDestinationFirst"))->setEnabled(false);
		return;
	}

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
		connect(menu->addAction(label), &QAction::triggered, this,
			[this, dest, i]() { OpenProjector(dest, i); });
	}
	menu->addSeparator();
	connect(menu->addAction(T("Destinations.Windowed")), &QAction::triggered, this,
		[this, dest]() { OpenProjector(dest, -1); });
	menu->addSeparator();
	connect(menu->addAction(T("Destinations.CloseProjectors")), &QAction::triggered, this,
		[this, dest]() { CloseProjectors(dest); });
}

void StudioWindow::OpenProjector(Destination *dest, int screenIndex)
{
	const QList<QScreen *> screens = QGuiApplication::screens();
	QScreen *screen = (screenIndex >= 0 && screenIndex < screens.size()) ? screens[screenIndex] : nullptr;

	/* Un seul projecteur par écran. */
	for (auto &p : projectors) {
		if (p.second && screen && p.second->TargetScreen() == screen)
			p.second->close();
	}

	auto *projector = new ProjectorWindow(dest, screen);
	projectors.emplace_back(dest, projector);
	projector->Start();
}

void StudioWindow::CloseProjectors(Destination *dest)
{
	for (auto &p : projectors) {
		if (p.second && p.first == dest)
			p.second->close();
	}
}
