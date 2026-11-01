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

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "text.hpp"

using SceneEditor::Box;

static constexpr int HEADER_HEIGHT = 24;
static constexpr int GRID_GAP = 6;
static constexpr float HANDLE_TOLERANCE_PX = 7.0f;
static constexpr float SNAP_PX = 8.0f;

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
	selectedId = -1;
	drag = Drag::None;
	RefreshHeader();
}

OBSSource TileWidget::LinkedScene() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return OBSGetStrongRef(linkedScene);
}

void TileWidget::SetActive(bool value)
{
	if (active == value)
		return;
	active = value;
	RefreshHeader();
}

void TileWidget::SetTally(bool value)
{
	if (onAir == value)
		return;
	onAir = value;
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
	font.setBold(active);
	headerLabel->setFont(font);

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

	if (tile->program) {
		DrawFitted(w, h, cx, cy, [d](float) { d->RenderProgram(); });
		return;
	}

	OBSSource scene = tile->LinkedScene();
	const int64_t selected = tile->selectedId.load();

	DrawFitted(w, h, cx, cy, [&](float screenScale) {
		if (!scene) {
			/* Scène non couverte : la destination garde son Program,
			 * affiché assombri. */
			d->RenderProgram();
			DrawSolidRect(0.0f, 0.0f, float(w), float(h), 0xB0000000);
			return;
		}
		DrawSolidRect(0.0f, 0.0f, float(w), float(h), 0xFF000000);
		const bool previous = gs_set_linear_srgb(true);
		obs_source_video_render(scene);
		gs_set_linear_srgb(previous);
		SceneEditor::DrawSelection(obs_scene_from_source(scene), selected, screenScale, w, h);
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

OBSSceneItem TileWidget::SelectedSceneItem() const
{
	const int64_t id = selectedId.load();
	if (id < 0)
		return nullptr;
	OBSSource scene = LinkedScene();
	obs_scene_t *s = scene ? obs_scene_from_source(scene) : nullptr;
	if (!s)
		return nullptr;
	return obs_scene_find_sceneitem_by_id(s, id);
}

void TileWidget::MousePress(QMouseEvent *event)
{
	emit activated(this);
	if (program)
		return;

	float x, y, s;
	if (!ToCanvas(event->position(), x, y, s))
		return;
	OBSSource scene = LinkedScene();
	obs_scene_t *sc = scene ? obs_scene_from_source(scene) : nullptr;
	if (!sc)
		return;

	display->setFocus();

	if (event->button() == Qt::RightButton) {
		const int64_t id = SceneEditor::HitTest(sc, x, y);
		if (id >= 0) {
			SetSelectedItem(id);
			emit selectionChanged(this, id);
			emit contextMenuRequested(this, event->globalPosition().toPoint());
		}
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;

	startX = x;
	startY = y;
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

static float Snap(float value, float target, float tol, bool &snapped)
{
	if (!snapped && std::fabs(value - target) <= tol) {
		snapped = true;
		return target - value;
	}
	return 0.0f;
}

void TileWidget::MouseMove(QMouseEvent *event)
{
	if (program)
		return;

	float x, y, s;
	if (!ToCanvas(event->position(), x, y, s))
		return;

	OBSSceneItem item = SelectedSceneItem();

	if (drag == Drag::None) {
		/* Curseur selon la poignée survolée. */
		Box box;
		int edges = 0;
		if (item && SceneEditor::GetBox(item, box) && !SceneEditor::IsRotated(item))
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
	const float cw = float(dest->Width());
	const float ch = float(dest->Height());
	const bool noSnap = event->modifiers() & Qt::ControlModifier;
	const float tol = SNAP_PX / s;

	if (drag == Drag::Move) {
		Box b = startBox;
		b.x += dx;
		b.y += dy;
		if (!noSnap) {
			bool sx = false;
			bool sy = false;
			float ox = Snap(b.x, 0.0f, tol, sx);
			ox += Snap(b.x + b.w, cw, tol, sx);
			ox += Snap(b.x + b.w * 0.5f, cw * 0.5f, tol, sx);
			float oy = Snap(b.y, 0.0f, tol, sy);
			oy += Snap(b.y + b.h, ch, tol, sy);
			oy += Snap(b.y + b.h * 0.5f, ch * 0.5f, tol, sy);
			b.x += ox;
			b.y += oy;
		}
		SceneEditor::SetBox(item, b);
		return;
	}

	if (drag == Drag::Resize) {
		const Box &b = startBox;
		float x0 = b.x, y0 = b.y, x1 = b.x + b.w, y1 = b.y + b.h;
		if (dragEdges & SceneEditor::EDGE_LEFT)
			x0 += dx;
		if (dragEdges & SceneEditor::EDGE_RIGHT)
			x1 += dx;
		if (dragEdges & SceneEditor::EDGE_TOP)
			y0 += dy;
		if (dragEdges & SceneEditor::EDGE_BOTTOM)
			y1 += dy;

		if (!noSnap) {
			bool sx = false;
			bool sy = false;
			if (dragEdges & SceneEditor::EDGE_LEFT)
				x0 += Snap(x0, 0.0f, tol, sx);
			if (dragEdges & SceneEditor::EDGE_RIGHT)
				x1 += Snap(x1, cw, tol, sx);
			if (dragEdges & SceneEditor::EDGE_TOP)
				y0 += Snap(y0, 0.0f, tol, sy);
			if (dragEdges & SceneEditor::EDGE_BOTTOM)
				y1 += Snap(y1, ch, tol, sy);
		}

		float w = std::fmax(x1 - x0, 4.0f);
		float h = std::fmax(y1 - y0, 4.0f);

		const bool corner = (dragEdges & (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_RIGHT)) &&
				    (dragEdges & (SceneEditor::EDGE_TOP | SceneEditor::EDGE_BOTTOM));
		const bool freeRatio = event->modifiers() & Qt::ShiftModifier;
		if (corner && !freeRatio && b.w > 0.0f && b.h > 0.0f) {
			const float ratio = b.w / b.h;
			if (w / b.w > h / b.h)
				h = w / ratio;
			else
				w = h * ratio;
		}

		Box nb;
		nb.w = w;
		nb.h = h;
		nb.x = (dragEdges & SceneEditor::EDGE_LEFT) ? (b.x + b.w - w) : b.x;
		nb.y = (dragEdges & SceneEditor::EDGE_TOP) ? (b.y + b.h - h) : b.y;
		SceneEditor::SetBox(item, nb);
		return;
	}

	if (drag == Drag::Crop) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		const int sw = int(obs_source_get_width(source));
		const int sh = int(obs_source_get_height(source));
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
		emit itemsChanged(this);
	}
}

void TileWidget::MouseDoubleClick(QMouseEvent *event)
{
	(void)event;
	if (program)
		return;
	OBSSceneItem item = SelectedSceneItem();
	if (item)
		obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
}

bool TileWidget::KeyPress(QKeyEvent *event)
{
	if (program)
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

	struct vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	pos.x += dx;
	pos.y += dy;
	obs_sceneitem_set_pos(item, &pos);
	return true;
}

/* ------------------------------------------------------------------ */
/* Grille                                                               */
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

void TileGrid::SetDestinations(const std::vector<Destination *> &dests)
{
	Clear();
	for (auto *d : dests) {
		auto *tile = new TileWidget(d, program, this);
		tile->show();
		tiles.push_back(tile);
	}
	emptyLabel->setVisible(tiles.empty());
	Relayout();
}

TileWidget *TileGrid::TileFor(Destination *dest) const
{
	for (auto *t : tiles) {
		if (t->Dest() == dest)
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
