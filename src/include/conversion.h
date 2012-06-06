/* Copyright 2012, Guilherme P. Gonçalves (guilherme.p.gonc@gmail.com)
 *
 * This file is part of ipod-syncer.
 *
 * ipod-syncer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ipod-syncer.  If not, see <http://www.gnu.org/licenses/>.
 */

#define SCRIPTDIR "scripts/"

gboolean is_mp3 (const gchar *filepath);
gchar *convert_to_mp3 (gchar *filepath, GError **err);
