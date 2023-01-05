/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 Mark Roszko <mark.roszko@gmail.com>
 * Copyright (C) 1992-2022 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "command_version.h"
#include <cli/exit_codes.h>
#include <wx/crt.h>
#include <build_version.h>

#include <macros.h>
#include <build_version.h>

#define ARG_FORMAT "--format"

CLI::VERSION_COMMAND::VERSION_COMMAND() : COMMAND( "version" )
{
    m_argParser.add_argument( ARG_FORMAT )
            .default_value( std::string( "plain" ) )
            .help( UTF8STDSTR( _( "version info format (plain, commit, about)" ) ) );
}


int CLI::VERSION_COMMAND::doPerform( KIWAY& aKiway )
{
    wxString format = FROM_UTF8( m_argParser.get<std::string>( ARG_FORMAT ).c_str() );
    if( format == wxS( "plain" ) )
    {
        wxPrintf( GetMajorMinorPatchVersion() );
    }
    else if( format == wxS( "commit" ) )
    {
        wxPrintf( GetCommitHash() );
    }
    else if( format == wxS( "about" ) )
    {
        wxString msg_version = GetVersionInfoData( wxS( "kicad-cli" ) );
        wxPrintf( msg_version );
    }
    else
    {
        wxFprintf( stderr, _( "Invalid format\n" ) );
        return EXIT_CODES::ERR_ARGS;
    }

    return 0;
}