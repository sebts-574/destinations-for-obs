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
#include "audio-mixer.hpp"
#include "destination.hpp"
#include "dialogs.hpp"
#include "manager.hpp"
#include "projector-window.hpp"
#include "text.hpp"
#include "tile-grid.hpp"

#include <obs-frontend-api.h>

#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>

/* ------------------------------------------------------------------ */
/* Petits utilitaires d'interface                                       */
/* ------------------------------------------------------------------ */

static QToolButton *ToolButton(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setAutoRaise(true);
	b->setMinimumWidth(26);
	return b;
}

/* Contenu d'un dock : liste + barre de boutons en bas, comme dans OBS. */
static QWidget *ListPanel(QListWidget *list, QHBoxLayout *buttons, QWidget *parent)
{
	auto *panel = new QWidget(parent);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(list, 1);
	buttons->setContentsMargins(2, 2, 2, 2);
	buttons->setSpacing(2);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	return panel;
}

static QDockWidget *Dock(const QString &title, const char *objectName, QWidget *content, QWidget *parent)
{
	auto *dock = new QDockWidget(title, parent);
	dock->setObjectName(objectName);
	dock->setWidget(content);
	dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
			  QDockWidget::DockWidgetClosable);
	return dock;
}

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

StudioWindow::StudioWindow(QWidget *parent) : QMainWindow(parent, Qt::Window)
{
	setWindowTitle("Multidestination Studio");
	setDockNestingEnabled(true);

	BuildCentral();
	BuildDocks();
	BuildMenus();

	Manager &m = Manager::Get();
	connect(&m, &Manager::previewChanged, this, &StudioWindow::RebuildGrids);
	connect(&m, &Manager::scenesChanged, this, [this]() {
		RefreshScenes();
		RebuildGrids();
	});
	connect(&m, &Manager::destinationsChanged, this, [this]() { UpdateGrids(true); });
	connect(&m, &Manager::programChanged, this, &StudioWindow::RefreshTally);
	connect(&m, &Manager::aboutToReset, this, &StudioWindow::OnReset);
	connect(&m, &Manager::loaded, this, [this]() {
		RefreshScenes();
		RefreshTransitions();
		UpdateGrids(true);
	});
	connect(&m, &Manager::aboutToRemove, this, [this](Destination *dest) {
		CloseProjectors(dest);
		if (activeDest == dest)
			activeDest = nullptr;
		pvwGrid->Clear();
		pgmGrid->Clear();
	});

	/* Changements faits ailleurs (OBS, transitions en cours, stream). */
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!isVisible())
			return;
		RefreshTally();
		UpdateControls();
	});
	timer->start(500);
}

StudioWindow::~StudioWindow()
{
	Shutdown();
}

void StudioWindow::BuildCentral()
{
	monitors = new QWidget(this);
	auto *layout = new QHBoxLayout(monitors);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(6);

	auto makeColumn = [this](const QString &label, TileGrid *grid) {
		auto *col = new QWidget(monitors);
		auto *l = new QVBoxLayout(col);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(2);
		grid->setParent(col);
		l->addWidget(grid, 1);
		auto *t = new QLabel(label, col);
		t->setAlignment(Qt::AlignCenter);
		l->addWidget(t);
		return col;
	};

	pvwGrid = new TileGrid(false, monitors);
	pgmGrid = new TileGrid(true, monitors);
	layout->addWidget(makeColumn(Tr("MDS.Preview"), pvwGrid), 1);

	/* Boutons de transition entre Preview et Program, comme le mode
	 * Studio d'OBS. */
	auto *controls = new QVBoxLayout();
	controls->addStretch(1);
	auto *transition = new QPushButton(Tr("MDS.Transition"), monitors);
	transition->setToolTip(Tr("MDS.TransitionTip"));
	connect(transition, &QPushButton::clicked, this, []() { Manager::Get().Take(false); });
	controls->addWidget(transition);
	auto *cut = new QPushButton(Tr("MDS.Cut"), monitors);
	cut->setToolTip(Tr("MDS.CutTip"));
	connect(cut, &QPushButton::clicked, this, []() { Manager::Get().Take(true); });
	controls->addWidget(cut);
	controls->addStretch(1);
	layout->addLayout(controls);

	layout->addWidget(makeColumn(Tr("MDS.Program"), pgmGrid), 1);
	setCentralWidget(monitors);
}

void StudioWindow::BuildDocks()
{
	/* --- Scènes --- */
	sceneList = new QListWidget(this);
	connect(sceneList, &QListWidget::itemSelectionChanged, this, [this]() {
		OBSSource scene = SelectedScene();
		if (scene)
			Manager::Get().SetPreviewScene(scene);
	});
	sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sceneList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		OBSSource scene = SelectedScene();
		QMenu menu(this);
		connect(menu.addAction(Tr("MDS.AddScene")), &QAction::triggered, this, &StudioWindow::AddScene);
		if (scene) {
			connect(menu.addAction(Tr("MDS.SceneSettingsMenu")), &QAction::triggered, this,
				[this, scene]() { EditScene(scene); });
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.RemoveScene")), &QAction::triggered, this,
				[this, scene]() { RemoveScene(scene); });
		}
		menu.exec(sceneList->viewport()->mapToGlobal(pos));
	});
	connect(sceneList, &QListWidget::itemDoubleClicked, this, [this]() { EditScene(SelectedScene()); });
	auto *sceneButtons = new QHBoxLayout();
	auto *addScene = ToolButton("+", Tr("MDS.AddScene"), this);
	auto *removeScene = ToolButton("−", Tr("MDS.RemoveScene"), this);
	auto *editScene = ToolButton("⚙", Tr("MDS.SceneSettingsMenu"), this);
	connect(addScene, &QToolButton::clicked, this, &StudioWindow::AddScene);
	connect(removeScene, &QToolButton::clicked, this, [this]() { RemoveScene(SelectedScene()); });
	connect(editScene, &QToolButton::clicked, this, [this]() { EditScene(SelectedScene()); });
	sceneButtons->addWidget(addScene);
	sceneButtons->addWidget(removeScene);
	sceneButtons->addWidget(editScene);
	auto *scenesDock = Dock(Tr("MDS.Scenes"), "mdsScenesDock", ListPanel(sceneList, sceneButtons, this), this);

	/* --- Sources --- */
	sourceList = new QListWidget(this);
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
	connect(sourceList, &QListWidget::itemDoubleClicked, this, &StudioWindow::EditProperties);
	sourceList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sourceList, &QListWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) { ShowItemMenu(sourceList->viewport()->mapToGlobal(pos)); });

	auto *sourceButtons = new QHBoxLayout();
	addSourceButton = ToolButton("+", Tr("MDS.AddSource"), this);
	addSourceButton->setPopupMode(QToolButton::InstantPopup);
	auto *sourceMenu = new QMenu(addSourceButton);
	addSourceButton->setMenu(sourceMenu);
	connect(sourceMenu, &QMenu::aboutToShow, this, [this, sourceMenu]() { FillAddSourceMenu(sourceMenu); });
	auto *removeSource = ToolButton("−", Tr("MDS.RemoveSource"), this);
	auto *propSource = ToolButton("⚙", Tr("MDS.Properties"), this);
	auto *upSource = ToolButton("↑", Tr("MDS.MoveUp"), this);
	auto *downSource = ToolButton("↓", Tr("MDS.MoveDown"), this);
	connect(removeSource, &QToolButton::clicked, this, &StudioWindow::EditRemove);
	connect(propSource, &QToolButton::clicked, this, &StudioWindow::EditProperties);
	connect(upSource, &QToolButton::clicked, this, &StudioWindow::EditMoveUp);
	connect(downSource, &QToolButton::clicked, this, &StudioWindow::EditMoveDown);
	sourceButtons->addWidget(addSourceButton);
	sourceButtons->addWidget(removeSource);
	sourceButtons->addWidget(propSource);
	sourceButtons->addWidget(upSource);
	sourceButtons->addWidget(downSource);
	auto *sourcesDock = Dock(Tr("MDS.Sources"), "mdsSourcesDock", ListPanel(sourceList, sourceButtons, this), this);

	/* --- Mixeur audio --- */
	mixer = new AudioMixer(this);
	auto *mixerDock = Dock(Tr("MDS.AudioMixer"), "mdsMixerDock", mixer, this);

	/* --- Transitions de scène --- */
	auto *transitions = new QWidget(this);
	auto *tl = new QVBoxLayout(transitions);
	tl->setContentsMargins(4, 4, 4, 4);
	auto *row = new QHBoxLayout();
	transitionCombo = new QComboBox(transitions);
	connect(transitionCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (refreshingTransitions || index < 0)
			return;
		/* Les transitions d'OBS sont privées : on les retrouve dans la
		 * liste du frontend. */
		struct obs_frontend_source_list list = {};
		obs_frontend_get_transitions(&list);
		const QString wanted = transitionCombo->itemData(index).toString();
		for (size_t i = 0; i < list.sources.num; i++) {
			if (wanted == QString::fromUtf8(obs_source_get_uuid(list.sources.array[i])))
				obs_frontend_set_current_transition(list.sources.array[i]);
		}
		obs_frontend_source_list_free(&list);
		RefreshTransitions();
	});
	row->addWidget(transitionCombo, 1);
	auto *transitionProps = ToolButton("⚙", Tr("MDS.TransitionProperties"), transitions);
	connect(transitionProps, &QToolButton::clicked, this, []() {
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		if (current)
			obs_frontend_open_source_properties(current);
	});
	row->addWidget(transitionProps);
	tl->addLayout(row);
	auto *durationRow = new QHBoxLayout();
	durationRow->addWidget(new QLabel(Tr("MDS.Duration"), transitions));
	durationSpin = new QSpinBox(transitions);
	durationSpin->setRange(50, 20000);
	durationSpin->setSingleStep(50);
	durationSpin->setSuffix(" ms");
	connect(durationSpin, &QSpinBox::valueChanged, this, [this](int value) {
		if (!refreshingTransitions)
			obs_frontend_set_transition_duration(value);
	});
	durationRow->addWidget(durationSpin, 1);
	tl->addLayout(durationRow);
	auto *transitionNote = new QLabel(Tr("MDS.TransitionNote"), transitions);
	transitionNote->setWordWrap(true);
	tl->addWidget(transitionNote);
	tl->addStretch(1);
	auto *transitionsDock = Dock(Tr("MDS.SceneTransitions"), "mdsTransitionsDock", transitions, this);

	/* --- Commandes --- */
	auto *controls = new QWidget(this);
	auto *cl = new QVBoxLayout(controls);
	cl->setContentsMargins(4, 4, 4, 4);
	streamButton = new QPushButton(controls);
	connect(streamButton, &QPushButton::clicked, this, [this]() {
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
		QTimer::singleShot(300, this, &StudioWindow::UpdateControls);
	});
	cl->addWidget(streamButton);
	recordButton = new QPushButton(controls);
	connect(recordButton, &QPushButton::clicked, this, [this]() {
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		else
			obs_frontend_recording_start();
		QTimer::singleShot(300, this, &StudioWindow::UpdateControls);
	});
	cl->addWidget(recordButton);
	auto *exit = new QPushButton(Tr("MDS.ExitStudio"), controls);
	connect(exit, &QPushButton::clicked, this, &StudioWindow::Close);
	cl->addWidget(exit);
	cl->addStretch(1);
	auto *controlsDock = Dock(Tr("MDS.Controls"), "mdsControlsDock", controls, this);
	UpdateControls();

	/* Disposition par défaut d'OBS : docks côte à côte en bas. */
	addDockWidget(Qt::BottomDockWidgetArea, scenesDock);
	splitDockWidget(scenesDock, sourcesDock, Qt::Horizontal);
	splitDockWidget(sourcesDock, mixerDock, Qt::Horizontal);
	splitDockWidget(mixerDock, transitionsDock, Qt::Horizontal);
	splitDockWidget(transitionsDock, controlsDock, Qt::Horizontal);
	resizeDocks({scenesDock, sourcesDock, mixerDock, transitionsDock, controlsDock}, {200, 220, 320, 200, 180},
		    Qt::Horizontal);
	resizeDocks({scenesDock}, {240}, Qt::Vertical);
}

QAction *StudioWindow::MakeEditAction(const QString &text, const QList<QKeySequence> &keys, void (StudioWindow::*fn)())
{
	auto *action = new QAction(text, this);
	action->setShortcuts(keys);
	/* Actifs quand le focus est dans les aperçus ou la liste des sources,
	 * pour ne pas gêner la saisie dans les autres champs. */
	action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(action, &QAction::triggered, this, fn);
	monitors->addAction(action);
	sourceList->addAction(action);
	editActions.push_back(action);
	return action;
}

void StudioWindow::BuildMenus()
{
	QMenuBar *bar = menuBar();

	/* --- Fichier --- */
	QMenu *file = bar->addMenu(Tr("MDS.MenuFile"));
	connect(file->addAction(Tr("MDS.ExitStudio")), &QAction::triggered, this, &StudioWindow::Close);

	/* --- Édition --- */
	QMenu *edit = bar->addMenu(Tr("MDS.MenuEdit"));
	edit->addAction(MakeEditAction(Tr("MDS.Copy"), {QKeySequence::Copy}, &StudioWindow::EditCopy));
	edit->addAction(MakeEditAction(Tr("MDS.Paste"), {QKeySequence::Paste}, &StudioWindow::EditPaste));
	edit->addAction(MakeEditAction(Tr("MDS.RemoveSource"),
				       {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)},
				       &StudioWindow::EditRemove));
	edit->addSeparator();
	QMenu *transform = edit->addMenu(Tr("MDS.Transform"));
	transform->addAction(MakeEditAction(Tr("MDS.CopyTransform"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
					    &StudioWindow::EditCopyTransform));
	transform->addAction(MakeEditAction(Tr("MDS.PasteTransform"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V)},
					    &StudioWindow::EditPasteTransform));
	transform->addSeparator();
	transform->addAction(MakeEditAction(Tr("MDS.ResetTransform"), {QKeySequence(Qt::CTRL | Qt::Key_R)},
					    &StudioWindow::EditResetTransform));
	transform->addAction(
		MakeEditAction(Tr("MDS.FitToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_F)}, &StudioWindow::EditFit));
	transform->addAction(MakeEditAction(Tr("MDS.StretchToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_S)},
					    &StudioWindow::EditStretch));
	transform->addAction(MakeEditAction(Tr("MDS.CenterToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_D)},
					    &StudioWindow::EditCenter));
	QMenu *order = edit->addMenu(Tr("MDS.Order"));
	order->addAction(
		MakeEditAction(Tr("MDS.MoveUp"), {QKeySequence(Qt::CTRL | Qt::Key_Up)}, &StudioWindow::EditMoveUp));
	order->addAction(MakeEditAction(Tr("MDS.MoveDown"), {QKeySequence(Qt::CTRL | Qt::Key_Down)},
					&StudioWindow::EditMoveDown));
	order->addAction(
		MakeEditAction(Tr("MDS.MoveTop"), {QKeySequence(Qt::CTRL | Qt::Key_Home)}, &StudioWindow::EditMoveTop));
	order->addAction(MakeEditAction(Tr("MDS.MoveBottom"), {QKeySequence(Qt::CTRL | Qt::Key_End)},
					&StudioWindow::EditMoveBottom));
	edit->addSeparator();
	edit->addAction(MakeEditAction(Tr("MDS.Properties"), {}, &StudioWindow::EditProperties));
	edit->addAction(MakeEditAction(Tr("MDS.Filters"), {}, &StudioWindow::EditFilters));

	/* --- Destinations --- */
	destinationsMenu = bar->addMenu(Tr("MDS.MenuDestinations"));
	connect(destinationsMenu, &QMenu::aboutToShow, this, &StudioWindow::FillDestinationsMenu);

	/* --- Affichage : afficher / masquer les docks --- */
	viewMenu = bar->addMenu(Tr("MDS.MenuView"));
	for (QDockWidget *dock : findChildren<QDockWidget *>())
		viewMenu->addAction(dock->toggleViewAction());

	auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(escape, &QShortcut::activated, this, &StudioWindow::Close);
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
	RefreshScenes();
	RefreshTransitions();
	mixer->Refresh();

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
	mixer->Clear();
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
	if (mixer)
		mixer->Clear();
	UpdateShowing({});
}

void StudioWindow::OnReset()
{
	for (auto &p : projectors) {
		if (p.second)
			p.second->close();
	}
	projectors.clear();
	pvwGrid->Clear();
	pgmGrid->Clear();
	mixer->Clear();
	UpdateShowing({});
	activeDest = nullptr;
	const QSignalBlocker b1(sceneList);
	const QSignalBlocker b2(sourceList);
	sceneList->clear();
	sourceList->clear();
}

QByteArray StudioWindow::SaveLayout() const
{
	return saveState();
}

void StudioWindow::RestoreLayout(const QByteArray &state)
{
	if (!state.isEmpty())
		restoreState(state);
}

void StudioWindow::UpdateControls()
{
	streamButton->setText(obs_frontend_streaming_active() ? Tr("MDS.StopStreaming") : Tr("MDS.StartStreaming"));
	recordButton->setText(obs_frontend_recording_active() ? Tr("MDS.StopRecording") : Tr("MDS.StartRecording"));
}

/* ------------------------------------------------------------------ */
/* Aperçus                                                              */
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
	connect(tile, &TileWidget::contextMenuRequested, this,
		[this](TileWidget *, const QPoint &pos) { ShowItemMenu(pos); });
}

void StudioWindow::SetActiveDestination(Destination *dest)
{
	const bool changed = dest != activeDest;
	activeDest = dest;
	for (auto *t : pvwGrid->Tiles())
		t->SetActive(t->Dest() == dest);
	for (auto *t : pgmGrid->Tiles())
		t->SetActive(t->Dest() == dest);
	if (changed)
		RefreshSources();
}

TileWidget *StudioWindow::ActivePreviewTile() const
{
	return activeDest ? pvwGrid->TileFor(activeDest) : nullptr;
}

void StudioWindow::RefreshTally()
{
	for (auto *t : pgmGrid->Tiles())
		t->RefreshHeader();

	OBSSource program = Manager::Get().ProgramScene();
	const char *pgmUuid = program ? obs_source_get_uuid(program) : nullptr;
	for (int i = 0; i < sceneList->count(); i++) {
		QListWidgetItem *row = sceneList->item(i);
		QFont font = row->font();
		font.setBold(pgmUuid && row->data(Qt::UserRole).toString() == QString::fromUtf8(pgmUuid));
		row->setFont(font);
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
		row->setToolTip(Tr("MDS.ShownOn").arg(names.isEmpty() ? Tr("MDS.NoDestination") : names.join(", ")));
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
	std::string name = QString("%1 %2").arg(Tr("MDS.Scene")).arg(sceneList->count() + 1).toStdString();
	std::set<Destination *> coverage;
	while (true) {
		if (!Dialogs::EditScene(this, nullptr, name, coverage, true))
			return;
		OBSSource scene = Manager::Get().CreateScene(name, coverage);
		if (scene) {
			Manager::Get().SetPreviewScene(scene);
			return;
		}
		QMessageBox::warning(this, Tr("MDS.NewScene"), Tr("MDS.SceneExists"));
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

	bool dropping = false;
	for (auto *d : Manager::Get().Destinations()) {
		if (!d->IsMain() && d->Covers(scene) && !coverage.count(d))
			dropping = true;
	}
	if (dropping &&
	    QMessageBox::question(this, Tr("MDS.SceneSettings"), Tr("MDS.ConfirmUncover")) != QMessageBox::Yes)
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
	if (QMessageBox::question(this, Tr("MDS.RemoveScene"), Tr("MDS.ConfirmRemoveScene").arg(name)) !=
	    QMessageBox::Yes)
		return;
	Manager::Get().RemoveScene(scene);
}

/* ------------------------------------------------------------------ */
/* Sources                                                              */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshSources()
{
	refreshingSources = true;
	sourceList->clear();

	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;

	if (auto *dock = qobject_cast<QDockWidget *>(sourceList->parentWidget()->parentWidget())) {
		dock->setWindowTitle(activeDest ? Tr("MDS.SourcesOf").arg(QString::fromStdString(activeDest->Name()))
						: Tr("MDS.Sources"));
	}
	addSourceButton->setEnabled(s != nullptr);

	if (s) {
		struct Ctx {
			QListWidget *list;
			int64_t selected;
		} ctx{sourceList, tile->SelectedItem()};
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

OBSSceneItem StudioWindow::CurrentItem() const
{
	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!s)
		return nullptr;
	int64_t id = tile->SelectedItem();
	if (id < 0 && sourceList->currentItem())
		id = sourceList->currentItem()->data(Qt::UserRole).toLongLong();
	return id >= 0 ? OBSSceneItem(obs_scene_find_sceneitem_by_id(s, id)) : OBSSceneItem();
}

void StudioWindow::SelectItem(int64_t id)
{
	if (TileWidget *t = ActivePreviewTile())
		t->SetSelectedItem(id);
	RefreshSources();
}

void StudioWindow::AddExistingSource(obs_source_t *source)
{
	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!source || !s)
		return;

	obs_sceneitem_t *item = obs_scene_add(s, source);
	if (!item)
		return;
	obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
	SceneEditor::FitToCanvas(item, activeDest->Width(), activeDest->Height());

	/* Source dont la taille n'est pas encore connue (réglages en cours) :
	 * on l'ajuste au cadre dès qu'elle a une image. */
	const int64_t id = obs_sceneitem_get_id(item);
	if (obs_source_get_width(source) == 0) {
		OBSWeakSource weakScene = OBSGetWeakRef(scene.Get());
		Destination *dest = activeDest;
		auto *timer = new QTimer(this);
		auto tries = std::make_shared<int>(0);
		connect(timer, &QTimer::timeout, this, [timer, weakScene, dest, id, tries]() {
			OBSSource sc = OBSGetStrongRef(weakScene);
			obs_scene_t *sp = sc ? obs_scene_from_source(sc) : nullptr;
			obs_sceneitem_t *it = sp ? obs_scene_find_sceneitem_by_id(sp, id) : nullptr;
			if (!it || ++(*tries) > 120) {
				timer->deleteLater();
				return;
			}
			if (obs_source_get_width(obs_sceneitem_get_source(it)) > 0) {
				SceneEditor::FitToCanvas(it, dest->Width(), dest->Height());
				timer->deleteLater();
			}
		});
		timer->start(250);
	}
	SelectItem(id);
}

void StudioWindow::CreateNewSource(const char *typeId)
{
	if (!ActivePreviewTile() || !ActivePreviewTile()->LinkedScene())
		return;

	QString base = QString::fromUtf8(obs_source_get_display_name(typeId));
	QString suggestion = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(suggestion.toUtf8().constData());
		if (!existing)
			break;
		suggestion = QString("%1 %2").arg(base).arg(i);
	}

	bool ok = false;
	QString name = QInputDialog::getText(this, Tr("MDS.NewSourceTitle").arg(base), Tr("MDS.Name"),
					     QLineEdit::Normal, suggestion, &ok)
			       .trimmed();
	if (!ok || name.isEmpty())
		return;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
	if (existing) {
		QMessageBox::warning(this, Tr("MDS.AddSource"), Tr("MDS.SourceExists"));
		return;
	}

	OBSSourceAutoRelease source = obs_source_create(typeId, name.toUtf8().constData(), nullptr, nullptr);
	if (!source)
		return;
	AddExistingSource(source);
	obs_frontend_open_source_properties(source);
}

void StudioWindow::FillAddSourceMenu(QMenu *menu)
{
	menu->clear();
	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	if (!scene) {
		menu->addAction(Tr("MDS.SelectSceneFirst"))->setEnabled(false);
		return;
	}

	/* Nouveaux types de sources, comme le « + » d'OBS. */
	std::vector<std::pair<QString, std::string>> types;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &id); i++) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED))
			continue;
		const char *display = obs_source_get_display_name(id);
		if (display)
			types.emplace_back(QString::fromUtf8(display), id);
	}
	std::sort(types.begin(), types.end(),
		  [](const auto &a, const auto &b) { return a.first.localeAwareCompare(b.first) < 0; });
	for (auto &[display, typeId] : types) {
		std::string tid = typeId;
		connect(menu->addAction(display), &QAction::triggered, this,
			[this, tid]() { CreateNewSource(tid.c_str()); });
	}
	menu->addSeparator();

	auto addEntry = [this](QMenu *target, obs_source_t *source) {
		OBSWeakSource weak = OBSGetWeakRef(source);
		connect(target->addAction(QString::fromUtf8(obs_source_get_name(source))), &QAction::triggered, this,
			[this, weak]() {
				OBSSource src = OBSGetStrongRef(weak);
				AddExistingSource(src);
			});
	};

	QMenu *existing = menu->addMenu(Tr("MDS.ExistingSource"));
	std::vector<OBSSource> inputs;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *l = static_cast<std::vector<OBSSource> *>(param);
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				l->emplace_back(source);
			return true;
		},
		&inputs);
	for (auto &s : inputs)
		addEntry(existing, s);
	if (inputs.empty())
		existing->addAction(Tr("MDS.None"))->setEnabled(false);

	QMenu *scenes = menu->addMenu(Tr("MDS.SceneAsSource"));
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
		scenes->addAction(Tr("MDS.None"))->setEnabled(false);
}

void StudioWindow::ShowItemMenu(const QPoint &globalPos)
{
	OBSSceneItem item = CurrentItem();
	QMenu menu(this);

	if (item) {
		QAction *visible = menu.addAction(Tr("MDS.Visible"));
		visible->setCheckable(true);
		visible->setChecked(obs_sceneitem_visible(item));
		obs_sceneitem_t *it = item;
		connect(visible, &QAction::toggled, this, [this, it](bool on) {
			obs_sceneitem_set_visible(it, on);
			RefreshSources();
		});
		menu.addSeparator();
	}
	for (QAction *a : editActions) {
		a->setEnabled(item || a->text() == Tr("MDS.Paste"));
		menu.addAction(a);
	}
	menu.exec(globalPos);
	for (QAction *a : editActions)
		a->setEnabled(true);
}

/* ------------------------------------------------------------------ */
/* Édition                                                              */
/* ------------------------------------------------------------------ */

void StudioWindow::EditCopy()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	clipboardSource = OBSGetWeakRef(obs_sceneitem_get_source(item));
	clipboardTransform = SceneEditor::GetTransform(item);
	hasClipboardTransform = true;
}

void StudioWindow::EditPaste()
{
	OBSSource source = OBSGetStrongRef(clipboardSource);
	TileWidget *tile = ActivePreviewTile();
	OBSSource scene = tile ? tile->LinkedScene() : OBSSource();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!source || !s || source.Get() == scene.Get())
		return;

	obs_sceneitem_t *item = obs_scene_add(s, source);
	if (!item)
		return;
	if (hasClipboardTransform)
		SceneEditor::SetTransform(item, clipboardTransform);
	SelectItem(obs_sceneitem_get_id(item));
}

void StudioWindow::EditRemove()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	obs_sceneitem_remove(item);
	SelectItem(-1);
}

void StudioWindow::EditCopyTransform()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	copiedTransform = SceneEditor::GetTransform(item);
	hasCopiedTransform = true;
}

void StudioWindow::EditPasteTransform()
{
	OBSSceneItem item = CurrentItem();
	if (item && hasCopiedTransform)
		SceneEditor::SetTransform(item, copiedTransform);
}

void StudioWindow::EditResetTransform()
{
	if (OBSSceneItem item = CurrentItem())
		SceneEditor::ResetTransform(item);
}

void StudioWindow::EditFit()
{
	if (OBSSceneItem item = CurrentItem())
		SceneEditor::FitToCanvas(item, activeDest->Width(), activeDest->Height());
}

void StudioWindow::EditStretch()
{
	if (OBSSceneItem item = CurrentItem())
		SceneEditor::StretchToCanvas(item, activeDest->Width(), activeDest->Height());
}

void StudioWindow::EditCenter()
{
	if (OBSSceneItem item = CurrentItem())
		SceneEditor::CenterInCanvas(item, activeDest->Width(), activeDest->Height());
}

static void Reorder(OBSSceneItem item, enum obs_order_movement movement)
{
	if (item)
		obs_sceneitem_set_order(item, movement);
}

void StudioWindow::EditMoveUp()
{
	Reorder(CurrentItem(), OBS_ORDER_MOVE_UP);
	RefreshSources();
}

void StudioWindow::EditMoveDown()
{
	Reorder(CurrentItem(), OBS_ORDER_MOVE_DOWN);
	RefreshSources();
}

void StudioWindow::EditMoveTop()
{
	Reorder(CurrentItem(), OBS_ORDER_MOVE_TOP);
	RefreshSources();
}

void StudioWindow::EditMoveBottom()
{
	Reorder(CurrentItem(), OBS_ORDER_MOVE_BOTTOM);
	RefreshSources();
}

void StudioWindow::EditProperties()
{
	if (OBSSceneItem item = CurrentItem())
		obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
}

void StudioWindow::EditFilters()
{
	if (OBSSceneItem item = CurrentItem())
		obs_frontend_open_source_filters(obs_sceneitem_get_source(item));
}

/* ------------------------------------------------------------------ */
/* Transitions                                                          */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshTransitions()
{
	refreshingTransitions = true;
	transitionCombo->clear();

	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		transitionCombo->addItem(QString::fromUtf8(obs_source_get_name(tr)),
					 QString::fromUtf8(obs_source_get_uuid(tr)));
		if (tr == current.Get())
			transitionCombo->setCurrentIndex(transitionCombo->count() - 1);
	}
	obs_frontend_source_list_free(&list);

	durationSpin->setValue(obs_frontend_get_transition_duration());
	durationSpin->setEnabled(current && !obs_transition_fixed(current));
	refreshingTransitions = false;
}

/* ------------------------------------------------------------------ */
/* Destinations                                                         */
/* ------------------------------------------------------------------ */

void StudioWindow::FillDestinationsMenu()
{
	destinationsMenu->clear();

	for (auto *d : Manager::Get().Destinations()) {
		QMenu *sub = destinationsMenu->addMenu(
			QString("%1  (%2×%3)").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height()));

		connect(sub->addAction(Tr("MDS.DestinationSettings")), &QAction::triggered, this,
			[this, d]() { EditDestination(d); });
		sub->addSeparator();

		QScreen *screen = Dialogs::FindScreen(d->OutputScreen());
		QAction *full = sub->addAction(screen ? Tr("MDS.OpenFullscreenOn").arg(screen->name())
						      : Tr("MDS.OpenFullscreen"));
		full->setEnabled(screen != nullptr);
		if (!screen)
			full->setToolTip(Tr("MDS.NoScreenHint"));
		connect(full, &QAction::triggered, this, [this, d]() { OpenProjector(d, true); });

		connect(sub->addAction(Tr("MDS.OpenWindowed")), &QAction::triggered, this,
			[this, d]() { OpenProjector(d, false); });

		QAction *close = sub->addAction(Tr("MDS.CloseProjection"));
		close->setEnabled(HasProjector(d));
		connect(close, &QAction::triggered, this, [this, d]() { CloseProjectors(d); });

		sub->addSeparator();
		QAction *del = sub->addAction(Tr("MDS.Delete"));
		del->setEnabled(!d->IsMain());
		connect(del, &QAction::triggered, this, [this, d]() { DeleteDestination(d); });
	}

	destinationsMenu->addSeparator();
	connect(destinationsMenu->addAction(Tr("MDS.NewDestinationMenu")), &QAction::triggered, this,
		&StudioWindow::NewDestination);
}

void StudioWindow::NewDestination()
{
	Dialogs::DestinationSettings settings;
	settings.name =
		QString("%1 %2").arg(Tr("MDS.Destination")).arg(Manager::Get().Destinations().size()).toStdString();
	if (!Dialogs::EditDestination(this, nullptr, settings))
		return;

	Destination *dest = Manager::Get().Add(settings.name, settings.width, settings.height);
	if (!dest) {
		QMessageBox::warning(this, Tr("MDS.NewDestination"), Tr("MDS.AddFailed"));
		return;
	}
	dest->SetOutputScreen(settings.outputScreen);
}

void StudioWindow::EditDestination(Destination *dest)
{
	if (!dest)
		return;
	Dialogs::DestinationSettings settings;
	settings.name = dest->Name();
	settings.width = dest->Width();
	settings.height = dest->Height();
	settings.outputScreen = dest->OutputScreen();
	if (!Dialogs::EditDestination(this, dest, settings))
		return;

	if (!dest->IsMain()) {
		dest->SetName(settings.name);
		dest->Attach(settings.width, settings.height);
	}
	dest->SetOutputScreen(settings.outputScreen);
	Manager::Get().NotifyDestinationsChanged();
}

void StudioWindow::DeleteDestination(Destination *dest)
{
	if (!dest || dest->IsMain())
		return;
	if (Dialogs::ConfirmDeleteDestination(this, dest))
		Manager::Get().Remove(dest);
}

bool StudioWindow::HasProjector(Destination *dest) const
{
	for (auto &p : projectors) {
		if (p.first == dest && p.second)
			return true;
	}
	return false;
}

void StudioWindow::OpenProjector(Destination *dest, bool fullscreen)
{
	QScreen *screen = fullscreen ? Dialogs::FindScreen(dest->OutputScreen()) : nullptr;
	if (fullscreen && !screen)
		return;

	/* Un seul projecteur par écran, et un seul plein écran par
	 * destination. */
	for (auto &p : projectors) {
		if (!p.second)
			continue;
		if ((screen && p.second->TargetScreen() == screen) ||
		    (fullscreen && p.first == dest && p.second->TargetScreen()))
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
