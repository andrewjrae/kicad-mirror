/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 KiCad Developers, see AUTHORS.txt for contributors.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ALTIUM_LIB_PLUGIN_CACHE_
#define _ALTIUM_LIB_PLUGIN_CACHE_

#include "../sch_lib_plugin_cache.h"

class FILE_LINE_READER;
class SCH_SEXPR_PLUGIN;

/**
 * A cache assistant for Altium symbol libraries.
 */
class ALTIUM_LIB_PLUGIN_CACHE : public SCH_LIB_PLUGIN_CACHE
{
public:
    ALTIUM_LIB_PLUGIN_CACHE( const wxString& aLibraryPath );
    virtual ~ALTIUM_LIB_PLUGIN_CACHE();

    void Load() override;

private:
    friend SCH_SEXPR_PLUGIN;

};

#endif    // _ALTIUM_LIB_PLUGIN_CACHE_
