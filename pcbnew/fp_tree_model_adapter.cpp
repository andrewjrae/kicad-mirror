/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018-2022 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <pgm_base.h>
#include <eda_base_frame.h>
#include <core/kicad_algo.h>
#include <settings/common_settings.h>
#include <project/project_file.h>
#include <wx/tokenzr.h>
#include <string_utils.h>
#include <eda_pattern_match.h>
#include <fp_lib_table.h>
#include <footprint_info.h>
#include <footprint_info_impl.h>
#include <generate_footprint_info.h>
#include <wx/settings.h>

#include "fp_tree_model_adapter.h"

wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER>
FP_TREE_MODEL_ADAPTER::Create( EDA_BASE_FRAME* aParent, LIB_TABLE* aLibs )
{
    auto* adapter = new FP_TREE_MODEL_ADAPTER( aParent, aLibs );
    return wxObjectDataPtr<LIB_TREE_MODEL_ADAPTER>( adapter );
}


FP_TREE_MODEL_ADAPTER::FP_TREE_MODEL_ADAPTER( EDA_BASE_FRAME* aParent, LIB_TABLE* aLibs ) :
        LIB_TREE_MODEL_ADAPTER( aParent, wxT( "pinned_footprint_libs" ) ),
        m_libs( (FP_LIB_TABLE*) aLibs )
{}


void FP_TREE_MODEL_ADAPTER::AddLibraries( EDA_BASE_FRAME* aParent )
{
    COMMON_SETTINGS* cfg = Pgm().GetCommonSettings();
    PROJECT_FILE&    project = aParent->Prj().GetProjectFile();

    for( const wxString& libName : m_libs->GetLogicalLibs() )
    {
        const FP_LIB_TABLE_ROW* library = m_libs->FindRow( libName, true );
        bool pinned = alg::contains( cfg->m_Session.pinned_fp_libs, libName )
                        || alg::contains( project.m_PinnedFootprintLibs, libName );

        DoAddLibrary( libName, library->GetDescr(), getFootprints( libName ), pinned, true );
    }

    m_tree.AssignIntrinsicRanks();
}


std::vector<LIB_TREE_ITEM*> FP_TREE_MODEL_ADAPTER::getFootprints( const wxString& aLibName )
{
    std::vector<LIB_TREE_ITEM*> libList;

    auto fullListStart = GFootprintList.GetList().begin();
    auto fullListEnd = GFootprintList.GetList().end();
    std::unique_ptr<FOOTPRINT_INFO> dummy = std::make_unique<FOOTPRINT_INFO_IMPL>( aLibName, wxEmptyString );

    // List is sorted, so use a binary search to find the range of footnotes for our library
    auto libBounds = std::equal_range( fullListStart, fullListEnd, dummy,
            []( const std::unique_ptr<FOOTPRINT_INFO>& a,
                const std::unique_ptr<FOOTPRINT_INFO>& b )
            {
                return StrNumCmp( a->GetLibNickname(), b->GetLibNickname(), false ) < 0;
            } );

    for( auto i = libBounds.first; i != libBounds.second; ++i )
        libList.push_back( i->get() );

    return libList;
}


wxString FP_TREE_MODEL_ADAPTER::GenerateInfo( LIB_ID const& aLibId, int aUnit )
{
    return GenerateFootprintInfo( m_libs, aLibId );
}


void FP_TREE_MODEL_ADAPTER::GetValue( wxVariant& aVariant, wxDataViewItem const& aItem,
                                      unsigned int aCol ) const
{
    if( IsFrozen() )
    {
        aVariant = wxEmptyString;
        return;
    }

    LIB_TREE_NODE* node = ToNode( aItem );
    wxASSERT( node );

    if( aCol == 0 && node->m_Pinned )
        aVariant = GetPinningSymbol() + node->m_Name;
    else
        aVariant = node->m_Name;

    switch( aCol )
    {
    case NAME_COL:
        if( node->m_Pinned )
            aVariant = GetPinningSymbol() + UnescapeString( node->m_Name );
        else
            aVariant = UnescapeString( node->m_Name );

        break;

    case DESC_COL:
        aVariant = node->m_Desc;
        break;
    }
}


