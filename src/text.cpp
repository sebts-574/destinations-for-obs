/*
Multidestination Studio
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

#include "text.hpp"
#include "default-strings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <cstring>

QString Tr(const char *key)
{
	const char *out = nullptr;
	if (obs_module_get_string(key, &out) && out)
		return QString::fromUtf8(out);

	static bool warned = false;
	if (!warned) {
		warned = true;
		obs_log(LOG_WARNING, "fichiers de langue introuvables, textes anglais intégrés utilisés");
	}

	for (const auto &entry : DEFAULT_STRINGS) {
		if (strcmp(entry.key, key) == 0)
			return QString::fromUtf8(entry.value);
	}
	return QString::fromUtf8(key);
}
