/*****************************************************************************
 * darwin_specific.m: Darwin specific features
 *****************************************************************************
 * Copyright (C) 2001-2007 the VideoLAN team
 * $Id$
 *
 * Authors: Sam Hocevar <sam@zoy.org>
 *          Christophe Massiot <massiot@via.ecp.fr>
 *          Pierre d'Herbemont <pdherbemont@free.fr>
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
#include <string.h>                                              /* strdup(), strstr() */
#include <stdlib.h>                                                /* free() */
#include <dirent.h>                                                /* *dir() */

#include <vlc/vlc.h>

#include <CoreFoundation/CoreFoundation.h>

#ifdef HAVE_LOCALE_H
#   include <locale.h>
#endif

/* CFLocaleCopyAvailableLocaleIdentifiers is present only on post-10.4 */
extern CFArrayRef CFLocaleCopyAvailableLocaleIdentifiers(void) __attribute__((weak_import));

/* emulate CFLocaleCopyAvailableLocaleIdentifiers on pre-10.4 */
static CFArrayRef copy_all_locale_indentifiers(void)
{
    CFMutableArrayRef available_locales;
    DIR * dir;
    struct dirent *file;

    dir = opendir( "/usr/share/locale" );
    available_locales = CFArrayCreateMutable( kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks );

    while ( (file = readdir(dir)) )
    {
        /* we should probably filter out garbage */
        /* we can't use CFStringCreateWithFileSystemRepresentation as it is 
         * supported only on post-10.4 (and this function is only for pre-10.4) */
        CFStringRef locale = CFStringCreateWithCString( kCFAllocatorDefault, file->d_name, kCFStringEncodingUTF8 );
        CFArrayAppendValue( available_locales, (void*)locale );
        CFRelease( locale );
    }

    closedir( dir );
    return available_locales;
}

/*****************************************************************************
 * system_Init: fill in program path & retrieve language
 *****************************************************************************/
void system_Init( vlc_t *p_this, int *pi_argc, char *ppsz_argv[] )
{
    char i_dummy;
    char *p_char, *p_oldchar = &i_dummy;

    /* Get the full program path and name */
    p_char = p_this->p_libvlc->psz_vlcpath = strdup( ppsz_argv[ 0 ] );

    /* Remove trailing program name */
    for( ; *p_char ; )
    {
        if( *p_char == '/' )
        {
            *p_oldchar = '/';
            *p_char = '\0';
            p_oldchar = p_char;
        }

        p_char++;
    }

    /* Check if $LANG is set. */
    if ( (p_char = getenv("LANG")) == NULL )
    {
        /*
           Retrieve the preferred language as chosen in  System Preferences.app
           (note that CFLocaleCopyCurrent() is not used because it returns the
            prefered locale not language) 
        */
        CFArrayRef all_locales, preferred_locales;
        char psz_locale[50];

        if( CFLocaleCopyAvailableLocaleIdentifiers )
            all_locales = CFLocaleCopyAvailableLocaleIdentifiers();
        else
            all_locales = copy_all_locale_indentifiers();

        preferred_locales = CFBundleCopyLocalizationsForPreferences( all_locales, NULL );

        if ( preferred_locales )
        {
            if ( CFArrayGetCount( preferred_locales ) )
            {
                CFStringRef user_language_string_ref = CFArrayGetValueAtIndex( preferred_locales, 0 );
                CFStringGetCString( user_language_string_ref, psz_locale, sizeof(psz_locale), kCFStringEncodingUTF8 );
                setenv( "LANG", psz_locale, 1 );
            }
            CFRelease( preferred_locales );
        }
        CFRelease( all_locales );
    }

    vlc_mutex_init( p_this, &p_this->p_libvlc->iconv_lock );
    p_this->p_libvlc->iconv_macosx = vlc_iconv_open( "UTF-8", "UTF-8-MAC" );
}

/*****************************************************************************
 * system_Configure: check for system specific configuration options.
 *****************************************************************************/
void system_Configure( vlc_t *p_this, int *pi_argc, char *ppsz_argv[] )
{

}

/*****************************************************************************
 * system_End: free the program path.
 *****************************************************************************/
void system_End( vlc_t *p_this )
{
    free( p_this->p_libvlc->psz_vlcpath );

    if ( p_this->p_libvlc->iconv_macosx != (vlc_iconv_t)-1 )
        vlc_iconv_close( p_this->p_libvlc->iconv_macosx );
    vlc_mutex_destroy( &p_this->p_libvlc->iconv_lock );
}

