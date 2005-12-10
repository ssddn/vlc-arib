/*****************************************************************************
 * realaudio.c: a realaudio decoder that uses the realaudio library/dll
 *****************************************************************************
 * Copyright (C) 2005 the VideoLAN team
 * $Id$
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <vlc/vlc.h>
#include <vlc/aout.h>
#include <vlc/vout.h>
#include <vlc/decoder.h>

#ifdef LOADER
/* Need the w32dll loader from mplayer */
#   include <wine/winerror.h>
#   include <ldt_keeper.h>
#   include <wine/windef.h>

void *WINAPI LoadLibraryA( char *name );
void *WINAPI GetProcAddress( void *handle, char *func );
int WINAPI FreeLibrary( void *handle );
#endif

#ifndef WINAPI
#   define WINAPI
#endif

#if defined(HAVE_DL_DLOPEN)
#   if defined(HAVE_DLFCN_H) /* Linux, BSD, Hurd */
#       include <dlfcn.h>
#   endif
#   if defined(HAVE_SYS_DL_H)
#       include <sys/dl.h>
#   endif
#endif

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin();
    set_description( _("RealAudio library decoder") );
    set_capability( "decoder", 10 );
    set_category( CAT_INPUT );
    set_subcategory( SUBCAT_INPUT_VCODEC );
    set_callbacks( Open, Close );
vlc_module_end();

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  OpenDll( decoder_t * );
static int  OpenNativeDll( decoder_t *, char *, char * );
static int  OpenWin32Dll( decoder_t *, char *, char * );
static void CloseDll( decoder_t * );

static aout_buffer_t *Decode( decoder_t *, block_t ** );

struct decoder_sys_t
{
    audio_date_t end_date;

    /* Frame buffer for data reordering */
    int i_subpacket;
    mtime_t i_packet_pts;
    int i_frame_size;
    char *p_frame;
    int i_frame;

    /* Output buffer */
    char *p_out_buffer;
    char *p_out;
    unsigned int i_out;

    /* Codec params */
    void *context;
    int i_subpacket_size;
    int i_subpacket_h;
    int i_codec_flavor;
    int i_coded_frame_size;
    int i_extra;
    uint8_t *p_extra;

    void *dll;
    unsigned long (*raCloseCodec)(void*);
    unsigned long (*raDecode)(void*, char*, unsigned long, char*,
                              unsigned int*, long);
    unsigned long (*raFlush)(unsigned long, unsigned long, unsigned long);
    unsigned long (*raFreeDecoder)(void*);
    void*         (*raGetFlavorProperty)(void*, unsigned long,
                                         unsigned long, int*);
    unsigned long (*raInitDecoder)(void*, void*);
    unsigned long (*raOpenCodec)(void*);
    unsigned long (*raOpenCodec2)(void*, void*);
    unsigned long (*raSetFlavor)(void*, unsigned long);
    void          (*raSetDLLAccessPath)(char*);
    void          (*raSetPwd)(char*, char*);

#if defined(LOADER)
    ldt_fs_t *ldt_fs;
#endif

    void *win32_dll;
    unsigned long WINAPI (*wraCloseCodec)(void*);
    unsigned long WINAPI (*wraDecode)(void*, char*, unsigned long, char*,
                                      unsigned int*, long);
    unsigned long WINAPI (*wraFlush)(unsigned long, unsigned long,
                                     unsigned long);
    unsigned long WINAPI (*wraFreeDecoder)(void*);
    void*         WINAPI (*wraGetFlavorProperty)(void*, unsigned long,
                                                 unsigned long, int*);
    unsigned long WINAPI (*wraInitDecoder)(void*, void*);
    unsigned long WINAPI (*wraOpenCodec)(void*);
    unsigned long WINAPI (*wraOpenCodec2)(void*, void*);
    unsigned long WINAPI (*wraSetFlavor)(void*, unsigned long);
    void          WINAPI (*wraSetDLLAccessPath)(char*);
    void          WINAPI (*wraSetPwd)(char*, char*);
};

/* linux dlls doesn't need packing */
typedef struct /*__attribute__((__packed__))*/ {
    int samplerate;
    short bits;
    short channels;
    short quality;
    /* 2bytes padding here, by gcc */
    int bits_per_frame;
    int packetsize;
    int extradata_len;
    void* extradata;
} ra_init_t;

/* windows dlls need packed structs (no padding) */
typedef struct __attribute__((__packed__)) {
    int samplerate;
    short bits;
    short channels;
    short quality;
    int bits_per_frame;
    int packetsize;
    int extradata_len;
    void* extradata;
} wra_init_t;

void *__builtin_new(unsigned long size) {return malloc(size);}
void __builtin_delete(void *p) {free(p);}

static int pi_channels_maps[7] =
{
    0,
    AOUT_CHAN_CENTER,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT,
    AOUT_CHAN_CENTER | AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_REARLEFT
     | AOUT_CHAN_REARRIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_CENTER
     | AOUT_CHAN_REARLEFT | AOUT_CHAN_REARRIGHT,
    AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_CENTER
     | AOUT_CHAN_REARLEFT | AOUT_CHAN_REARRIGHT | AOUT_CHAN_LFE
};

/*****************************************************************************
 * Open: probe the decoder and return score
 *****************************************************************************
 * Tries to launch a decoder and return score so that the interface is able
 * to choose.
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    decoder_t *p_dec = (decoder_t*)p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    switch( p_dec->fmt_in.i_codec )
    {
    case VLC_FOURCC('c','o','o','k'):
        break;

    default:
        return VLC_EGENERIC;
    }

    p_dec->p_sys = p_sys = malloc( sizeof( decoder_sys_t ) );
    memset( p_sys, 0, sizeof(decoder_sys_t) );

    if( p_dec->fmt_in.i_extra >= 10 )
    {
        p_sys->i_subpacket_size = ((short *)(p_dec->fmt_in.p_extra))[0];
        p_sys->i_subpacket_h = ((short *)(p_dec->fmt_in.p_extra))[1];
        p_sys->i_codec_flavor = ((short *)(p_dec->fmt_in.p_extra))[2];
        p_sys->i_coded_frame_size = ((short *)(p_dec->fmt_in.p_extra))[3];
        p_sys->i_extra = ((short*)(p_dec->fmt_in.p_extra))[4];
        p_sys->p_extra = p_dec->fmt_in.p_extra + 10;
    }

    if( OpenDll( p_dec ) != VLC_SUCCESS )
    {
        free( p_sys );
        return VLC_EGENERIC;
    }

#ifdef LOADER
    if( p_sys->win32_dll ) Close( p_this );
#endif

    es_format_Init( &p_dec->fmt_out, AUDIO_ES, AOUT_FMT_S16_NE );
    p_dec->fmt_out.audio.i_rate = p_dec->fmt_in.audio.i_rate;
    p_dec->fmt_out.audio.i_channels = p_dec->fmt_in.audio.i_channels;
    p_dec->fmt_out.audio.i_bitspersample =
        p_dec->fmt_in.audio.i_bitspersample;
    p_dec->fmt_out.audio.i_physical_channels =
    p_dec->fmt_out.audio.i_original_channels =
        pi_channels_maps[p_dec->fmt_out.audio.i_channels];

    aout_DateInit( &p_sys->end_date, p_dec->fmt_out.audio.i_rate );
    aout_DateSet( &p_sys->end_date, 0 );

    p_dec->pf_decode_audio = Decode;

    p_sys->i_frame_size =
        p_dec->fmt_in.audio.i_blockalign * p_sys->i_subpacket_h;
    p_sys->p_frame = malloc( p_sys->i_frame_size );
    p_sys->i_packet_pts = 0;
    p_sys->i_subpacket = 0;
    p_sys->i_frame = 0;

    p_sys->p_out_buffer = malloc( 4096 * 10 );
    p_sys->p_out = 0;
    p_sys->i_out = 0;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    decoder_t *p_dec = (decoder_t*)p_this;

    CloseDll( p_dec );
    if( p_dec->p_sys->p_frame ) free( p_dec->p_sys->p_frame );
    if( p_dec->p_sys->p_out_buffer ) free( p_dec->p_sys->p_out_buffer );
    free( p_dec->p_sys );
}

/*****************************************************************************
 * OpenDll:
 *****************************************************************************/
static int OpenDll( decoder_t *p_dec )
{
    char *psz_dll;
    int i, i_result;

    char *ppsz_path[] =
    {
      ".",
#ifndef WIN32
      "/usr/local/RealPlayer8/Codecs",
      "/usr/RealPlayer8/Codecs",
      "/usr/lib/RealPlayer8/Codecs",
      "/opt/RealPlayer8/Codecs",
      "/usr/lib/RealPlayer9/users/Real/Codecs",
      "/usr/lib64/RealPlayer8/Codecs",
      "/usr/lib64/RealPlayer9/users/Real/Codecs",
      "/usr/lib/win32",
#endif
      NULL,
      NULL,
      NULL
    };

#ifdef WIN32
    char psz_win32_real_codecs[MAX_PATH + 1];
    char psz_win32_helix_codecs[MAX_PATH + 1];
#endif

    for( i = 0; ppsz_path[i]; i++ )
    {
        asprintf( &psz_dll, "%s/%4.4s.so.6.0", ppsz_path[i],
                  (char *)&p_dec->fmt_in.i_codec );
        i_result = OpenNativeDll( p_dec, ppsz_path[i], psz_dll );
        free( psz_dll );
        if( i_result == VLC_SUCCESS ) return VLC_SUCCESS;
    }

#ifdef WIN32
    {
        HKEY h_key;
        DWORD i_type, i_data = MAX_PATH + 1, i_index = 1;
        char *p_data;

        p_data = psz_win32_real_codecs;
        if( RegOpenKeyEx( HKEY_CLASSES_ROOT,
                          _T("Software\\RealNetworks\\Preferences\\DT_Codecs"),
                          0, KEY_READ, &h_key ) == ERROR_SUCCESS )
        {
             if( RegQueryValueEx( h_key, _T(""), 0, &i_type,
                                  (LPBYTE)p_data, &i_data ) == ERROR_SUCCESS &&
                 i_type == REG_SZ )
             {
                 int i_len = strlen( p_data );
                 if( i_len && p_data[i_len-1] == '\\' ) p_data[i_len-1] = 0;
                 ppsz_path[i_index++] = p_data;
                 msg_Err( p_dec, "Real: %s", p_data );
             }
             RegCloseKey( h_key );
        }

        p_data = psz_win32_helix_codecs;
        if( RegOpenKeyEx( HKEY_CLASSES_ROOT,
                          _T("Helix\\HelixSDK\\10.0\\Preferences\\DT_Codecs"),
                          0, KEY_READ, &h_key ) == ERROR_SUCCESS )
        {
             if( RegQueryValueEx( h_key, _T(""), 0, &i_type,
                                  (LPBYTE)p_data, &i_data ) == ERROR_SUCCESS &&
                 i_type == REG_SZ )
             {
                 int i_len = strlen( p_data );
                 if( i_len && p_data[i_len-1] == '\\' ) p_data[i_len-1] = 0;
                 ppsz_path[i_index++] = p_data;
                 msg_Err( p_dec, "Real: %s", p_data );
             }
             RegCloseKey( h_key );
        }
    }
#endif

    for( i = 0; ppsz_path[i]; i++ )
    {
        /* New format */
        asprintf( &psz_dll, "%s\\%4.4s.dll", ppsz_path[i],
                  (char *)&p_dec->fmt_in.i_codec );
        i_result = OpenWin32Dll( p_dec, ppsz_path[i], psz_dll );
        free( psz_dll );
        if( i_result == VLC_SUCCESS ) return VLC_SUCCESS;

        /* Old format */
        asprintf( &psz_dll, "%s\\%4.4s3260.dll", ppsz_path[i],
                  (char *)&p_dec->fmt_in.i_codec );
        i_result = OpenWin32Dll( p_dec, ppsz_path[i], psz_dll );
        free( psz_dll );
        if( i_result == VLC_SUCCESS ) return VLC_SUCCESS;
    }

    return VLC_EGENERIC;
}

static int OpenNativeDll( decoder_t *p_dec, char *psz_path, char *psz_dll )
{
#if defined(HAVE_DL_DLOPEN)
    decoder_sys_t *p_sys = p_dec->p_sys;
    void *handle = 0, *context = 0;
    unsigned int i_result;
    void *p_prop;
    int i_prop;

    ra_init_t init_data =
    {
        p_dec->fmt_in.audio.i_rate,
        p_dec->fmt_in.audio.i_bitspersample,
        p_dec->fmt_in.audio.i_channels,
        100, /* quality */
        p_sys->i_subpacket_size,
        p_sys->i_coded_frame_size,
        p_sys->i_extra, p_sys->p_extra
    };

    msg_Dbg( p_dec, "opening library '%s'", psz_dll );
    if( !(handle = dlopen( psz_dll, RTLD_LAZY )) )
    {
        msg_Dbg( p_dec, "couldn't load library '%s' (%s)",
                 psz_dll, dlerror() );
        return VLC_EGENERIC;
    }

    p_sys->raCloseCodec = dlsym( handle, "RACloseCodec" );
    p_sys->raDecode = dlsym( handle, "RADecode" );
    p_sys->raFlush = dlsym( handle, "RAFlush" );
    p_sys->raFreeDecoder = dlsym( handle, "RAFreeDecoder" );
    p_sys->raGetFlavorProperty = dlsym( handle, "RAGetFlavorProperty" );
    p_sys->raOpenCodec = dlsym( handle, "RAOpenCodec" );
    p_sys->raOpenCodec2 = dlsym( handle, "RAOpenCodec2" );
    p_sys->raInitDecoder = dlsym( handle, "RAInitDecoder" );
    p_sys->raSetFlavor = dlsym( handle, "RASetFlavor" );
    p_sys->raSetDLLAccessPath = dlsym( handle, "SetDLLAccessPath" );
    p_sys->raSetPwd = dlsym( handle, "RASetPwd" ); // optional, used by SIPR

    if( !(p_sys->raOpenCodec || p_sys->raOpenCodec2) ||
        !p_sys->raCloseCodec || !p_sys->raInitDecoder ||
        !p_sys->raDecode || !p_sys->raFreeDecoder ||
        !p_sys->raGetFlavorProperty || !p_sys->raSetFlavor
        /* || !p_sys->raFlush || !p_sys->raSetDLLAccessPath */ )
    {
        goto error_native;
    }

    if( p_sys->raOpenCodec2 )
        i_result = p_sys->raOpenCodec2( &context, psz_path );
    else
        i_result = p_sys->raOpenCodec( &context );

    if( i_result )
    {
        msg_Err( p_dec, "decoder open failed, error code: 0x%x", i_result );
        goto error_native;
    }

    i_result = p_sys->raInitDecoder( context, &init_data );
    if( i_result )
    {
        msg_Err( p_dec, "decoder init failed, error code: 0x%x", i_result );
        goto error_native;
    }

    i_result = p_sys->raSetFlavor( context, p_sys->i_codec_flavor );
    if( i_result )
    {
        msg_Err( p_dec, "decoder flavor setup failed, error code: 0x%x",
                 i_result );
        goto error_native;
    }

    p_prop = p_sys->raGetFlavorProperty( context, p_sys->i_codec_flavor,
                                         0, &i_prop );
    msg_Dbg( p_dec, "audio codec: [%d] %s",
             p_sys->i_codec_flavor, (char *)p_prop );

    p_prop = p_sys->raGetFlavorProperty( context, p_sys->i_codec_flavor,
                                         1, &i_prop );
    if( p_prop )
    {
        int i_bps = ((*((int*)p_prop))+4)/8;
        msg_Dbg( p_dec, "audio bitrate: %5.3f kbit/s (%d bps)",
                 (*((int*)p_prop))*0.001f, i_bps );
    }

    p_sys->context = context;
    p_sys->dll = handle;
    return VLC_SUCCESS;

 error_native:
    if( context ) p_sys->raFreeDecoder( context );
    if( context ) p_sys->raCloseCodec( context );
    dlclose( handle );
#endif

    return VLC_EGENERIC;
}

static int OpenWin32Dll( decoder_t *p_dec, char *psz_path, char *psz_dll )
{
#if defined(LOADER) || defined(WIN32)
    decoder_sys_t *p_sys = p_dec->p_sys;
    void *handle = 0, *context = 0;
    unsigned int i_result;
    void *p_prop;
    int i_prop;

    wra_init_t init_data =
    {
        p_dec->fmt_in.audio.i_rate,
        p_dec->fmt_in.audio.i_bitspersample,
        p_dec->fmt_in.audio.i_channels,
        100, /* quality */
        p_sys->i_subpacket_size,
        p_sys->i_coded_frame_size,
        p_sys->i_extra, p_sys->p_extra
    };

    msg_Dbg( p_dec, "opening win32 dll '%s'", psz_dll );

#ifdef LOADER
    Setup_LDT_Keeper();
#endif

    if( !(handle = LoadLibraryA( psz_dll )) )
    {
        msg_Dbg( p_dec, "couldn't load dll '%s'", psz_dll );
        return VLC_EGENERIC;
    }

    p_sys->wraCloseCodec = GetProcAddress( handle, "RACloseCodec" );
    p_sys->wraDecode = GetProcAddress( handle, "RADecode" );
    p_sys->wraFlush = GetProcAddress( handle, "RAFlush" );
    p_sys->wraFreeDecoder = GetProcAddress( handle, "RAFreeDecoder" );
    p_sys->wraGetFlavorProperty =
        GetProcAddress( handle, "RAGetFlavorProperty" );
    p_sys->wraOpenCodec = GetProcAddress( handle, "RAOpenCodec" );
    p_sys->wraOpenCodec2 = GetProcAddress( handle, "RAOpenCodec2" );
    p_sys->wraInitDecoder = GetProcAddress( handle, "RAInitDecoder" );
    p_sys->wraSetFlavor = GetProcAddress( handle, "RASetFlavor" );
    p_sys->wraSetDLLAccessPath = GetProcAddress( handle, "SetDLLAccessPath" );
    p_sys->wraSetPwd =
        GetProcAddress( handle, "RASetPwd" ); // optional, used by SIPR

    if( !(p_sys->wraOpenCodec || p_sys->wraOpenCodec2) ||
        !p_sys->wraCloseCodec || !p_sys->wraInitDecoder ||
        !p_sys->wraDecode || !p_sys->wraFreeDecoder ||
        !p_sys->wraGetFlavorProperty || !p_sys->wraSetFlavor
        /* || !p_sys->wraFlush || !p_sys->wraSetDLLAccessPath */ )
    {
        FreeLibrary( handle );
        return VLC_EGENERIC;
    }

    if( p_sys->wraOpenCodec2 )
        i_result = p_sys->wraOpenCodec2( &context, psz_path );
    else
        i_result = p_sys->wraOpenCodec( &context );

    if( i_result )
    {
        msg_Err( p_dec, "decoder open failed, error code: 0x%x", i_result );
        goto error_win32;
    }

    i_result = p_sys->wraInitDecoder( context, &init_data );
    if( i_result )
    {
        msg_Err( p_dec, "decoder init failed, error code: 0x%x", i_result );
        goto error_win32;
    }

    i_result = p_sys->wraSetFlavor( context, p_sys->i_codec_flavor );
    if( i_result )
    {
        msg_Err( p_dec, "decoder flavor setup failed, error code: 0x%x",
                 i_result );
        goto error_win32;
    }

    p_prop = p_sys->wraGetFlavorProperty( context, p_sys->i_codec_flavor,
                                          0, &i_prop );
    msg_Dbg( p_dec, "audio codec: [%d] %s",
             p_sys->i_codec_flavor, (char *)p_prop );

    p_prop = p_sys->wraGetFlavorProperty( context, p_sys->i_codec_flavor,
                                          1, &i_prop );
    if( p_prop )
    {
        int i_bps = ((*((int*)p_prop))+4)/8;
        msg_Dbg( p_dec, "audio bitrate: %5.3f kbit/s (%d bps)",
                 (*((int*)p_prop))*0.001f, i_bps );
    }

    p_sys->context = context;
    p_sys->win32_dll = handle;
    return VLC_SUCCESS;

 error_win32:
    if( context ) p_sys->wraFreeDecoder( context );
    if( context ) p_sys->wraCloseCodec( context );
    FreeLibrary( handle );
#endif

    return VLC_EGENERIC;
}

/*****************************************************************************
 * CloseDll:
 *****************************************************************************/
static void CloseDll( decoder_t *p_dec )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( p_sys->context && p_sys->dll )
    {
        p_sys->raFreeDecoder( p_sys->context );
        p_sys->raCloseCodec( p_sys->context );
    }

    if( p_sys->context && p_sys->win32_dll )
    {
        p_sys->wraFreeDecoder( p_sys->context );
        p_sys->wraCloseCodec( p_sys->context );
    }

#if defined(HAVE_DL_DLOPEN)
    if( p_sys->dll ) dlclose( p_sys->dll );
#endif

#if defined(LOADER) || defined(WIN32)
    if( p_sys->win32_dll ) FreeLibrary( p_sys->win32_dll );

#if 0 //def LOADER /* Segfaults */
    Restore_LDT_Keeper( p_sys->ldt_fs );
    msg_Dbg( p_dec, "Restore_LDT_Keeper" );
#endif
#endif

    p_sys->dll = 0;
    p_sys->win32_dll = 0;
    p_sys->context = 0;
}

/*****************************************************************************
 * DecodeAudio:
 *****************************************************************************/
static aout_buffer_t *Decode( decoder_t *p_dec, block_t **pp_block )
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    aout_buffer_t *p_aout_buffer = 0;
    block_t *p_block;

#ifdef LOADER
    if( !p_sys->win32_dll && !p_sys->dll )
    {
        /* We must do open and close in the same thread (unless we do
         * Setup_LDT_Keeper in the main thread before all others */
        if( OpenDll( p_dec ) != VLC_SUCCESS )
        {
            /* Fatal */
            p_dec->b_error = VLC_TRUE;
            return NULL;
        }
    }
#endif

    /* Output 1024 samples at a time
     * (the audio output doesn't like big audio packets) */
    if( p_sys->i_out )
    {
        int i_out;
        int i_samples = p_sys->i_out * 8 /
            p_dec->fmt_out.audio.i_bitspersample /
              p_dec->fmt_out.audio.i_channels;

        i_samples = __MIN( i_samples, 1024 );
        i_out = i_samples * p_dec->fmt_out.audio.i_bitspersample *
            p_dec->fmt_out.audio.i_channels / 8;

        p_aout_buffer = p_dec->pf_aout_buffer_new( p_dec, i_samples );
        if( p_aout_buffer == NULL ) return NULL;
        memcpy( p_aout_buffer->p_buffer, p_sys->p_out, i_out );

        p_sys->i_out -= i_out;
        p_sys->p_out += i_out;

        /* Date management */
        p_aout_buffer->start_date = aout_DateGet( &p_sys->end_date );
        p_aout_buffer->end_date =
            aout_DateIncrement( &p_sys->end_date, i_samples );

        return p_aout_buffer;
    }

    /* If we have a full frame ready then we can start decoding */
    if( p_sys->i_frame )
    {
        unsigned int i_result;

        p_sys->p_out = p_sys->p_out_buffer;

        if( p_sys->dll )
            i_result = p_sys->raDecode( p_sys->context, p_sys->p_frame +
                                        p_sys->i_frame_size - p_sys->i_frame,
                                        p_dec->fmt_in.audio.i_blockalign,
                                        p_sys->p_out, &p_sys->i_out, -1 );
        else
            i_result = p_sys->wraDecode( p_sys->context, p_sys->p_frame +
                                         p_sys->i_frame_size - p_sys->i_frame,
                                         p_dec->fmt_in.audio.i_blockalign,
                                         p_sys->p_out, &p_sys->i_out, -1 );

#if 0
        msg_Err( p_dec, "decoded: %i samples (%i)",
                 p_sys->i_out * 8 / p_dec->fmt_out.audio.i_bitspersample /
                 p_dec->fmt_out.audio.i_channels, i_result );
#endif

        p_sys->i_frame -= p_dec->fmt_in.audio.i_blockalign;

        return Decode( p_dec, pp_block );
    }


    if( pp_block == NULL || *pp_block == NULL ) return NULL;
    p_block = *pp_block;

    //msg_Err( p_dec, "Decode: "I64Fd", %i", p_block->i_pts, p_block->i_buffer );

    /* Detect missing subpackets */
    if( p_sys->i_subpacket && p_block->i_pts > 0 &&
        p_block->i_pts != p_sys->i_packet_pts )
    {
        /* All subpackets in a packet should have the same pts so we must
         * have dropped some. Clear current frame buffer. */
        p_sys->i_subpacket = 0;
        msg_Dbg( p_dec, "detected dropped subpackets" );
    }
    if( p_block->i_pts > 0 ) p_sys->i_packet_pts = p_block->i_pts;

    /* Date management */
    if( /* !p_sys->i_subpacket && */ p_block && p_block->i_pts > 0 &&
        p_block->i_pts != aout_DateGet( &p_sys->end_date ) )
    {
        aout_DateSet( &p_sys->end_date, p_block->i_pts );
    }

#if 0
    if( !aout_DateGet( &p_sys->end_date ) )
    {
        /* We've just started the stream, wait for the first PTS. */
        if( p_block ) block_Release( p_block );
        return NULL;
    }
#endif

    if( p_sys->i_subpacket_size )
    {
        /* 'cook' way */
        int sps = p_sys->i_subpacket_size;
        int w = p_dec->fmt_in.audio.i_blockalign;
        int h = p_sys->i_subpacket_h;
        /* int cfs = p_sys->i_coded_frame_size; */
        int x, y;

        w /= sps;
        y = p_sys->i_subpacket;

        for( x = 0; x < w; x++ )
        {
            memcpy( p_sys->p_frame + sps * (h*x+((h+1)/2)*(y&1)+(y>>1)),
                    p_block->p_buffer, sps );
            p_block->p_buffer += sps;
        }

        p_sys->i_subpacket = (p_sys->i_subpacket + 1) % p_sys->i_subpacket_h;

        if( !p_sys->i_subpacket )
        {
            /* We have a complete frame */
            p_sys->i_frame = p_sys->i_frame_size;
            p_aout_buffer = Decode( p_dec, pp_block );
        }
    }

    block_Release( p_block );
    *pp_block = 0;
    return p_aout_buffer;
}
