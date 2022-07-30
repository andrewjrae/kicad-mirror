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

#ifndef SIM_MODEL_TLINE_H
#define SIM_MODEL_TLINE_H

#include <sim/sim_model.h>


class SIM_MODEL_TLINE : public SIM_MODEL
{
public:
    SIM_MODEL_TLINE( TYPE aType );

    void ReadDataSchFields( unsigned aSymbolPinCount,
                            const std::vector<SCH_FIELD>* aFields ) override;
    void ReadDataLibFields( unsigned aSymbolPinCount,
                            const std::vector<LIB_FIELD>* aFields ) override;

    void WriteDataSchFields( std::vector<SCH_FIELD>& aFields ) const override;
    void WriteDataLibFields( std::vector<LIB_FIELD>& aFields ) const override;

private:
    template <typename T>
    void inferredReadDataFields( unsigned aSymbolPinCount, const std::vector<T>* aFields );

    template <typename T>
    void inferredWriteDataFields( std::vector<T>& aFields ) const;

    static std::vector<PARAM::INFO> makeZ0ParamInfo();
    static std::vector<PARAM::INFO> makeRlgcParamInfo();

    std::vector<wxString> getPinNames() const override { return { "1+", "1-", "2+", "2-" }; }

    // Subcircuits require models even when they have no Spice instance parameters.
    bool requiresSpiceModel() const override;

    bool m_isInferred;
};

#endif // SIM_MODEL_TLINE_H