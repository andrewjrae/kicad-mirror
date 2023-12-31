/**
 * @file invoke_pcb_dialog.h
 */

/* This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2013-2018 KiCad Developers, see AUTHORS.txt for contributors.
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

// This header is an insolation layer between top most frames and any number of
// DIALOG classes which can be called from a frame window.
// It is a place to put invocation functions for [modal] dialogs, with benefits:
//
// 1) The information about each dialog class is not exposed to the frame.
//    So therefore the DIALOG class can often be kept out of a header file entirely.
//
// 2) The information about the calling frame is not necessarily exposed to
//    to the called dialog class, at least not in here.

// The actual InvokeDialog<class>() function is usually coded at the bottom of the
// DIALOG_<class>.cpp file.


#ifndef INVOKE_A_DIALOG_H_
#define INVOKE_A_DIALOG_H_


class wxWindow;
class wxPoint;
class wxSize;
class wxString;

class BOARD;
class FOOTPRINT;

// Often this is not used in the prototypes, since wxFrame is good enough and would
// represent maximum information hiding.
class PCB_BASE_FRAME;
class PCB_EDIT_FRAME;
class FOOTPRINT_EDIT_FRAME;
class FP_LIB_TABLE;
class BOARD;
class PCB_PLOT_PARAMS;
class KIWAY;


/**
 * Function InvokePcbLibTableEditor
 * shows the modal DIALOG_FP_LIB_TABLE for purposes of editing the global and project
 * library tables.
 *
 * @param aCaller is the wxWindow which is invoking the dialog.
 * @return true if either table changed.
 */
void InvokePcbLibTableEditor( KIWAY* aKiway, wxWindow* aCaller );

/**
 * Function InvokeExportSVG
 * shows the Export SVG dialog
 * @param aCaller is the PCB_EDIT_FRAME which is invoking the dialog.
 * @param aBoard is the currently edited board.
 * @return bool - true if user pressed OK (did not abort), else false.
 */
bool InvokeExportSVG( PCB_EDIT_FRAME* aCaller, BOARD* aBoard );

#endif  // INVOKE_A_DIALOG_H_
