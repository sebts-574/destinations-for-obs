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

#pragma once

#include <obs.h>

#include <set>
#include <string>

class Destination;
class QWidget;

namespace Dialogs {

/* Nom et destinations couvertes d'une scène commune. isNew : scène qui
 * vient d'être créée (toutes les destinations cochées par défaut). */
bool EditScene(QWidget *parent, obs_source_t *scene, std::string &name, std::set<Destination *> &coverage, bool isNew);

struct DestinationSettings {
	std::string name;
	uint32_t width = 1920;
	uint32_t height = 1080;
	std::string transition = "fade_transition";
	int duration = 500;
	bool coverAll = true; /* création uniquement */
};

/* Création (dest == nullptr) ou réglages d'une destination. */
bool EditDestination(QWidget *parent, Destination *dest, DestinationSettings &settings);

} // namespace Dialogs
