/*****************************************************************************
 * cedar.c: Cedar decoder
 *****************************************************************************
 * Copyright © 2010-2012 VideoLAN
 *
 * Authors: Wills Wang <wills.wang@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* VLC includes */
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>
#include <vlc_threads.h>

/* Cedar */
#include <libcedarx/libcedarx.h>
#include <assert.h>


/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int        OpenDecoder  (vlc_object_t *);
static void       CloseDecoder (vlc_object_t *);

vlc_module_begin ()
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_VCODEC)
    set_description(N_("Cedar hardware video decoder"))
    set_capability("decoder", 0)
    set_callbacks(OpenDecoder, CloseDecoder)
    add_shortcut("cedar")

    add_integer ("cedar-rotation", 0, "Video Rotation Angle", "Video rotation angel from decoder side",
            false )
vlc_module_end ()

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static picture_t *DecodeBlock   (decoder_t *p_dec, block_t **pp_block);

/*****************************************************************************
 * decoder_sys_t : Cedar decoder structure
 *****************************************************************************/
struct decoder_sys_t
{
    uint8_t *data;
    uint32_t data_size;
    bool b_have_pts;
};

static void request_buffer(cedarx_picture_t * pic, void *sys) {
    decoder_t *p_dec = sys; 
    
    if (p_dec) {
        picture_t *p_pic = decoder_NewPicture(p_dec);    
        if (p_pic) {
            pic->y[0] = p_pic->p[0].p_pixels;
            pic->u[0] = p_pic->p[1].p_pixels;
            pic->size_y[0] = p_pic->p[0].i_pitch * p_pic->p[0].i_lines;
            pic->size_u[0] = p_pic->p[1].i_pitch * p_pic->p[1].i_lines;
        }
        pic->sys = p_pic;
    }
}

static void update_buffer(cedarx_picture_t * pic, void *sys) {
    picture_t *p_pic;

    VLC_UNUSED(sys);
    if (pic && pic->sys) {
        p_pic = pic->sys;
        p_pic->date = pic->pts;
        p_pic->format.i_width = pic->width;
        p_pic->format.i_height = pic->height;
        p_pic->format.i_x_offset = pic->top_offset;
        p_pic->format.i_y_offset = pic->left_offset;
        p_pic->format.i_visible_width = pic->display_width;
        p_pic->format.i_visible_height = pic->display_height;
        p_pic->format.i_frame_rate = pic->frame_rate;
        p_pic->format.i_frame_rate_base = 1000;
        p_pic->b_progressive = pic->is_progressive? true: false;
        p_pic->b_top_field_first = pic->top_field_first? true: false;
        p_pic->i_nb_fields = p_pic->b_progressive? 1: 2;
    }    
}

static void release_buffer(cedarx_picture_t * pic, void *sys) {
    picture_t *p_pic;
    decoder_t *p_dec = sys; 
    
    if (pic && pic->sys && p_dec) {
        p_pic = pic->sys;
        decoder_DeletePicture(p_dec, p_pic);
    }
}

static void lock_buffer(cedarx_picture_t * pic, void *sys) {
    picture_t *p_pic;
    decoder_t *p_dec = sys; 

    if (pic && pic->sys && p_dec) {
        p_pic = pic->sys;
        decoder_LinkPicture(p_dec, p_pic);
    }
}

static void unlock_buffer(cedarx_picture_t * pic, void *sys) {
    picture_t *p_pic;
    decoder_t *p_dec = sys; 

    if (pic && pic->sys && p_dec) {
        p_pic = pic->sys;
        decoder_UnlinkPicture(p_dec, p_pic);    
    }
}

/*****************************************************************************
* OpenDecoder: probe the decoder and return score
*****************************************************************************/
static int OpenDecoder(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t*)p_this;
    decoder_sys_t *p_sys;
    cedarx_info_t info;

    /* Allocate the memory needed to store the decoder's structure */
    p_sys = malloc(sizeof(decoder_sys_t));
    if(!p_sys)
        return VLC_ENOMEM;

    /* Fill decoder_sys_t */
    p_dec->p_sys = p_sys;
    p_sys->data = NULL;
	p_sys->data_size = 0;
	p_sys->b_have_pts = false;
    
    memset(&info, 0, sizeof(cedarx_info_t));
    
    /* Codec specifics */
    switch (p_dec->fmt_in.i_codec) {
        case VLC_CODEC_H264:
            info.stream = CEDARX_STREAM_FORMAT_H264;
            break;
//        case VLC_CODEC_WMV3:
//        case VLC_CODEC_VC1:
//            info.stream = CEDARX_STREAM_FORMAT_VC1;
//            break;
        case VLC_CODEC_H263:
            info.stream = CEDARX_STREAM_FORMAT_H263;
            break;
        case VLC_CODEC_FLV1:
            info.stream = CEDARX_STREAM_FORMAT_SORENSSON_H263;
            break;
        case VLC_CODEC_MJPG:
            info.stream = CEDARX_STREAM_FORMAT_MJPEG;
            break;
        case VLC_CODEC_VP6:
            info.stream = CEDARX_STREAM_FORMAT_VP6;
            break;
        case VLC_CODEC_VP8:
            info.stream = CEDARX_STREAM_FORMAT_VP8;
            break;
        case VLC_CODEC_WMV1:
            info.stream = CEDARX_STREAM_FORMAT_WMV1;
            break;
        case VLC_CODEC_WMV2:
            info.stream = CEDARX_STREAM_FORMAT_WMV2;
            break;
//        case VLC_CODEC_RV10:
//        case VLC_CODEC_RV13:
//        case VLC_CODEC_RV20:
//        case VLC_CODEC_RV30:
//        case VLC_CODEC_RV40:
//            info.stream = CEDARX_STREAM_FORMAT_REALVIDEO;
//            p_sys->data_size = p_dec->fmt_in.i_extra + 32;
//            p_sys->data = malloc(p_sys->data_size);
//            if (!p_sys->data)
//                goto free_out;
//
//            memset(p_sys->data, 0, p_sys->data_size);
//            SetDWLE(&p_sys->data[0], p_dec->fmt_in.i_extra + 26);
//            SetDWBE(&p_sys->data[4], VLC_FOURCC('V', 'I', 'D', 'O'));
//            SetDWBE(&p_sys->data[8], p_dec->fmt_in.i_codec);
//            SetWLE(&p_sys->data[12], p_dec->fmt_in.video.i_width);
//            SetWLE(&p_sys->data[14], p_dec->fmt_in.video.i_height);
//            SetWLE(&p_sys->data[16], 12);
//            SetDWLE(&p_sys->data[24], (p_dec->fmt_in.video.i_frame_rate << 16) / 
//                                            p_dec->fmt_in.video.i_frame_rate_base );
//            SetDWLE(&p_sys->data[28], p_dec->fmt_in.i_extra);
//
//            memcpy(&p_sys->data[32], p_dec->fmt_in.p_extra, p_dec->fmt_in.i_extra);
//            info.data = p_sys->data;
//            info.data_size = p_sys->data_size;
//            break;
        case VLC_CODEC_CAVS:
            info.stream = CEDARX_STREAM_FORMAT_AVS;
            break;
        case VLC_CODEC_MP4V:
            switch(p_dec->fmt_in.i_original_fourcc)
            {
                case VLC_FOURCC('m','p','4','v'):
                case VLC_FOURCC('M','P','4','V'):
                case VLC_FOURCC('m','p','4','s'):
                case VLC_FOURCC('M','P','4','S'):
                case VLC_FOURCC('p','m','p','4'):
                case VLC_FOURCC('P','M','P','4'):
                case VLC_FOURCC('f','m','p','4'):
                case VLC_FOURCC('F','M','P','4'):
                case VLC_FOURCC('x','v','i','d'):
                case VLC_FOURCC('X','V','I','D'):
                case VLC_FOURCC('X','v','i','D'):
                case VLC_FOURCC('X','V','I','X'):
                case VLC_FOURCC('x','v','i','x'):
                    info.stream = CEDARX_STREAM_FORMAT_XVID;
                    break;
                case VLC_FOURCC('d','i','v','x'):
                case VLC_FOURCC('D','I','V','X'):
                    info.stream = CEDARX_STREAM_FORMAT_DIVX4;
                    break;
                case VLC_FOURCC('D','X','5','0'):
                case VLC_FOURCC('d','x','5','0'):
                    info.stream = CEDARX_STREAM_FORMAT_DIVX5;
                    break;
                default:
                    goto free_out;
            }
            break;
        case VLC_CODEC_DIV1:
            info.stream = CEDARX_STREAM_FORMAT_DIVX1;
            break;
        case VLC_CODEC_DIV2:
            info.stream = CEDARX_STREAM_FORMAT_DIVX2;
            break;
        case VLC_CODEC_DIV3:
            switch(p_dec->fmt_in.i_original_fourcc)
            {
                case VLC_FOURCC('d','i','v','3'):
                case VLC_FOURCC('D','I','V','3'):
                case VLC_FOURCC('m','p','g','3'):
                case VLC_FOURCC('M','P','G','3'):
                case VLC_FOURCC('m','p','4','3'):
                case VLC_FOURCC('M','P','4','3'):
                case VLC_FOURCC('d','i','v','4'):
                case VLC_FOURCC('D','I','V','4'):
                case VLC_FOURCC('d','i','v','f'):
                case VLC_FOURCC('D','I','V','F'):
                    info.stream = CEDARX_STREAM_FORMAT_DIVX3;
                    break;
                case VLC_FOURCC('d','i','v','5'):
                case VLC_FOURCC('D','I','V','5'):
                    info.stream = CEDARX_STREAM_FORMAT_DIVX5;
                    break;
                default:
                    goto free_out;
            }
            break;
        case VLC_CODEC_MPGV:
            switch(p_dec->fmt_in.i_original_fourcc)
            {
                case VLC_FOURCC('m','p','g','1'):
                case VLC_FOURCC('m','p','1','v'):
                  info.stream = CEDARX_STREAM_FORMAT_MPEG1;
                  break;
                default:
                  info.stream = CEDARX_STREAM_FORMAT_MPEG2;
                  break;
            }
            break;
        default:
            goto free_out;
    }

    info.container = CEDARX_CONTAINER_FORMAT_UNKNOW;
    info.width = p_dec->fmt_in.video.i_width;
    info.height = p_dec->fmt_in.video.i_height;
    info.rot = var_CreateGetInteger(p_dec, "cedar-rotation");
    msg_Info(p_dec, "Using decoder rotation %d degree on cedar", info.rot);

    if (!info.data && !info.data_size) {
        info.data = p_dec->fmt_in.p_extra;
        info.data_size = p_dec->fmt_in.i_extra;
    }
    
    if(p_dec->fmt_in.video.i_frame_rate > 0 && p_dec->fmt_in.video.i_frame_rate_base > 0) {
        info.frame_rate = INT64_C(1000) * 
                p_dec->fmt_in.video.i_frame_rate / 
                p_dec->fmt_in.video.i_frame_rate_base;
        info.frame_duration = INT64_C(1000000) * 
                p_dec->fmt_in.video.i_frame_rate_base / 
                p_dec->fmt_in.video.i_frame_rate;
    }

    info.sys = p_dec;
    info.request_buffer = request_buffer;
    info.update_buffer = update_buffer;
    info.release_buffer = release_buffer;
    info.lock_buffer = lock_buffer;
    info.unlock_buffer = unlock_buffer;

    /* Open the device */
    if(libcedarx_decoder_open(&info) < 0) {
        msg_Err(p_dec, "Couldn't find and open the Cedar device");  
		goto out;
    }
        
    /* Set output properties */
    switch (p_dec->fmt_in.i_codec) {
        case VLC_CODEC_MJPG:
            p_dec->fmt_out.i_codec = VLC_CODEC_MV16;
            break;
        case VLC_CODEC_H264:
            p_dec->i_extra_picture_buffers = 4;
        default:
            p_dec->fmt_out.i_codec = VLC_CODEC_MV12;
            break;
    }
    
    p_dec->fmt_out.i_cat            = VIDEO_ES;
    p_dec->fmt_out.video.i_width    = p_dec->fmt_in.video.i_width;
    p_dec->fmt_out.video.i_height   = p_dec->fmt_in.video.i_height;
    p_dec->fmt_out.video.i_sar_num  = p_dec->fmt_in.video.i_sar_num;
    p_dec->fmt_out.video.i_sar_den  = p_dec->fmt_in.video.i_sar_den;
    p_dec->b_need_packetized        = true;
    p_dec->b_need_eos               = true; 

    /* Set callbacks */
    p_dec->pf_decode_video = DecodeBlock;
    msg_Dbg(p_dec, "Opened Cedar device with success");

    return VLC_SUCCESS;

out:
	if (p_sys->data && p_sys->data_size)
		free(p_sys->data);
free_out:
	free(p_sys);
	return VLC_EGENERIC;
}

/*****************************************************************************
 * CloseDecoder: decoder destruction
 *****************************************************************************/
static void CloseDecoder(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    msg_Dbg(p_dec, "done cleaning up Cedar");
    if (p_sys) {
        libcedarx_decoder_close();
        if (p_sys->data && p_sys->data_size)
            free(p_sys->data);
        free(p_sys);
        p_dec->p_sys = NULL;
    }
}

/****************************************************************************
 * DecodeBlock: the whole thing
 ****************************************************************************/
static picture_t *DecodeBlock(decoder_t *p_dec, block_t **pp_block)
{
    decoder_sys_t *p_sys = p_dec->p_sys;
    block_t *p_block;
    picture_t *p_pic;
    mtime_t i_pts;

    if(!p_sys || !pp_block)
        return NULL;
        
    p_block = *pp_block;
    if(p_block) {
        if (p_block->i_flags & BLOCK_FLAG_END_OF_STREAM) {
            while (!libcedarx_decoder_decode_stream(true));            
        } else if (!(p_block->i_flags&(BLOCK_FLAG_DISCONTINUITY|BLOCK_FLAG_CORRUPTED))) {
            if (p_block->i_pts > VLC_TS_INVALID) {
                i_pts = p_block->i_pts;
                p_sys->b_have_pts = true;
            } else {
                if (!p_sys->b_have_pts) {
                    i_pts = p_block->i_dts;
                } else {
                    i_pts = -1;
                }
            }
            
            if (libcedarx_decoder_add_stream(p_block->p_buffer, p_block->i_buffer, i_pts, 0) < 0)
                msg_Warn(p_dec, "Failed to add stream!");
    
            libcedarx_decoder_decode_stream(false);
        }
        
        /* Make sure we don't reuse the same timestamps twice */
        p_block->i_pts = p_block->i_dts = VLC_TS_INVALID;
        block_Release(p_block);
        *pp_block = NULL;
    }

    p_pic = libcedarx_decoder_request_frame();
    if (p_pic) {
        if (!p_dec->fmt_out.video.i_width || !p_dec->fmt_out.video.i_height) {
            p_dec->fmt_out.video.i_width = p_pic->format.i_width;
            p_dec->fmt_out.video.i_height = p_pic->format.i_height;
        }
    }

    return p_pic;
}
