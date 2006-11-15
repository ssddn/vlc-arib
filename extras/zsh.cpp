/*****************************************************************************
 * zsh.cpp: create zsh completion rule for vlc
 *****************************************************************************
 * Copyright (C) 2005 the VideoLAN team
 * $Id$
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
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

#include <stdio.h>
#include <map>
#include <string>
#include <utility>
#include <iostream>
#include <algorithm>
typedef std::multimap<std::string, std::string> mmap;
typedef std::multimap<int, std::string> mcmap;

typedef std::pair<std::string, std::string> mpair;
typedef std::pair<int, std::string> mcpair;

#include <vlc/vlc.h>

void ParseModules( vlc_t *p_vlc, mmap &mods, mcmap &mods2 );
void PrintModuleList( vlc_t *p_vlc, mmap &mods, mcmap &mods2 );
void ParseOption( module_config_t *p_item, mmap &mods, mcmap &mods2 );
void PrintOption( char *psz_option, char i_short, char *psz_exlusive,
                   char *psz_text, char *psz_longtext, char *psz_args );

int main( int i_argc, char **ppsz_argv )
{
    mmap mods;
    mcmap mods2;
    /* Create a libvlc structure */
    int i_ret = VLC_Create();
    vlc_t *p_vlc;

    if( i_ret < 0 )
    {
        return i_ret;
    }

    /* Initialize libvlc */    
    i_ret = VLC_Init( 0, i_argc, ppsz_argv );
    if( i_ret < 0 )
    {
        VLC_Destroy( 0 );
        return i_ret;
    }
    p_vlc = vlc_current_object( i_ret );
    printf("#compdef vlc\n\n"

           "#This file is autogenerated by zsh.cpp\n"
           "typeset -A opt_args\n"
           "local context state line ret=1\n"
           "local modules\n\n" );

    PrintModuleList( p_vlc, mods, mods2 );

    printf( "_arguments -S -s \\\n" );
    ParseModules( p_vlc, mods, mods2 );
    printf( "  \"(--module)-p[print help on module]:print help on module:($modules)\"\\\n" );
    printf( "  \"(-p)--module[print help on module]:print help on module:($modules)\"\\\n" );
    printf( "  \"(--help)-h[print help]\"\\\n" );
    printf( "  \"(-h)--help[print help]\"\\\n" );
    printf( "  \"(--longhelp)-H[print detailed help]\"\\\n" );
    printf( "  \"(-H)--longhelp[print detailed help]\"\\\n" );
    printf( "  \"(--list)-l[print a list of available modules]\"\\\n" );
    printf( "  \"(-l)--list[print a list of available modules]\"\\\n" );
    printf( "  \"--save-config[save the current command line options in the config file]\"\\\n" );
    printf( "  \"--reset-config[reset the current config to the default values]\"\\\n" );
    printf( "  \"--config[use alternate config file]\"\\\n" );
    printf( "  \"--reset-plugins-cache[resets the current plugins cache]\"\\\n" );
    printf( "  \"--version[print version information]\"\\\n" );
    printf( "  \"*:Playlist item:->mrl\" && ret=0\n\n" );

    printf( "case $state in\n" );
    printf( "  mrl)\n" );
    printf( "    _alternative 'files:file:_files' 'urls:URL:_urls' && ret=0\n" );
    printf( "  ;;\n" );
    printf( "esac\n\n" );
    
    printf( "return ret\n" );

    return 0;
    /* Finish the threads */
    VLC_CleanUp( 0 );

    /* Destroy the libvlc structure */
    VLC_Destroy( 0 );

    return i_ret;
    
}

void ParseModules( vlc_t *p_vlc, mmap &mods, mcmap &mods2 )
{
    vlc_list_t      *p_list = NULL;;
    module_t        *p_module;
    module_config_t *p_item;
    int              i_index;

    /* List the plugins */
    p_list = vlc_list_find( p_vlc, VLC_OBJECT_MODULE, FIND_ANYWHERE );
    if( !p_list ) return;
    for( i_index = 0; i_index < p_list->i_count; i_index++ )
    {
        p_module = (module_t *)p_list->p_values[i_index].p_object;

        /* Exclude empty plugins (submodules don't have config options, they
         * are stored in the parent module) */
        if( p_module->b_submodule )
              continue;
//            p_item = ((module_t *)p_module->p_parent)->p_config;
        else
            p_item = p_module->p_config;

//        printf( "  #%s\n", p_module->psz_longname );
        if( !p_item ) continue;
        do
        {
            if( p_item->i_type == CONFIG_CATEGORY )
            {
//                printf( "  #Category %d\n", p_item->i_value );
            }
            else if( p_item->i_type == CONFIG_SUBCATEGORY )
            {
//                printf( "  #Subcategory %d\n", p_item->i_value );
            }
            if( p_item->i_type & CONFIG_ITEM )
                ParseOption( p_item, mods, mods2 );
        }
        while( p_item->i_type != CONFIG_HINT_END && p_item++ );

    }    
}

void PrintModuleList( vlc_t *p_vlc, mmap &mods, mcmap &mods2 )
{
    vlc_list_t      *p_list = NULL;;
    module_t        *p_module;
    int              i_index;

    /* List the plugins */
    p_list = vlc_list_find( p_vlc, VLC_OBJECT_MODULE, FIND_ANYWHERE );
    if( !p_list ) return;

    printf( "modules=\"" );
    for( i_index = 0; i_index < p_list->i_count; i_index++ )
    {
        p_module = (module_t *)p_list->p_values[i_index].p_object;

        /* Exclude empty plugins (submodules don't have config options, they
         * are stored in the parent module) */

        if( strcmp( p_module->psz_object_name, "main" ) )
        {
            mods.insert( mpair( p_module->psz_capability,
                                p_module->psz_object_name ) );
            module_config_t *p_config = p_module->p_config;
            if( p_config ) do
            {
                /* Hack: required subcategory is stored in i_min */
                if( p_config->i_type == CONFIG_SUBCATEGORY )
                {
                    mods2.insert( mcpair( p_config->i_value,
                                          p_module->psz_object_name ) );
                }
            } while( p_config->i_type != CONFIG_HINT_END && p_config++ );
            if( p_module->b_submodule )
                continue;
            printf( "%s ", p_module->psz_object_name );
        }

    }
    printf( "\"\n\n" );
    return;
}

void ParseOption( module_config_t *p_item, mmap &mods, mcmap &mods2 )
{
    char *psz_arguments = "";
    char *psz_exclusive;
    char *psz_option;
    //Skip deprecated options
    if( p_item->psz_current )
        return;
    
    switch( p_item->i_type )
    {
    case CONFIG_ITEM_MODULE:
    {
        std::pair<mmap::iterator, mmap::iterator> range = mods.equal_range( p_item->psz_type );
        std::string list = (*range.first).second;
        ++range.first;
        while( range.first != range.second )
        {
            list = list.append( " " );
            list = list.append( range.first->second );
            ++range.first;
        }
        asprintf( &psz_arguments, "(%s)", list.c_str() );
    }
    break;
    case CONFIG_ITEM_MODULE_CAT:
    {
        std::pair<mcmap::iterator, mcmap::iterator> range =
            mods2.equal_range( p_item->i_min );
        std::string list = (*range.first).second;
        ++range.first;
        while( range.first != range.second )
        {
            list = list.append( " " );
            list = list.append( range.first->second );
            ++range.first;
        }
        asprintf( &psz_arguments, "(%s)", list.c_str() );
    }
    break;
    case CONFIG_ITEM_MODULE_LIST_CAT:
    {
        std::pair<mcmap::iterator, mcmap::iterator> range =
            mods2.equal_range( p_item->i_min );
        std::string list = "_values -s , ";
        list = list.append( p_item->psz_name );
        while( range.first != range.second )
        {
            list = list.append( " '*" );
            list = list.append( range.first->second );
            list = list.append( "'" );
            ++range.first;
        }
        asprintf( &psz_arguments, "%s", list.c_str() );
    }
    break;

    case CONFIG_ITEM_STRING:
        if( p_item->i_list )
        {
            int i = p_item->i_list -1;
            char *psz_list;
            if( p_item->ppsz_list_text )
                asprintf( &psz_list, "%s\\:%s", p_item->ppsz_list[i],
                          p_item->ppsz_list_text[i] );
            else
                psz_list = strdup(p_item->ppsz_list[i]);
            char *psz_list2;
            while( i>1 )
            {
                if( p_item->ppsz_list_text )
                    asprintf( &psz_list2, "%s\\:%s %s", p_item->ppsz_list[i-1],
                              p_item->ppsz_list_text[i-1], psz_list );
                else
                    asprintf( &psz_list2, "%s %s", p_item->ppsz_list[i-1],
                              psz_list );

                free( psz_list );
                psz_list = psz_list2;
                i--;
            }
            if( p_item->ppsz_list_text )
                asprintf( &psz_arguments, "((%s))", psz_list );
            else
                asprintf( &psz_arguments, "(%s)", psz_list );
                
            free( psz_list );
        }
        break;

    case CONFIG_ITEM_FILE:
        psz_arguments = "_files";
        break;
    case CONFIG_ITEM_DIRECTORY:
        psz_arguments = "_files -/";
        break;

    case CONFIG_ITEM_INTEGER:
        if( p_item->i_list )
        {
            int i = p_item->i_list -1;
            char *psz_list;
            if( p_item->ppsz_list_text )
                asprintf( &psz_list, "%d\\:%s", p_item->pi_list[i],
                          p_item->ppsz_list_text[i] );
            else
                psz_list = strdup(p_item->ppsz_list[i]);
            char *psz_list2;
            while( i>1 )
            {
                if( p_item->ppsz_list_text )
                    asprintf( &psz_list2, "%d\\:%s %s", p_item->pi_list[i-1],
                              p_item->ppsz_list_text[i-1], psz_list );
                else
                    asprintf( &psz_list2, "%s %s", p_item->ppsz_list[i-1],
                              psz_list );

                free( psz_list );
                psz_list = psz_list2;
                i--;
            }
            if( p_item->ppsz_list_text )
                asprintf( &psz_arguments, "((%s))", psz_list );
            else
                asprintf( &psz_arguments, "(%s)", psz_list );
                
            free( psz_list );
        }
        else if( p_item->i_min != 0 || p_item->i_max != 0 )
        {
//            p_control = new RangedIntConfigControl( p_this, p_item, parent );
        }
        else
        {
//            p_control = new IntegerConfigControl( p_this, p_item, parent );
        }
        break;

    case CONFIG_ITEM_KEY:
//        p_control = new KeyConfigControl( p_this, p_item, parent );
        break;

    case CONFIG_ITEM_FLOAT:
//        p_control = new FloatConfigControl( p_this, p_item, parent );
        break;

    case CONFIG_ITEM_BOOL:
//        p_control = new BoolConfigControl( p_this, p_item, parent );
        psz_arguments = NULL;
        asprintf( &psz_exclusive, "--no%s --no-%s", p_item->psz_name,
                 p_item->psz_name );
        PrintOption( p_item->psz_name, p_item->i_short, psz_exclusive,
                     p_item->psz_text, p_item->psz_longtext, psz_arguments );
        free( psz_exclusive );
        asprintf( &psz_exclusive, "--no%s --%s", p_item->psz_name,
                 p_item->psz_name );
        asprintf( &psz_option, "no-%s", p_item->psz_name );
        PrintOption( psz_option, p_item->i_short, psz_exclusive,
                     p_item->psz_text, p_item->psz_longtext, psz_arguments );
        free( psz_exclusive );
        free( psz_option );
        asprintf( &psz_exclusive, "--no-%s --%s", p_item->psz_name,
                 p_item->psz_name );
        asprintf( &psz_option, "no%s", p_item->psz_name );
        PrintOption( psz_option, p_item->i_short, psz_exclusive,
                     p_item->psz_text, p_item->psz_longtext, psz_arguments );
        free( psz_exclusive );
        free( psz_option );        
        return;

    case CONFIG_SECTION:
//        p_control = new SectionConfigControl( p_this, p_item, parent );
        break;

    default:
        break;
    }
    PrintOption( p_item->psz_name, p_item->i_short, NULL,
                 p_item->psz_text, p_item->psz_longtext, psz_arguments );

}

void PrintOption( char *psz_option, char i_short, char *psz_exclusive,
                   char *psz_text, char *psz_longtext, char *psz_args )
{
    char *foo;
    if( psz_text )
    {
        while( (foo = strchr( psz_text, ':' ))) *foo=';';
        while( (foo = strchr( psz_text, '"' ))) *foo='\'';
    }
    if( psz_longtext )
    {
        while( (foo = strchr( psz_longtext, ':' ))) *foo=';';
        while( (foo = strchr( psz_longtext, '"' ))) *foo='\'';
    }
    if( !psz_longtext ||
        strchr( psz_longtext, '\n' ) ||
        strchr( psz_longtext, '(' ) ) psz_longtext = psz_text;
    if( i_short )
    {
        if( !psz_exclusive ) psz_exclusive = "";
        else asprintf( &psz_exclusive, " %s", psz_exclusive );
        printf( "  \"(-%c%s)--%s%s[%s]", i_short, psz_exclusive,
                psz_option, psz_args?"=":"", psz_text );
        if( psz_args )
            printf( ":%s:%s\"\\\n", psz_longtext, psz_args );
        else
            printf( "\"\\\n" );
        printf( "  \"(--%s%s)-%c[%s]", psz_option, psz_exclusive,
                i_short, psz_text );        
        if( psz_args )
            printf( ":%s:%s\"\\\n", psz_longtext, psz_args );
        else
            printf( "\"\\\n" );
                
    }
    else
    {
        if( psz_exclusive )
            printf( "  \"(%s)--%s%s[%s]", psz_exclusive, psz_option,
                    psz_args?"=":"", psz_text );
        else
            printf( "  \"--%s[%s]", psz_option, psz_text );
            
        if( psz_args )
            printf( ":%s:%s\"\\\n", psz_longtext, psz_args );
        else
            printf( "\"\\\n" );
        
    }
}

