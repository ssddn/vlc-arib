/*****************************************************************************
 * utils.cpp: ActiveX control for VLC
 *****************************************************************************
 * Copyright (C) 2005 the VideoLAN team
 *
 * Authors: Damien Fouilleul <Damien.Fouilleul@laposte.net>
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

#include "utils.h"

/*
** conversion facilities
*/

using namespace std;

char *CStrFromBSTR(UINT codePage, BSTR bstr)
{
    UINT len = SysStringLen(bstr);
    if( len > 0 )
    {
        size_t mblen = WideCharToMultiByte(codePage,
                0, bstr, len, NULL, 0, NULL, NULL);
        if( mblen > 0 )
        {
            char *buffer = (char *)CoTaskMemAlloc(mblen+1);
            ZeroMemory(buffer, mblen+1);
            if( WideCharToMultiByte(codePage, 0, bstr, len, buffer, mblen, NULL, NULL) )
            {
                buffer[mblen] = '\0';
                return buffer;
            }
        }
    }
    return NULL;
};

BSTR BSTRFromCStr(UINT codePage, LPCSTR s)
{
    int wideLen = MultiByteToWideChar(codePage, 0, s, -1, NULL, 0);
    if( wideLen > 0 )
    {
        WCHAR* wideStr = (WCHAR*)CoTaskMemAlloc(wideLen*sizeof(WCHAR));
        if( NULL != wideStr )
        {
            BSTR bstr;

            ZeroMemory(wideStr, wideLen*sizeof(WCHAR));
            MultiByteToWideChar(codePage, 0, s, -1, wideStr, wideLen);
            bstr = SysAllocStringLen(wideStr, wideLen);
            CoTaskMemFree(wideStr);

            return bstr;
        }
    }
    return NULL;
};

char *CStrFromGUID(REFGUID clsid)
{
    LPOLESTR oleStr;

    if( FAILED(StringFromIID(clsid, &oleStr)) )
        return NULL;

#ifdef OLE2ANSI
    return (LPCSTR)oleStr;
#else
    char *pct_CLSID = NULL;
    size_t len = WideCharToMultiByte(CP_ACP, 0, oleStr, -1, NULL, 0, NULL, NULL);
    if( len > 0 )
    {
        pct_CLSID = (char *)CoTaskMemAlloc(len);
        WideCharToMultiByte(CP_ACP, 0, oleStr, -1, pct_CLSID, len, NULL, NULL);
    }
    CoTaskMemFree(oleStr);
    return pct_CLSID;
#endif
};

/*
**  properties
*/

HRESULT GetObjectProperty(LPUNKNOWN object, DISPID dispID, VARIANT& v)
{
    IDispatch *pDisp;
    HRESULT hr = object->QueryInterface(IID_IDispatch, (LPVOID *)&pDisp);
    if( SUCCEEDED(hr) )
    {
        DISPPARAMS dispparamsNoArgs = {NULL, NULL, 0, 0};
        VARIANT vres;
        VariantInit(&vres);
        hr = pDisp->Invoke(dispID, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_PROPERTYGET, &dispparamsNoArgs, &vres, NULL, NULL);
        if( SUCCEEDED(hr) )
        {
            if( V_VT(&v) != V_VT(&vres) )
            {
                hr = VariantChangeType(&v, &vres, 0, V_VT(&v));
                VariantClear(&vres);
            }
            else
            {
                v = vres;
            }
        }
        pDisp->Release();
    }
    return hr;
};

HDC CreateDevDC(DVTARGETDEVICE *ptd)
{
	HDC hdc=NULL;
	if( NULL == ptd )
    {
		hdc = CreateDC(TEXT("DISPLAY"), NULL, NULL, NULL);
	}
    else
    {
        LPDEVNAMES lpDevNames;
        LPDEVMODE lpDevMode;
        LPTSTR lpszDriverName;
        LPTSTR lpszDeviceName;
        LPTSTR lpszPortName;

        lpDevNames = (LPDEVNAMES) ptd; // offset for size field

        if (ptd->tdExtDevmodeOffset == 0)
        {
            lpDevMode = NULL;
        }
        else
        {
            lpDevMode  = (LPDEVMODE) ((LPTSTR)ptd + ptd->tdExtDevmodeOffset);
        }

        lpszDriverName = (LPTSTR) lpDevNames + ptd->tdDriverNameOffset;
        lpszDeviceName = (LPTSTR) lpDevNames + ptd->tdDeviceNameOffset;
        lpszPortName   = (LPTSTR) lpDevNames + ptd->tdPortNameOffset;

        hdc = CreateDC(lpszDriverName, lpszDeviceName, lpszPortName, lpDevMode);
    }
	return hdc;
};

#define HIMETRIC_PER_INCH 2540

void DPFromHimetric(HDC hdc, LPPOINT pt, int count)
{
    LONG lpX = GetDeviceCaps(hdc, LOGPIXELSX);
    LONG lpY = GetDeviceCaps(hdc, LOGPIXELSY);
    while( count-- )
    {
        pt->x = pt->x*lpX/HIMETRIC_PER_INCH;
        pt->y = pt->y*lpY/HIMETRIC_PER_INCH;
        ++pt;
    }
};

void HimetricFromDP(HDC hdc, LPPOINT pt, int count)
{
    LONG lpX = GetDeviceCaps(hdc, LOGPIXELSX);
    LONG lpY = GetDeviceCaps(hdc, LOGPIXELSY);
    while( count-- )
    {
        pt->x = pt->x*HIMETRIC_PER_INCH/lpX;
        pt->y = pt->y*HIMETRIC_PER_INCH/lpY;
        ++pt;
    }
};

