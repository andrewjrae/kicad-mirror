/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 Mikolaj Wielgus
 * Copyright (C) 2022 KiCad Developers, see AUTHORS.txt for contributors.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * https://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SIM_MODEL_SPICE_H
#define SIM_MODEL_SPICE_H

#include <sim/sim_model.h>
#include <sim/spice_generator.h>


class SPICE_GENERATOR_SPICE : public SPICE_GENERATOR
{
    using SPICE_GENERATOR::SPICE_GENERATOR;

    wxString Preview( const wxString& aModelName ) const override;
};


class SIM_MODEL_SPICE : public SIM_MODEL
{
public:
    friend class SPICE_GENERATOR_SPICE;

    static TYPE ReadTypeFromSpiceCode( const wxString& aSpiceCode );
    static std::unique_ptr<SIM_MODEL_SPICE> Create( const wxString& aSpiceCode );
    
    virtual void ReadSpiceCode( const wxString& aSpiceCode );

protected:
    using SIM_MODEL::SIM_MODEL;

    bool SetParamValue( unsigned aParamIndex, const wxString& aParamValue,
                        SIM_VALUE_GRAMMAR::NOTATION aNotation
                            = SIM_VALUE_GRAMMAR::NOTATION::SI ) override;

    virtual bool SetParamFromSpiceCode( const wxString& aParamName, const wxString& aParamValue,
                                        SIM_VALUE_GRAMMAR::NOTATION aNotation
                                            = SIM_VALUE_GRAMMAR::NOTATION::SPICE );

    wxString m_spiceCode;

private:
    static TYPE readTypeFromSpiceStrings( const wxString& aTypeString,
                                          const wxString& aLevel = "",
                                          const wxString& aVersion = "",
                                          bool aSkipDefaultLevel = true );
};

#endif // SIM_MODEL_SPICE_H
