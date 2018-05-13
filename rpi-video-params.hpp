#pragma once

// Hard coded parameters
#define VIDEO_WIDTH                     1920
#define VIDEO_HEIGHT                    1080
#define VIDEO_FRAMERATE                 25
#define VIDEO_BITRATE                   10000000

typedef struct
{
    OMX_HANDLETYPE encoder;
    OMX_BUFFERHEADERTYPE *encoder_ppBuffer_in;
    OMX_BUFFERHEADERTYPE *encoder_ppBuffer_out;
    int encoder_input_buffer_needed;
    int encoder_output_buffer_available;
} OmxEncoderModule;

extern void config_omx_encoder(OmxEncoderModule *encodermodule, OMX_U32 width, OMX_U32 height, OMX_U32 framerate, OMX_U32 stride, OMX_U32 bitrate);
