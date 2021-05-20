/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019-2020 KiCad Developers, see AUTHORS.txt for contributors.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


#include <cstdio>
#include <memory>
#include <board.h>
#include <track.h>
#include <geometry/shape_segment.h>
#include <pcb_expr_evaluator.h>

#include <connectivity/connectivity_data.h>
#include <connectivity/connectivity_algo.h>
#include <connectivity/from_to_cache.h>

#include <drc/drc_engine.h>
#include <geometry/shape_circle.h>

bool exprFromTo( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    LIBEVAL::VALUE*   argTo = aCtx->Pop();
    LIBEVAL::VALUE*   argFrom = aCtx->Pop();

    result->Set(0.0);
    aCtx->Push( result );

    if(!item)
        return false;

    auto ftCache = item->GetBoard()->GetConnectivity()->GetFromToCache();

    if( !ftCache )
    {
        wxLogWarning( "Attempting to call fromTo() with non-existent from-to cache, aborting...");
        return true;
    }

    if( ftCache->IsOnFromToPath( static_cast<BOARD_CONNECTED_ITEM*>( item ),
        argFrom->AsString(), argTo->AsString() ) )
    {
        result->Set(1.0);
    }

    return true;
}


static void existsOnLayer( LIBEVAL::CONTEXT* aCtx, void *self )
{
    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    LIBEVAL::VALUE*   arg = aCtx->Pop();
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !item )
        return;

    if( !arg && aCtx->HasErrorCallback() )
    {
        aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                             wxT( "existsOnLayer()" ) ) );
        return;
    }

    wxString     layerName = arg->AsString();
    wxPGChoices& layerMap = ENUM_MAP<PCB_LAYER_ID>::Instance().Choices();
    bool         anyMatch = false;

    for( unsigned ii = 0; ii < layerMap.GetCount(); ++ii )
    {
        wxPGChoiceEntry& entry = layerMap[ii];

        if( entry.GetText().Matches( layerName ) )
        {
            anyMatch = true;

            if( item->IsOnLayer( ToLAYER_ID( entry.GetValue() ) ) )
            {
                result->Set( 1.0 );
                return;
            }
        }
    }

    if( !anyMatch && aCtx->HasErrorCallback() )
        aCtx->ReportError( wxString::Format( _( "Unrecognized layer '%s'" ), layerName ) );
}


static void isPlated( LIBEVAL::CONTEXT* aCtx, void* self )
{
    LIBEVAL::VALUE* result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    if( item->Type() == PCB_PAD_T && static_cast<PAD*>( item )->GetAttribute() == PAD_ATTRIB::PTH )
        result->Set( 1.0 );
    else if( item->Type() == PCB_VIA_T )
        result->Set( 1.0 );
}


static bool insideFootprintCourtyard( BOARD_ITEM* aItem, const EDA_RECT& aItemBBox,
                                      std::shared_ptr<SHAPE> aItemShape, PCB_EXPR_CONTEXT* aCtx,
                                      FOOTPRINT* aFootprint, PCB_LAYER_ID aSide = UNDEFINED_LAYER )
{
    SHAPE_POLY_SET footprintCourtyard;

    if( aSide == F_Cu )
        footprintCourtyard = aFootprint->GetPolyCourtyardFront();
    else if( aSide == B_Cu )
        footprintCourtyard = aFootprint->GetPolyCourtyardBack();
    else if( aFootprint->IsFlipped() )
        footprintCourtyard = aFootprint->GetPolyCourtyardBack();
    else
        footprintCourtyard = aFootprint->GetPolyCourtyardFront();

    if( aItem->Type() == PCB_ZONE_T || aItem->Type() == PCB_FP_ZONE_T )
    {
        // A zone must be entirely inside the courtyard to be considered
        if( !aFootprint->GetBoundingBox().Contains( aItemBBox ) )
            return false;
    }
    else
    {
        if( !aFootprint->GetBoundingBox().Intersects( aItemBBox ) )
            return false;
    }

    if( !aItemShape )
        aItemShape = aItem->GetEffectiveShape( aCtx->GetLayer() );

    return footprintCourtyard.Collide( aItemShape.get() );
};


static void insideCourtyard( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_CONTEXT* context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
    LIBEVAL::VALUE*   arg = aCtx->Pop();
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "insideCourtyard()" ) ) );
        }

        return;
    }

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    BOARD*                 board = item->GetBoard();
    EDA_RECT               itemBBox;
    std::shared_ptr<SHAPE> shape;

    if( item->Type() == PCB_ZONE_T || item->Type() == PCB_FP_ZONE_T )
        itemBBox = static_cast<ZONE*>( item )->GetCachedBoundingBox();
    else
        itemBBox = item->GetBoundingBox();

    auto insideFootprint =
            [&]( FOOTPRINT* footprint ) -> bool
            {
                if( !footprint )
                    return false;

                std::unique_lock<std::mutex>        cacheLock( board->m_CachesMutex );
                std::pair<BOARD_ITEM*, BOARD_ITEM*> key( footprint, item );
                auto                                i = board->m_InsideCourtyardCache.find( key );

                if( i != board->m_InsideCourtyardCache.end() )
                    return i->second;

                bool res = insideFootprintCourtyard( item, itemBBox, shape, context, footprint );

                board->m_InsideCourtyardCache[ key ] = res;
                return res;
            };

    if( arg->AsString() == "A" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 0 ) ) ) )
            result->Set( 1.0 );
    }
    else if( arg->AsString() == "B" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 1 ) ) ) )
            result->Set( 1.0 );
    }
    else
    {
        for( FOOTPRINT* candidate : board->Footprints() )
        {
            if( candidate->GetReference().Matches( arg->AsString() ) )
            {
                if( insideFootprint( candidate ) )
                {
                    result->Set( 1.0 );
                    return;
                }
            }
        }
    }
}


static void insideFrontCourtyard( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_CONTEXT* context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
    LIBEVAL::VALUE*   arg = aCtx->Pop();
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "insideFrontCourtyard()" ) ) );
        }

        return;
    }

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    BOARD*                 board = item->GetBoard();
    EDA_RECT               itemBBox;
    std::shared_ptr<SHAPE> shape;

    if( item->Type() == PCB_ZONE_T || item->Type() == PCB_FP_ZONE_T )
        itemBBox = static_cast<ZONE*>( item )->GetCachedBoundingBox();
    else
        itemBBox = item->GetBoundingBox();

    auto insideFootprint =
            [&]( FOOTPRINT* footprint ) -> bool
            {
                if( !footprint )
                    return false;

                std::unique_lock<std::mutex>        cacheLock( board->m_CachesMutex );
                std::pair<BOARD_ITEM*, BOARD_ITEM*> key( footprint, item );
                auto                                i = board->m_InsideFCourtyardCache.find( key );

                if( i != board->m_InsideFCourtyardCache.end() )
                    return i->second;

                bool res = insideFootprintCourtyard( item, itemBBox, shape, context, footprint,
                                                        F_Cu );

                board->m_InsideFCourtyardCache[ key ] = res;
                return res;
            };

    if( arg->AsString() == "A" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 0 ) ) ) )
            result->Set( 1.0 );
    }
    else if( arg->AsString() == "B" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 1 ) ) ) )
            result->Set( 1.0 );
    }
    else
    {
        for( FOOTPRINT* candidate : board->Footprints() )
        {
            if( candidate->GetReference().Matches( arg->AsString() ) )
            {
                if( insideFootprint( candidate ) )
                {
                    result->Set( 1.0 );
                    return;
                }
            }
        }
    }
}


static void insideBackCourtyard( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_CONTEXT* context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
    LIBEVAL::VALUE*   arg = aCtx->Pop();
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "insideBackCourtyard()" ) ) );
        }
        return;
    }

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    BOARD*                 board = item->GetBoard();
    EDA_RECT               itemBBox;
    std::shared_ptr<SHAPE> shape;

    if( item->Type() == PCB_ZONE_T || item->Type() == PCB_FP_ZONE_T )
        itemBBox = static_cast<ZONE*>( item )->GetCachedBoundingBox();
    else
        itemBBox = item->GetBoundingBox();

    auto insideFootprint =
            [&]( FOOTPRINT* footprint ) -> bool
            {
                if( !footprint )
                    return false;

                std::unique_lock<std::mutex>        cacheLock( board->m_CachesMutex );
                std::pair<BOARD_ITEM*, BOARD_ITEM*> key( footprint, item );
                auto                                i = board->m_InsideBCourtyardCache.find( key );

                if( i != board->m_InsideBCourtyardCache.end() )
                    return i->second;

                bool res = insideFootprintCourtyard( item, itemBBox, shape, context, footprint,
                                                        B_Cu );

                board->m_InsideBCourtyardCache[ key ] = res;
                return res;
            };

    if( arg->AsString() == "A" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 0 ) ) ) )
            result->Set( 1.0 );
    }
    else if( arg->AsString() == "B" )
    {
        if( insideFootprint( dynamic_cast<FOOTPRINT*>( context->GetItem( 1 ) ) ) )
            result->Set( 1.0 );
    }
    else
    {
        for( FOOTPRINT* candidate : board->Footprints() )
        {
            if( candidate->GetReference().Matches( arg->AsString() ) )
            {
                if( insideFootprint( candidate ) )
                {
                    result->Set( 1.0 );
                    return;
                }
            }
        }
    }
}


static void insideArea( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_CONTEXT* context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
    LIBEVAL::VALUE*   arg = aCtx->Pop();
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "insideArea()" ) ) );
        }

        return;
    }

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    BOARD*                 board = item->GetBoard();
    EDA_RECT               itemBBox;
    std::shared_ptr<SHAPE> shape;

    if( item->Type() == PCB_ZONE_T || item->Type() == PCB_FP_ZONE_T )
        itemBBox = static_cast<ZONE*>( item )->GetCachedBoundingBox();
    else
        itemBBox = item->GetBoundingBox();

    auto realInsideZone =
            [&]( ZONE* zone ) -> bool
            {
                if( !zone->GetCachedBoundingBox().Intersects( itemBBox ) )
                    return false;

                // Collisions include touching, so we need to deflate outline by enough to
                // exclude touching.  This is particularly important for detecting copper fills
                // as they will be exactly touching along the entire border.
                SHAPE_POLY_SET zoneOutline = *zone->Outline();
                zoneOutline.Deflate( Millimeter2iu( 0.001 ), 4 );

                if( item->GetFlags() & HOLE_PROXY )
                {
                    if( item->Type() == PCB_PAD_T )
                    {
                        PAD*                 pad = static_cast<PAD*>( item );
                        const SHAPE_SEGMENT* holeShape = pad->GetEffectiveHoleShape();

                        return zoneOutline.Collide( holeShape );
                    }
                    else if( item->Type() == PCB_VIA_T )
                    {
                        VIA*               via = static_cast<VIA*>( item );
                        const SHAPE_CIRCLE holeShape( via->GetPosition(), via->GetDrillValue() );

                        return zoneOutline.Collide( &holeShape );
                    }

                    return false;
                }

                if( item->Type() == PCB_FOOTPRINT_T )
                {
                    FOOTPRINT* footprint = static_cast<FOOTPRINT*>( item );

                    if( ( footprint->GetFlags() & MALFORMED_COURTYARDS ) != 0 )
                    {
                        if( aCtx->HasErrorCallback() )
                            aCtx->ReportError( _( "Footprint's courtyard is not a single, closed shape." ) );

                        return false;
                    }

                    if( ( zone->GetLayerSet() & LSET::FrontMask() ).any() )
                    {
                        SHAPE_POLY_SET courtyard = footprint->GetPolyCourtyardFront();

                        if( courtyard.OutlineCount() == 0 )
                        {
                            if( aCtx->HasErrorCallback() )
                                aCtx->ReportError( _( "Footprint has no front courtyard." ) );

                            return false;
                        }
                        else
                        {
                            return zoneOutline.Collide( &courtyard.Outline( 0 ) );
                        }
                    }

                    if( ( zone->GetLayerSet() & LSET::BackMask() ).any() )
                    {
                        SHAPE_POLY_SET courtyard = footprint->GetPolyCourtyardBack();

                        if( courtyard.OutlineCount() == 0 )
                        {
                            if( aCtx->HasErrorCallback() )
                                aCtx->ReportError( _( "Footprint has no back courtyard." ) );

                            return false;
                        }
                        else
                        {
                            return zoneOutline.Collide( &courtyard.Outline( 0 ) );
                        }
                    }

                    return false;
                }

                if( item->Type() == PCB_ZONE_T || item->Type() == PCB_FP_ZONE_T )
                {
                    ZONE* itemZone = static_cast<ZONE*>( item );

                    if( !itemZone->IsFilled() )
                        return false;

                    DRC_RTREE* itemRTree = board->m_CopperZoneRTrees[ itemZone ].get();

                    if( itemRTree )
                    {
                        for( PCB_LAYER_ID layer : zone->GetLayerSet().Seq() )
                        {
                            if( itemRTree->QueryColliding( itemBBox,  &zoneOutline, layer, 0 ) )
                                return true;
                        }
                    }

                    return false;
                }
                else
                {
                    if( !shape )
                        shape = item->GetEffectiveShape( context->GetLayer() );

                    return zoneOutline.Collide( shape.get() );
                }
            };

    auto insideZone =
            [&]( ZONE* zone ) -> bool
            {
                if( !zone || zone == item || zone->GetParent() == item )
                    return false;

                std::unique_lock<std::mutex>        cacheLock( board->m_CachesMutex );
                std::pair<BOARD_ITEM*, BOARD_ITEM*> key( zone, item );
                auto                                i = board->m_InsideAreaCache.find( key );

                if( i != board->m_InsideAreaCache.end() )
                    return i->second;

                bool isInside = realInsideZone( zone );

                board->m_InsideAreaCache[ key ] = isInside;
                return isInside;
            };

    if( arg->AsString() == "A" )
    {
        if( insideZone( dynamic_cast<ZONE*>( context->GetItem( 0 ) ) ) )
            result->Set( 1.0 );
    }
    else if( arg->AsString() == "B" )
    {
        if( insideZone( dynamic_cast<ZONE*>( context->GetItem( 1 ) ) ) )
            result->Set( 1.0 );
    }
    else if( KIID::SniffTest( arg->AsString() ) )
    {
        KIID target( arg->AsString() );

        for( ZONE* candidate : board->Zones() )
        {
            // Only a single zone can match the UUID; exit once we find a match whether
            // "inside" or not
            if( candidate->m_Uuid == target )
            {
                if( insideZone( candidate ) )
                    result->Set( 1.0 );

                return;
            }
        }

        for( FOOTPRINT* footprint : board->Footprints() )
        {
            for( ZONE* candidate : footprint->Zones() )
            {
                // Only a single zone can match the UUID; exit once we find a match whether
                // "inside" or not
                if( candidate->m_Uuid == target )
                {
                    if( insideZone( candidate ) )
                        result->Set( 1.0 );

                    return;
                }
            }
        }
    }
    else  // Match on zone name
    {
        for( ZONE* candidate : board->Zones() )
        {
            if( candidate->GetZoneName().Matches( arg->AsString() ) )
            {
                // Many zones can match the name; exit only when we find an "inside"
                if( insideZone( candidate ) )
                {
                    result->Set( 1.0 );
                    return;
                }
            }
        }

        for( FOOTPRINT* footprint : board->Footprints() )
        {
            for( ZONE* candidate : footprint->Zones() )
            {
                // Many zones can match the name; exit only when we find an "inside"
                if( candidate->GetZoneName().Matches( arg->AsString() ) )
                {
                    if( insideZone( candidate ) )
                    {
                        result->Set( 1.0 );
                        return;
                    }
                }
            }
        }
    }
}


static void memberOf( LIBEVAL::CONTEXT* aCtx, void* self )
{
    LIBEVAL::VALUE* arg = aCtx->Pop();
    LIBEVAL::VALUE* result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "memberOf()" ) ) );
        }
        return;
    }

    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;

    if( !item )
        return;

    PCB_GROUP* group = item->GetParentGroup();

    if( !group && item->GetParent() && item->GetParent()->Type() == PCB_FOOTPRINT_T )
        group = item->GetParent()->GetParentGroup();

    while( group )
    {
        if( group->GetName().Matches( arg->AsString() ) )
        {
            result->Set( 1.0 );
            return;
        }

        group = group->GetParentGroup();
    }
}


static void isMicroVia( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    auto via = dyn_cast<VIA*>( item );

    if( via && via->GetViaType() == VIATYPE::MICROVIA )
    {
        result->Set ( 1.0 );
    }
}


static void isBlindBuriedVia( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_VAR_REF* vref = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item = vref ? vref->GetObject( aCtx ) : nullptr;
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    auto via = dyn_cast<VIA*>( item );

    if( via && via->GetViaType() == VIATYPE::BLIND_BURIED )
    {
        result->Set ( 1.0 );
    }
}


static void isCoupledDiffPair( LIBEVAL::CONTEXT* aCtx, void* self )
{
    PCB_EXPR_CONTEXT*     context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
    BOARD_CONNECTED_ITEM* a = dynamic_cast<BOARD_CONNECTED_ITEM*>( context->GetItem( 0 ) );
    BOARD_CONNECTED_ITEM* b = dynamic_cast<BOARD_CONNECTED_ITEM*>( context->GetItem( 1 ) );
    LIBEVAL::VALUE*       result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( a && b )
    {
        NETINFO_ITEM* netinfo = a->GetNet();
        wxString      coupledNet, dummy;

        if( netinfo && DRC_ENGINE::MatchDpSuffix( netinfo->GetNetname(), coupledNet, dummy ) != 0 )
        {
            if( b->GetNetname() == coupledNet )
                result->Set( 1.0 );
        }
    }
}


static void inDiffPair( LIBEVAL::CONTEXT* aCtx, void* self )
{
    LIBEVAL::VALUE*   arg    = aCtx->Pop();
    PCB_EXPR_VAR_REF* vref   = static_cast<PCB_EXPR_VAR_REF*>( self );
    BOARD_ITEM*       item   = vref ? vref->GetObject( aCtx ) : nullptr;
    LIBEVAL::VALUE*   result = aCtx->AllocValue();

    result->Set( 0.0 );
    aCtx->Push( result );

    if( !arg )
    {
        if( aCtx->HasErrorCallback() )
        {
            aCtx->ReportError( wxString::Format( _( "Missing argument to '%s'" ),
                                                 wxT( "inDiffPair()" ) ) );
        }

        return;
    }

    if( item && item->IsConnected() )
    {
        NETINFO_ITEM* netinfo = static_cast<BOARD_CONNECTED_ITEM*>( item )->GetNet();

        wxString refName = netinfo->GetNetname();
        wxString baseName, coupledNet;

        int polarity = DRC_ENGINE::MatchDpSuffix( refName, coupledNet, baseName );

        if( polarity == 0 )
            return;

        if( BOARD* board = item->GetBoard() )
        {
            if( !board->FindNet( coupledNet ) )
                return;
        }

        if( baseName.Matches( arg->AsString() ) )
            result->Set( 1.0 );
    }
}


PCB_EXPR_BUILTIN_FUNCTIONS::PCB_EXPR_BUILTIN_FUNCTIONS()
{
    RegisterAllFunctions();
}


void PCB_EXPR_BUILTIN_FUNCTIONS::RegisterAllFunctions()
{
    m_funcs.clear();
    RegisterFunc( "existsOnLayer('x')", existsOnLayer );
    RegisterFunc( "isPlated()", isPlated );
    RegisterFunc( "insideCourtyard('x')", insideCourtyard );
    RegisterFunc( "insideFrontCourtyard('x')", insideFrontCourtyard );
    RegisterFunc( "insideBackCourtyard('x')", insideBackCourtyard );
    RegisterFunc( "insideArea('x')", insideArea );
    RegisterFunc( "isMicroVia()", isMicroVia );
    RegisterFunc( "isBlindBuriedVia()", isBlindBuriedVia );
    RegisterFunc( "memberOf('x')", memberOf );
    RegisterFunc( "fromTo('x','y')", exprFromTo );
    RegisterFunc( "isCoupledDiffPair()", isCoupledDiffPair );
    RegisterFunc( "inDiffPair('x')", inDiffPair );
}


BOARD_ITEM* PCB_EXPR_VAR_REF::GetObject( const LIBEVAL::CONTEXT* aCtx ) const
{
    wxASSERT( dynamic_cast<const PCB_EXPR_CONTEXT*>( aCtx ) );

    const PCB_EXPR_CONTEXT* ctx = static_cast<const PCB_EXPR_CONTEXT*>( aCtx );
    BOARD_ITEM*             item  = ctx->GetItem( m_itemIndex );
    return item;
}


class PCB_LAYER_VALUE : public LIBEVAL::VALUE
{
public:
    PCB_LAYER_VALUE( PCB_LAYER_ID aLayer ) :
        LIBEVAL::VALUE( double( aLayer ) )
    {};

    virtual bool EqualTo( const VALUE* b ) const override
    {
        // For boards with user-defined layer names there will be 2 entries for each layer
        // in the ENUM_MAP: one for the canonical layer name and one for the user layer name.
        // We need to check against both.

        wxPGChoices& layerMap = ENUM_MAP<PCB_LAYER_ID>::Instance().Choices();
        PCB_LAYER_ID layerId = ToLAYER_ID( (int) AsDouble() );

        for( unsigned ii = 0; ii < layerMap.GetCount(); ++ii )
        {
            wxPGChoiceEntry& entry = layerMap[ii];

            if( entry.GetValue() == layerId && entry.GetText().Matches( b->AsString() ) )
                return true;
        }

        return false;
    }
};


LIBEVAL::VALUE PCB_EXPR_VAR_REF::GetValue( LIBEVAL::CONTEXT* aCtx )
{
    if( m_itemIndex == 2 )
    {
        PCB_EXPR_CONTEXT* context = static_cast<PCB_EXPR_CONTEXT*>( aCtx );
        return PCB_LAYER_VALUE( context->GetLayer() );
    }

    BOARD_ITEM* item = GetObject( aCtx );

    if( !item )
        return LIBEVAL::VALUE();

    auto it = m_matchingTypes.find( TYPE_HASH( *item ) );

    if( it == m_matchingTypes.end() )
    {
        // Don't force user to type "A.Type == 'via' && A.Via_Type == 'buried'" when the
        // simplier "A.Via_Type == 'buried'" is perfectly clear.  Instead, return an undefined
        // value when the property doesn't appear on a particular object.

        return LIBEVAL::VALUE();
    }
    else
    {
        if( m_type == LIBEVAL::VT_NUMERIC )
            return LIBEVAL::VALUE( (double) item->Get<int>( it->second ) );
        else
        {
            wxString str;

            if( !m_isEnum )
            {
                str = item->Get<wxString>( it->second );
                return LIBEVAL::VALUE( str );
            }
            else
            {
                const wxAny& any = item->Get( it->second );
                bool valid = any.GetAs<wxString>( &str );

                if( valid )
                    return LIBEVAL::VALUE( str );
            }

            return LIBEVAL::VALUE();
        }
    }
}


LIBEVAL::VALUE PCB_EXPR_NETCLASS_REF::GetValue( LIBEVAL::CONTEXT* aCtx )
{
    BOARD_ITEM* item = GetObject( aCtx );

    if( !item )
        return LIBEVAL::VALUE();

    if( item->IsConnected() )
        return LIBEVAL::VALUE( static_cast<BOARD_CONNECTED_ITEM*>( item )->GetNetClassName() );
    else
        return LIBEVAL::VALUE();
}


LIBEVAL::VALUE PCB_EXPR_NETNAME_REF::GetValue( LIBEVAL::CONTEXT* aCtx )
{
    BOARD_ITEM* item = GetObject( aCtx );

    if( !item )
        return LIBEVAL::VALUE();

    if( item->IsConnected() )
        return LIBEVAL::VALUE( static_cast<BOARD_CONNECTED_ITEM*>( item )->GetNetname() );
    else
        return LIBEVAL::VALUE();
}


LIBEVAL::FUNC_CALL_REF PCB_EXPR_UCODE::CreateFuncCall( const wxString& aName )
{
    PCB_EXPR_BUILTIN_FUNCTIONS& registry = PCB_EXPR_BUILTIN_FUNCTIONS::Instance();

    return registry.Get( aName.Lower() );
}


std::unique_ptr<LIBEVAL::VAR_REF> PCB_EXPR_UCODE::CreateVarRef( const wxString& aVar,
                                                                const wxString& aField )
{
    PROPERTY_MANAGER& propMgr = PROPERTY_MANAGER::Instance();
    std::unique_ptr<PCB_EXPR_VAR_REF> vref;

    // Check for a couple of very common cases and compile them straight to "object code".

    if( aField.CmpNoCase( "NetClass" ) == 0 )
    {
        if( aVar == "A" )
            return std::make_unique<PCB_EXPR_NETCLASS_REF>( 0 );
        else if( aVar == "B" )
            return std::make_unique<PCB_EXPR_NETCLASS_REF>( 1 );
        else
            return nullptr;
    }
    else if( aField.CmpNoCase( "NetName" ) == 0 )
    {
        if( aVar == "A" )
            return std::make_unique<PCB_EXPR_NETNAME_REF>( 0 );
        else if( aVar == "B" )
            return std::make_unique<PCB_EXPR_NETNAME_REF>( 1 );
        else
            return nullptr;
    }

    if( aVar == "A" || aVar == "AB" )
        vref = std::make_unique<PCB_EXPR_VAR_REF>( 0 );
    else if( aVar == "B" )
        vref = std::make_unique<PCB_EXPR_VAR_REF>( 1 );
    else if( aVar == "L" )
        vref = std::make_unique<PCB_EXPR_VAR_REF>( 2 );
    else
        return nullptr;

    if( aField.length() == 0 ) // return reference to base object
    {
        return std::move( vref );
    }

    wxString field( aField );
    field.Replace( "_",  " " );

    for( const PROPERTY_MANAGER::CLASS_INFO& cls : propMgr.GetAllClasses() )
    {
        if( propMgr.IsOfType( cls.type, TYPE_HASH( BOARD_ITEM ) ) )
        {
            PROPERTY_BASE* prop = propMgr.GetProperty( cls.type, field );

            if( prop )
            {
                vref->AddAllowedClass( cls.type, prop );

                if( prop->TypeHash() == TYPE_HASH( int ) )
                {
                    vref->SetType( LIBEVAL::VT_NUMERIC );
                }
                else if( prop->TypeHash() == TYPE_HASH( wxString ) )
                {
                    vref->SetType( LIBEVAL::VT_STRING );
                }
                else if ( prop->HasChoices() )
                {   // it's an enum, we treat it as string
                    vref->SetType( LIBEVAL::VT_STRING );
                    vref->SetIsEnum ( true );
                }
                else
                {
                    wxFAIL_MSG( "PCB_EXPR_UCODE::createVarRef: Unknown property type." );
                }
            }
        }
    }

    if( vref->GetType() == LIBEVAL::VT_UNDEFINED )
        vref->SetType( LIBEVAL::VT_PARSE_ERROR );

    return std::move( vref );
}


class PCB_UNIT_RESOLVER : public LIBEVAL::UNIT_RESOLVER
{
public:
    virtual ~PCB_UNIT_RESOLVER()
    {
    }

    virtual const std::vector<wxString>& GetSupportedUnits() const override
    {
        static const std::vector<wxString> pcbUnits = { "mil", "mm", "in" };

        return pcbUnits;
    }

    virtual wxString GetSupportedUnitsMessage() const override
    {
        return _( "must be mm, in, or mil" );
    }

    virtual double Convert( const wxString& aString, int unitId ) const override
    {
        double v = wxAtof( aString );

        switch( unitId )
        {
        case 0:  return DoubleValueFromString( EDA_UNITS::MILS, aString );
        case 1:  return DoubleValueFromString( EDA_UNITS::MILLIMETRES, aString );
        case 2:  return DoubleValueFromString( EDA_UNITS::INCHES, aString );
        default: return v;
        }
    };
};


PCB_EXPR_COMPILER::PCB_EXPR_COMPILER()
{
    m_unitResolver = std::make_unique<PCB_UNIT_RESOLVER>();
}


PCB_EXPR_EVALUATOR::PCB_EXPR_EVALUATOR()
{
    m_result = 0;
}

PCB_EXPR_EVALUATOR::~PCB_EXPR_EVALUATOR()
{
}


bool PCB_EXPR_EVALUATOR::Evaluate( const wxString& aExpr )
{
    PCB_EXPR_UCODE   ucode;
    PCB_EXPR_CONTEXT preflightContext( F_Cu );

    if( !m_compiler.Compile( aExpr.ToUTF8().data(), &ucode, &preflightContext ) )
        return false;

    PCB_EXPR_CONTEXT evaluationContext( F_Cu );
    LIBEVAL::VALUE*  result = ucode.Run( &evaluationContext );

    if( result->GetType() == LIBEVAL::VT_NUMERIC )
        m_result = KiROUND( result->AsDouble() );

    return true;
}

