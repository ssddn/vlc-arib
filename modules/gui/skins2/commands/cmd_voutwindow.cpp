/*****************************************************************************
 * cmd_voutwindow.cpp
 *****************************************************************************
 * Copyright (C) 2009 the VideoLAN team
 * $Id$
 *
 * Author: Erwan Tulou      <erwan10 aT videolan doT org >
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "cmd_voutwindow.hpp"
#include "../src/vout_manager.hpp"
#include "../src/vout_window.hpp"


CmdNewVoutWindow::CmdNewVoutWindow( intf_thread_t *pIntf, vout_window_t* pWnd )
   : CmdGeneric( pIntf ), m_pWnd( pWnd )
{
}


void CmdNewVoutWindow::execute()
{
    vlc_mutex_lock( &getIntf()->p_sys->vout_lock );

    VoutManager* p_VoutManager = getIntf()->p_sys->p_voutManager;

    void* handle = p_VoutManager->acceptWnd( m_pWnd );

    getIntf()->p_sys->handle = handle;
    getIntf()->p_sys->b_vout_ready = true;
    vlc_cond_signal( &getIntf()->p_sys->vout_wait );

    vlc_mutex_unlock( &getIntf()->p_sys->vout_lock );
}


CmdReleaseVoutWindow::CmdReleaseVoutWindow( intf_thread_t *pIntf,
                                            vout_window_t* pWnd )
   : CmdGeneric( pIntf ), m_pWnd( pWnd )
{
}


void CmdReleaseVoutWindow::execute()
{
    vlc_mutex_lock( &getIntf()->p_sys->vout_lock );

    VoutManager* p_VoutManager = getIntf()->p_sys->p_voutManager;

    p_VoutManager->releaseWnd( m_pWnd );

    getIntf()->p_sys->b_vout_ready = true;
    vlc_cond_signal( &getIntf()->p_sys->vout_wait );

    vlc_mutex_unlock( &getIntf()->p_sys->vout_lock );
}


