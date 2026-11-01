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

#include "tile-grid.hpp"
#include "destination.hpp"
#include "qt-display.hpp"
#include "render-helpers.hpp"
#include "text.hpp"
#include "undo.hpp"

#include <obs-frontend-api.h>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using SceneEditor::Box;

static constexpr int HEADER_HEIGHT = 24;
static constexpr int GRID_GAP = 6;
static constexpr float HANDLE_TOLERANCE_PX = 7.0f;
static constexpr float SNAP_PX = 8.0f;
static constexpr uint32_t ACTIVE_FRAME_COLOR = 0xB0228B3A; /* vert discret */

/* ------------------------------------------------------------------ */
/* Affichage avec souris et clavier                                     */
/* ------------------------------------------------------------------ */

class EditDisplay : public OBSQTDisplay {
public:
	EditDisplay(TileWidget *tile_, QWidget *parent) : OBSQTDisplay(parent), tile(tile_)
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::ClickFocus);
	}

protected:
	void mousePressEvent(QMouseEvent *e) override { tile->MousePress(e); }
	void mouseMoveEvent(QMouseEvent *e) override { tile->MouseMove(e); }
	void mouseReleaseEvent(QMouseEvent *e) override { tile->MouseRelease(e); }
	void mouseDoubleClickEvent(QMouseEvent *e) override { tile->MouseDoubleClick(e); }
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!tile->KeyPress(e))
			OBSQTDisplay::keyPressEvent(e);
	}

private:
	TileWidget *tile;
};

/* ------------------------------------------------------------------ */
/* Tuile                                                                */
/* ------------------------------------------------------------------ */

TileWidget::TileWidget(Destination *destination, bool program_, QWidget *parent)
	: QWidget(parent),
	  dest(destination),
	  program(program_)
{
	editable = !program;

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	header = new QWidget(this);
	header->setFixedHeight(HEADER_HEIGHT);
	auto *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(4);
	headerLabel = new QLabel(header);
	headerLayout->addWidget(headerLabel, 1);

	focusButton = new QPushButton(header);
	focusButton->setFixedHeight(HEADER_HEIGHT);
	focusButton->setToolTip(Tr("MDS.FocusTip"));
	connect(focusButton, &QPushButton::clicked, this, [this]() { emit focusRequested(this); });
	headerLayout->addWidget(focusButton);

	if (!program) {
		lockButton = new QPushButton(header);
		lockButton->setCheckable(true);
		lockButton->setChecked(dest->Locked());
		lockButton->setToolTip(Tr("MDS.LockTip"));
		lockButton->setFixedHeight(HEADER_HEIGHT);
		connect(lockButton, &QPushButton::toggled, this, [this](bool on) {
			dest->SetLocked(on);
			RefreshHeader();
			emit lockChanged(this);
		});
		headerLayout->addWidget(lockButton);
	}
	layout->addWidget(header);

	display = new EditDisplay(this, this);
	layout->addWidget(display, 1);

	connect(display, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), DrawCb, this); });

	RefreshHeader();
}

TileWidget::~TileWidget()
{
	if (display && display->GetDisplay())
		obs_display_remove_draw_callback(display->GetDisplay(), DrawCb, this);
}

void TileWidget::SetLinkedScene(obs_source_t *scene)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (obs_weak_source_references_source(linkedScene, scene) || (!scene && !linkedScene))
			return;
		linkedScene = scene ? OBSGetWeakRef(scene) : OBSWeakSource();
	}
	if (!program) {
		selectedId = -1;
		drag = Drag::None;
	}
	RefreshHeader();
}

OBSSource TileWidget::LinkedScene() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return OBSGetStrongRef(linkedScene);
}

OBSSource TileWidget::EditScene() const
{
	return program ? dest->LiveScene() : LinkedScene();
}

void TileWidget::SetEditable(bool value)
{
	editable = value;
	if (!value) {
		selectedId = -1;
		drag = Drag::None;
	}
}

void TileWidget::SetActive(bool value)
{
	if (active.load() == value)
		return;
	active = value;
	RefreshHeader();
}

void TileWidget::SetFocused(bool value)
{
	focused = value;
	RefreshHeader();
}

void TileWidget::SetSelectedItem(int64_t id)
{
	selectedId = id;
}

void TileWidget::RefreshHeader()
{
	QString text =
		QString("%1  ·  %2×%3").arg(QString::fromStdString(dest->Name())).arg(dest->Width()).arg(dest->Height());
	if (program) {
		const std::string pgm = dest->ProgramSceneName();
		if (!pgm.empty())
			text += "  —  " + QString::fromStdString(pgm);
	}
	headerLabel->setText(text);

	QFont font = headerLabel->font();
	font.setBold(active.load());
	headerLabel->setFont(font);

	focusButton->setText(focused ? Tr("MDS.Unfocus") : Tr("MDS.Focus"));

	if (lockButton) {
		const QSignalBlocker blocker(lockButton);
		lockButton->setChecked(dest->Locked());
		lockButton->setText(dest->Locked() ? Tr("MDS.Locked") : Tr("MDS.Lock"));
	}
}

/* ------------------------------------------------------------------ */
/* Rendu (thread graphique)                                             */
/* ------------------------------------------------------------------ */

void TileWidget::DrawCb(void *data, uint32_t cx, uint32_t cy)
{
	auto *tile = static_cast<TileWidget *>(data);
	Destination *d = tile->dest;
	if (!d || !d->Attached())
		return;

	const uint32_t w = d->Width();
	const uint32_t h = d->Height();
	const bool isActive = tile->active.load() && tile->highlighted.load();
	const bool canEdit = tile->editable.load();
	const int64_t selected = tile->selectedId.load();

	DrawFitted(w, h, cx, cy, [&](float screenScale) {
		const float px = 1.0f / screenScale;
		OBSSource scene;

		if (tile->program) {
			d->RenderProgram();
			if (canEdit)
				scene = d->LiveScene();
		} else {
			scene = tile->LinkedScene();
			DrawSolidRect(0.0f, 0.0f, float(w), float(h), 0xFF000000);
			if (scene) {
				const bool previous = gs_set_linear_srgb(true);
				obs_source_video_render(scene);
				gs_set_linear_srgb(previous);
			}
		}

		/* Contour discret de la destination sur laquelle on travaille. */
		if (isActive && canEdit)
			DrawOutline(1.5f * px, 1.5f * px, float(w) - 3.0f * px, float(h) - 3.0f * px, 1.5f * px,
				    ACTIVE_FRAME_COLOR);

		/* Sélection et guides seulement sur la tuile active ; cadres
		 * provisoires des médias toujours visibles. */
		if (scene)
			SceneEditor::DrawSelection(obs_scene_from_source(scene), (isActive && canEdit) ? selected : -1,
						   screenScale, w, h);
	});
}

/* ------------------------------------------------------------------ */
/* Édition à la souris                                                  */
/* ------------------------------------------------------------------ */

bool TileWidget::ToCanvas(const QPointF &pos, float &x, float &y, float &screenScale) const
{
	const qreal ratio = display->devicePixelRatioF();
	const QSize size = display->size() * ratio;
	const FitRect fit = ComputeFit(dest->Width(), dest->Height(), uint32_t(size.width()), uint32_t(size.height()));
	if (fit.scale <= 0.0f || fit.cx <= 0)
		return false;
	x = (float(pos.x() * ratio) - float(fit.x)) / fit.scale;
	y = (float(pos.y() * ratio) - float(fit.y)) / fit.scale;
	screenScale = fit.scale / float(ratio);
	return true;
}

bool TileWidget::CanvasPointAt(const QPoint &tilePos, float &x, float &y) const
{
	float s;
	return ToCanvas(QPointF(display->mapFrom(this, tilePos)), x, y, s);
}

OBSSceneItem TileWidget::SelectedSceneItem() const
{
	const int64_t id = selectedId.load();
	if (id < 0)
		return nullptr;
	OBSSource scene = EditScene();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!s)
		return nullptr;
	return obs_scene_find_sceneitem_by_id(s, id);
}

void TileWidget::MousePress(QMouseEvent *event)
{
	if (!editable.load())
		return;
	highlighted = true;
	emit activated(this);

	float x, y, s;
	if (!ToCanvas(event->position(), x, y, s))
		return;
	OBSSource scene = EditScene();
	obs_scene_t *sc = scene ? obs_scene_from_source(scene) : nullptr;
	if (!sc)
		return;

	display->setFocus();

	if (event->button() == Qt::RightButton) {
		const int64_t id = SceneEditor::HitTest(sc, x, y);
		SetSelectedItem(id);
		emit selectionChanged(this, id);
		emit contextMenuRequested(this, event->globalPosition().toPoint());
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;

	startX = x;
	startY = y;
	dragMoved = false;
	const float tol = HANDLE_TOLERANCE_PX / s;

	/* Poignées de la source déjà sélectionnée. */
	OBSSceneItem item = SelectedSceneItem();
	Box box;
	if (item && SceneEditor::GetBox(item, box) && !SceneEditor::IsRotated(item)) {
		const int edges = SceneEditor::HandleAt(box, x, y, tol);
		if (edges != SceneEditor::EDGE_NONE) {
			const bool alt = event->modifiers() & Qt::AltModifier;
			drag = (alt && !SceneEditor::UsesBounds(item)) ? Drag::Crop : Drag::Resize;
			dragEdges = edges;
			startBox = box;
			obs_sceneitem_get_crop(item, &startCrop);
			struct vec2 scale;
			obs_sceneitem_get_scale(item, &scale);
			startScaleX = std::fabs(scale.x);
			startScaleY = std::fabs(scale.y);
			return;
		}
	}

	const int64_t id = SceneEditor::HitTest(sc, x, y);
	SetSelectedItem(id);
	emit selectionChanged(this, id);

	item = SelectedSceneItem();
	if (item && SceneEditor::GetBox(item, startBox))
		drag = Drag::Move;
}

void TileWidget::MouseMove(QMouseEvent *event)
{
	if (!editable.load())
		return;

	float x, y, s;
	if (!ToCanvas(event->position(), x, y, s))
		return;

	OBSSceneItem item = SelectedSceneItem();

	if (drag == Drag::None) {
		/* Curseur selon la poignée survolée. */
		Box box;
		int edges = 0;
		if (active.load() && item && SceneEditor::GetBox(item, box) && !SceneEditor::IsRotated(item))
			edges = SceneEditor::HandleAt(box, x, y, HANDLE_TOLERANCE_PX / s);
		const bool lr = edges & (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_RIGHT);
		const bool tb = edges & (SceneEditor::EDGE_TOP | SceneEditor::EDGE_BOTTOM);
		const bool diagMain = (edges == (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_TOP)) ||
				      (edges == (SceneEditor::EDGE_RIGHT | SceneEditor::EDGE_BOTTOM));
		if (lr && tb)
			display->setCursor(diagMain ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
		else if (lr)
			display->setCursor(Qt::SizeHorCursor);
		else if (tb)
			display->setCursor(Qt::SizeVerCursor);
		else
			display->unsetCursor();
		return;
	}

	if (!item) {
		drag = Drag::None;
		return;
	}

	const float dx = x - startX;
	const float dy = y - startY;
	if (!dragMoved) {
		if (std::fabs(dx) * s < 1.0f && std::fabs(dy) * s < 1.0f)
			return;
		/* Première modification réelle : on mémorise l'état pour Ctrl+Z. */
		OBSSource scene = EditScene();
		Undo::PushSceneState(obs_scene_from_source(scene));
		dragMoved = true;
	}

	const float cw = float(dest->Width());
	const float ch = float(dest->Height());
	const bool noSnap = event->modifiers() & Qt::ControlModifier;
	const float tol = SNAP_PX / s;

	if (drag == Drag::Move) {
		/* Pas de limite aux bords ; seul le centre est aimanté. */
		Box b = startBox;
		b.x += dx;
		b.y += dy;
		if (!noSnap) {
			const float offX = cw * 0.5f - (b.x + b.w * 0.5f);
			const float offY = ch * 0.5f - (b.y + b.h * 0.5f);
			if (std::fabs(offX) <= tol)
				b.x += offX;
			if (std::fabs(offY) <= tol)
				b.y += offY;
		}
		SceneEditor::SetBox(item, b);
		return;
	}

	if (drag == Drag::Resize) {
		const Box &b = startBox;
		const bool horiz = dragEdges & (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_RIGHT);
		const bool vert = dragEdges & (SceneEditor::EDGE_TOP | SceneEditor::EDGE_BOTTOM);
		const bool freeRatio = event->modifiers() & Qt::ShiftModifier;
		const float ratio = b.h > 0.0f ? b.w / b.h : 1.0f;

		float w = b.w;
		float h = b.h;
		if (dragEdges & SceneEditor::EDGE_LEFT)
			w = b.w - dx;
		if (dragEdges & SceneEditor::EDGE_RIGHT)
			w = b.w + dx;
		if (dragEdges & SceneEditor::EDGE_TOP)
			h = b.h - dy;
		if (dragEdges & SceneEditor::EDGE_BOTTOM)
			h = b.h + dy;
		w = std::fmax(w, 4.0f);
		h = std::fmax(h, 4.0f);

		/* Proportions gardées sur toutes les poignées, sauf avec Maj. */
		if (!freeRatio) {
			if (horiz && vert) {
				if (w / b.w > h / b.h)
					h = w / ratio;
				else
					w = h * ratio;
			} else if (horiz) {
				h = w / ratio;
			} else if (vert) {
				w = h * ratio;
			}
		}

		Box nb;
		nb.w = w;
		nb.h = h;
		/* Le bord opposé reste fixe ; sur une poignée de côté, l'autre
		 * axe reste centré. */
		if (dragEdges & SceneEditor::EDGE_LEFT)
			nb.x = b.x + b.w - w;
		else if (dragEdges & SceneEditor::EDGE_RIGHT)
			nb.x = b.x;
		else
			nb.x = b.x + (b.w - w) * 0.5f;
		if (dragEdges & SceneEditor::EDGE_TOP)
			nb.y = b.y + b.h - h;
		else if (dragEdges & SceneEditor::EDGE_BOTTOM)
			nb.y = b.y;
		else
			nb.y = b.y + (b.h - h) * 0.5f;
		SceneEditor::SetBox(item, nb);
		return;
	}

	if (drag == Drag::Crop) {
		uint32_t usw = 0;
		uint32_t ush = 0;
		SceneEditor::SourceSize(obs_sceneitem_get_source(item), usw, ush);
		const int sw = int(usw);
		const int sh = int(ush);
		if (sw <= 0 || sh <= 0 || startScaleX <= 0.0f || startScaleY <= 0.0f)
			return;

		struct obs_sceneitem_crop crop = startCrop;
		Box b = startBox;

		if (dragEdges & SceneEditor::EDGE_LEFT) {
			const int v = startCrop.left + int(std::lround(dx / startScaleX));
			crop.left = std::max(0, std::min(v, sw - crop.right - 1));
			const float d = float(crop.left - startCrop.left) * startScaleX;
			b.x += d;
			b.w -= d;
		}
		if (dragEdges & SceneEditor::EDGE_RIGHT) {
			const int v = startCrop.right - int(std::lround(dx / startScaleX));
			crop.right = std::max(0, std::min(v, sw - crop.left - 1));
			b.w -= float(crop.right - startCrop.right) * startScaleX;
		}
		if (dragEdges & SceneEditor::EDGE_TOP) {
			const int v = startCrop.top + int(std::lround(dy / startScaleY));
			crop.top = std::max(0, std::min(v, sh - crop.bottom - 1));
			const float d = float(crop.top - startCrop.top) * startScaleY;
			b.y += d;
			b.h -= d;
		}
		if (dragEdges & SceneEditor::EDGE_BOTTOM) {
			const int v = startCrop.bottom - int(std::lround(dy / startScaleY));
			crop.bottom = std::max(0, std::min(v, sh - crop.top - 1));
			b.h -= float(crop.bottom - startCrop.bottom) * startScaleY;
		}

		obs_sceneitem_set_crop(item, &crop);
		SceneEditor::SetBox(item, b);
	}
}

void TileWidget::MouseRelease(QMouseEvent *event)
{
	(void)event;
	if (drag != Drag::None) {
		drag = Drag::None;
		if (dragMoved)
			emit itemsChanged(this);
	}
}

void TileWidget::MouseDoubleClick(QMouseEvent *event)
{
	(void)event;
	if (!editable.load())
		return;
	OBSSceneItem item = SelectedSceneItem();
	if (item)
		obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
}

bool TileWidget::KeyPress(QKeyEvent *event)
{
	if (!editable.load())
		return false;
	OBSSceneItem item = SelectedSceneItem();
	if (!item)
		return false;

	const float step = (event->modifiers() & Qt::ShiftModifier) ? 10.0f : 1.0f;
	float dx = 0.0f;
	float dy = 0.0f;
	switch (event->key()) {
	case Qt::Key_Left:
		dx = -step;
		break;
	case Qt::Key_Right:
		dx = step;
		break;
	case Qt::Key_Up:
		dy = -step;
		break;
	case Qt::Key_Down:
		dy = step;
		break;
	default:
		return false;
	}

	if (!event->isAutoRepeat()) {
		OBSSource scene = EditScene();
		Undo::PushSceneState(obs_scene_from_source(scene));
	}
	struct vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	pos.x += dx;
	pos.y += dy;
	obs_sceneitem_set_pos(item, &pos);
	emit itemsChanged(this);
	return true;
}

/* ------------------------------------------------------------------ */
/* Colonne                                                              */
/* ------------------------------------------------------------------ */

TileGrid::TileGrid(bool program_, QWidget *parent) : QWidget(parent), program(program_)
{
	emptyLabel = new QLabel(Tr("MDS.NoDestinationForScene"), this);
	emptyLabel->setAlignment(Qt::AlignCenter);
	setMinimumHeight(80);
}

void TileGrid::Clear()
{
	for (auto *t : tiles)
		delete t;
	tiles.clear();
	emptyLabel->setVisible(true);
}

std::vector<TileWidget *> TileGrid::SetDestinations(const std::vector<Destination *> &dests)
{
	std::vector<TileWidget *> next;
	std::vector<TileWidget *> created;

	/* Réutiliser les tuiles des destinations toujours affichées : leur
	 * affichage OBS n'est pas recréé, donc pas de clignotement. */
	for (auto *d : dests) {
		TileWidget *tile = nullptr;
		for (auto *&t : tiles) {
			if (t && t->Dest() == d) {
				tile = t;
				t = nullptr;
				break;
			}
		}
		if (!tile) {
			tile = new TileWidget(d, program, this);
			tile->show();
			created.push_back(tile);
		}
		next.push_back(tile);
	}
	for (auto *t : tiles)
		delete t;
	tiles = next;

	emptyLabel->setVisible(tiles.empty());
	Relayout();
	return created;
}

TileWidget *TileGrid::TileFor(Destination *dest) const
{
	for (auto *t : tiles) {
		if (t->Dest() == dest)
			return t;
	}
	return nullptr;
}

TileWidget *TileGrid::TileAt(const QPoint &globalPos) const
{
	for (auto *t : tiles) {
		if (t->rect().contains(t->mapFromGlobal(globalPos)))
			return t;
	}
	return nullptr;
}

void TileGrid::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	Relayout();
}

/* Empile les tuiles verticalement : chaque tuile prend toute la largeur
 * disponible pour son format, puis tout est réduit si la hauteur manque. */
void TileGrid::Relayout()
{
	emptyLabel->setGeometry(rect());
	const size_t n = tiles.size();
	if (n == 0)
		return;

	const double W = double(width());
	const double H = double(height());
	if (W <= 0.0 || H <= 0.0)
		return;

	std::vector<double> aspect;
	double natural = 0.0;
	for (auto *t : tiles) {
		const double w = double(t->Dest()->Width());
		const double h = double(t->Dest()->Height());
		const double a = h > 0.0 ? w / h : 16.0 / 9.0;
		aspect.push_back(a);
		natural += W / a;
	}

	const double fixed = double(n) * HEADER_HEIGHT + double(GRID_GAP) * double(n - 1);
	const double room = H - fixed;
	if (room <= 0.0)
		return;
	const double scale = natural > room ? room / natural : 1.0;

	double blockH = fixed;
	for (size_t i = 0; i < n; i++)
		blockH += W / aspect[i] * scale;

	double y = (H - blockH) / 2.0;
	for (size_t i = 0; i < n; i++) {
		const double h = W / aspect[i] * scale;
		const double w = h * aspect[i];
		const double x = (W - w) / 2.0;
		tiles[i]->setGeometry(int(x), int(y), int(w), int(h) + HEADER_HEIGHT);
		y += h + HEADER_HEIGHT + GRID_GAP;
	}
}
