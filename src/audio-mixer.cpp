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

#include "audio-mixer.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>

#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

static constexpr float METER_MIN_DB = -60.0f;

/* ------------------------------------------------------------------ */
/* Tranche                                                              */
/* ------------------------------------------------------------------ */

AudioStrip::AudioStrip(obs_source_t *src, QWidget *parent) : QWidget(parent), source(src)
{
	auto *grid = new QGridLayout(this);
	grid->setContentsMargins(2, 2, 2, 6);
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(2);

	nameLabel = new QLabel(QString::fromUtf8(obs_source_get_name(source)), this);
	dbLabel = new QLabel(this);
	dbLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	grid->addWidget(nameLabel, 0, 0, 1, 2);
	grid->addWidget(dbLabel, 0, 2);

	meter = new QProgressBar(this);
	meter->setRange(0, 1000);
	meter->setTextVisible(false);
	meter->setFixedHeight(6);
	grid->addWidget(meter, 1, 0, 1, 3);

	slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, 1000);
	grid->addWidget(slider, 2, 0, 1, 2);

	muteButton = new QToolButton(this);
	muteButton->setCheckable(true);
	muteButton->setToolTip(Tr("MDS.Mute"));
	grid->addWidget(muteButton, 2, 2);
	grid->setColumnStretch(0, 1);

	fader = obs_fader_create(OBS_FADER_LOG);
	obs_fader_attach_source(fader, source);
	volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_attach_source(volmeter, source);
	obs_volmeter_add_callback(volmeter, VolmeterCb, this);

	connect(slider, &QSlider::valueChanged, this, [this](int value) {
		if (updating)
			return;
		obs_fader_set_deflection(fader, float(value) / 1000.0f);
		Poll();
	});
	connect(muteButton, &QToolButton::toggled, this, [this](bool on) {
		if (updating)
			return;
		obs_source_set_muted(source, on);
		Poll();
	});

	Poll();
}

AudioStrip::~AudioStrip()
{
	obs_volmeter_remove_callback(volmeter, VolmeterCb, this);
	obs_volmeter_detach_source(volmeter);
	obs_volmeter_destroy(volmeter);
	obs_fader_detach_source(fader);
	obs_fader_destroy(fader);
}

void AudioStrip::VolmeterCb(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			    const float[MAX_AUDIO_CHANNELS])
{
	auto *strip = static_cast<AudioStrip *>(param);
	float maxDb = -100.0f;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (std::isfinite(peak[i]))
			maxDb = std::max(maxDb, peak[i]);
	}
	/* Garder le pic jusqu'à la lecture suivante. */
	float current = strip->peakDb.load();
	while (maxDb > current && !strip->peakDb.compare_exchange_weak(current, maxDb)) {
	}
}

void AudioStrip::Poll()
{
	updating = true;

	const float deflection = obs_fader_get_deflection(fader);
	const int sliderValue = int(std::lround(deflection * 1000.0f));
	if (!slider->isSliderDown() && slider->value() != sliderValue)
		slider->setValue(sliderValue);

	const float db = obs_fader_get_db(fader);
	dbLabel->setText(std::isfinite(db) ? QString("%1 dB").arg(double(db), 0, 'f', 1) : QString("-inf dB"));

	const bool muted = obs_source_muted(source);
	if (muteButton->isChecked() != muted)
		muteButton->setChecked(muted);
	muteButton->setText(muted ? Tr("MDS.Muted") : Tr("MDS.Mute"));

	const char *name = obs_source_get_name(source);
	if (name && nameLabel->text() != QString::fromUtf8(name))
		nameLabel->setText(QString::fromUtf8(name));

	const float peak = peakDb.exchange(-100.0f);
	const float level = std::clamp((peak - METER_MIN_DB) / -METER_MIN_DB, 0.0f, 1.0f);
	meter->setValue(muted ? 0 : int(std::lround(level * 1000.0f)));

	updating = false;
}

/* ------------------------------------------------------------------ */
/* Mixeur                                                               */
/* ------------------------------------------------------------------ */

AudioMixer::AudioMixer(QWidget *parent) : QWidget(parent)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	stripsLayout = new QVBoxLayout(content);
	stripsLayout->setContentsMargins(4, 4, 4, 4);
	stripsLayout->addStretch(1);
	scroll->setWidget(content);
	outer->addWidget(scroll);

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!isVisible())
			return;
		for (auto *s : strips)
			s->Poll();
	});
	timer->start(50);

	/* Sources ajoutées / supprimées / renommées dans OBS. */
	auto *listTimer = new QTimer(this);
	connect(listTimer, &QTimer::timeout, this, [this]() {
		if (isVisible() && CurrentSourceIds() != shownIds)
			Refresh();
	});
	listTimer->start(1000);
}

std::vector<std::string> AudioMixer::CurrentSourceIds() const
{
	std::vector<std::string> ids;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<std::string> *>(param);
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				list->emplace_back(obs_source_get_uuid(source));
			return true;
		},
		&ids);
	return ids;
}

void AudioMixer::Clear()
{
	for (auto *s : strips)
		delete s;
	strips.clear();
	shownIds.clear();
}

void AudioMixer::Refresh()
{
	Clear();
	shownIds = CurrentSourceIds();
	for (const auto &id : shownIds) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.c_str());
		if (!source)
			continue;
		auto *strip = new AudioStrip(source, this);
		stripsLayout->insertWidget(stripsLayout->count() - 1, strip);
		strips.push_back(strip);
	}
}
