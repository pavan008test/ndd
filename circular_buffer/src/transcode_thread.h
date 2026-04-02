/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Anirudh Maringanti <anirudh.maringanti@netradyne.com>, November 2017 - January 2018
 */


#ifndef TRANSCODE_THREAD_H
#define TRANSCODE_THREAD_H


#include "circular_buffer.h"
#include <stdio.h>

#include <cstdlib>
#include <set>
#include <unistd.h>
#include <curl/curl.h>
#include <nd_msg_utils.h> 

//for mount command
#include <sys/mount.h>
#include <errno.h>

// for nd_timed_task function
#include <nd_task.h>

// for gstreamer
//#include <gst/gst.h>

#include <glob.h>
#if 0
typedef struct transcode_ctxt {

    GstElement *filesrc;
    GstElement *demuxer;
    GstElement *src_queue;
    GstElement *parse;
    GstElement *src_dec;
    GstElement *conv;
    GstElement *dst_caps;
    GstElement *enc;
    GstElement *muxer;
    GstElement *filesink;

    GstElement *pipeline;
    
    //dst_caps params
    gint transcode_width;
    gint transcode_height;

    //encoder params
    gint transcode_bitrate;
    gint transcode_quality_level;
    gint transcode_preset_level;
    gint transcode_control_rate;
    gint transcode_iframeinterval;

#ifdef GST_PROBE_LOGS    
    //for probing
    GstPad *demux_pad;
    GstPad *dec_pad;
    GstPad *nvvid_pad;
    GstPad *enc_pad;
    GstPad *mux_pad;

    gulong demux_probe_id;
    gulong dec_probe_id;
    gulong nvvid_probe_id;
    gulong enc_probe_id;
    gulong mux_probe_id;
#endif

} transcode_ctxt;


class transcode_scheme{
    public:
        gint width;
        gint height;

        gint bitrate;
        gint quality_level;
        gint preset_level;
        gint control_rate;
        gint iframeinterval;
};
#endif

#endif
