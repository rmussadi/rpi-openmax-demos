#pragma once

/*
 * I420 frame stuff
 */
#include "rpi-omx-utils.hpp"

typedef struct
{
    int width;
    int height;
    size_t size;
    int buf_stride;
    int buf_slice_height;
    int buf_extra_padding;
    int p_offset[3];
    int p_stride[3];
} i420_frame_info;

// Stolen from video-info.c of gstreamer-plugins-base
#define ROUND_UP_2(num) (((num)+1)&~1)
#define ROUND_UP_4(num) (((num)+3)&~3)

extern void get_i420_frame_info(int width, int height, int buf_stride, int buf_slice_height, i420_frame_info *info);
extern void dump_frame_info(const char *message, const i420_frame_info *info);
extern void dump_event(OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
