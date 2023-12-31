/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * Copyright (C) 2016-2023 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * @author Wayne Stambaugh <stambaughw@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
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

#include <string_utf8_map.h>
#include <unordered_set>

#include <ki_exception.h>
#include <sch_io_mgr.h>
#include <wx/translation.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>

#define FMT_UNIMPLEMENTED wxT( "Plugin \"%s\" does not implement the \"%s\" function." )
#define NOT_IMPLEMENTED( aCaller )                                                   \
    THROW_IO_ERROR( wxString::Format( FMT_UNIMPLEMENTED,                             \
                                      GetName().GetData(),                           \
                                      wxString::FromUTF8( aCaller ).GetData() ) );


const PLUGIN_FILE_DESC SCH_PLUGIN::GetSchematicFileDesc() const
{
    return PLUGIN_FILE_DESC( wxEmptyString, {} );
}


const PLUGIN_FILE_DESC SCH_PLUGIN::GetLibraryFileDesc() const
{
    return PLUGIN_FILE_DESC( wxEmptyString, {} );
}


bool SCH_PLUGIN::CanReadSchematicFile( const wxString& aFileName ) const
{
    const std::vector<std::string>& exts = GetSchematicFileDesc().m_FileExtensions;

    wxString fileExt = wxFileName( aFileName ).GetExt().MakeLower();

    for( const std::string& ext : exts )
    {
        if( fileExt == wxString( ext ).Lower() )
            return true;
    }

    return false;
}


bool SCH_PLUGIN::CanReadLibrary( const wxString& aFileName ) const
{
    const PLUGIN_FILE_DESC& desc = GetLibraryFileDesc();

    if( desc.m_IsFile )
    {
        const std::vector<std::string>& exts = desc.m_FileExtensions;

        wxString fileExt = wxFileName( aFileName ).GetExt().MakeLower();

        for( const std::string& ext : exts )
        {
            if( fileExt == wxString( ext ).Lower() )
                return true;
        }
    }
    else
    {
        wxDir dir( aFileName );

        if( !dir.IsOpened() )
            return false;

        std::vector<std::string>     exts = desc.m_ExtensionsInDir;
        std::unordered_set<wxString> lowerExts;

        for( const std::string& ext : exts )
            lowerExts.emplace( wxString( ext ).MakeLower() );

        wxString filenameStr;

        bool cont = dir.GetFirst( &filenameStr, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN );
        while( cont )
        {
            wxString ext = wxS( "" );

            int idx = filenameStr.Find( '.', true );
            if( idx != -1 )
                ext = filenameStr.Mid( idx + 1 ).MakeLower();

            if( lowerExts.count( ext ) )
                return true;

            cont = dir.GetNext( &filenameStr );
        }
    }

    return false;
}


void SCH_PLUGIN::SaveLibrary( const wxString& aFileName, const STRING_UTF8_MAP* aProperties )
{
    NOT_IMPLEMENTED( __FUNCTION__ );
}


SCH_SHEET* SCH_PLUGIN::LoadSchematicFile( const wxString& aFileName, SCHEMATIC* aSchematic,
                                      SCH_SHEET* aAppendToMe, const STRING_UTF8_MAP* aProperties )
{
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::SaveSchematicFile( const wxString& aFileName, SCH_SHEET* aSheet, SCHEMATIC* aSchematic,
                            const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::EnumerateSymbolLib( wxArrayString&    aAliasNameList,
                                     const wxString&   aLibraryPath,
                                     const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::EnumerateSymbolLib( std::vector<LIB_SYMBOL*>& aSymbolList,
                                     const wxString&   aLibraryPath,
                                     const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


LIB_SYMBOL* SCH_PLUGIN::LoadSymbol( const wxString& aLibraryPath, const wxString& aSymbolName,
                                    const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::SaveSymbol( const wxString& aLibraryPath, const LIB_SYMBOL* aSymbol,
                             const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::DeleteSymbol( const wxString& aLibraryPath, const wxString& aSymbolName,
                               const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::CreateSymbolLib( const wxString& aLibraryPath, const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


bool SCH_PLUGIN::DeleteSymbolLib( const wxString& aLibraryPath, const STRING_UTF8_MAP* aProperties )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


bool SCH_PLUGIN::IsSymbolLibWritable( const wxString& aLibraryPath )
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


void SCH_PLUGIN::SymbolLibOptions( STRING_UTF8_MAP* aListToAppendTo ) const
{
    // Empty for most plugins
    //
    // To add a new option override and use example code below:
    //
    //(*aListToAppendTo)["new_option_name"] = UTF8( _(
    //    "A nice descrtiption with possibility for <b>bold</b> and other formatting."
    //    ) );
}


const wxString& SCH_PLUGIN::GetError() const
{
    // not pure virtual so that plugins only have to implement subset of the SCH_PLUGIN interface.
    NOT_IMPLEMENTED( __FUNCTION__ );
}


bool SCH_PLUGIN::fileStartsWithPrefix( const wxString& aFilePath, const wxString& aPrefix,
                                   bool aIgnoreWhitespace )
{
    wxFFileInputStream input( aFilePath );

    if( input.IsOk() && !input.Eof() )
    {
        // Find first non-empty line
        wxTextInputStream text( input );
        wxString          line = text.ReadLine();

        if( aIgnoreWhitespace )
        {
            while( line.IsEmpty() )
                line = text.ReadLine().Trim( false /*trim from left*/ );
        }

        if( line.StartsWith( aPrefix ) )
            return true;
    }

    return false;
}


bool SCH_PLUGIN::fileStartsWithBinaryHeader( const wxString&             aFilePath,
                                         const std::vector<uint8_t>& aHeader )
{
    wxFFileInputStream input( aFilePath );

    if( input.IsOk() && !input.Eof() )
    {
        if( input.GetLength() < aHeader.size() )
            return false;

        std::vector<uint8_t> parsedHeader( aHeader.size() );
        if( !input.ReadAll( parsedHeader.data(), parsedHeader.size() ) )
            return false;

        return parsedHeader == aHeader;
    }

    return false;
}
