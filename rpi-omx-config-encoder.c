#include "rpi-omx-utils.hpp"
#include "rpi-video-params.hpp"


void config_omx_encoder_out(OmxEncoderModule *encodermodule, OMX_U32 width, OMX_U32 height, OMX_U32 framerate, OMX_U32 stride, OMX_U32 encbitrate)
{
    OMX_ERRORTYPE r;

    say("Default port definition for encoder input port 200");
    dump_port(encodermodule->encoder, 200, OMX_TRUE);
    say("Default port definition for encoder output port 201");
    dump_port(encodermodule->encoder, 201, OMX_TRUE);

    // Encoder input port definition is done automatically upon tunneling

    // Configure video format emitted by encoder output port
    OMX_PARAM_PORTDEFINITIONTYPE encoder_portdef;
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 201;
    if((r = OMX_GetParameter(encodermodule->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder output port 201");
    }
    // Copy some of the encoder output port configuration from camera output port
    encoder_portdef.format.video.nFrameWidth  = width;
    encoder_portdef.format.video.nFrameHeight = height;
    encoder_portdef.format.video.xFramerate   = framerate;
    encoder_portdef.format.video.nStride      = stride;
    // Which one is effective, this or the configuration just below?
    encoder_portdef.format.video.nBitrate     = encbitrate;
    if((r = OMX_SetParameter(encodermodule->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for encoder output port 201");
    }
    // Configure bitrate
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    OMX_INIT_STRUCTURE(bitrate);
    bitrate.eControlRate = OMX_Video_ControlRateVariable;
    bitrate.nTargetBitrate = encoder_portdef.format.video.nBitrate;
    bitrate.nPortIndex = 201;
    if((r = OMX_SetParameter(encodermodule->encoder, OMX_IndexParamVideoBitrate, &bitrate)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set bitrate for encoder output port 201");
    }
    // Configure format
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_INIT_STRUCTURE(format);
    format.nPortIndex = 201;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    if((r = OMX_SetParameter(encodermodule->encoder, OMX_IndexParamVideoPortFormat, &format)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set video format for encoder output port 201");
    }
}

void config_omx_encoder_in_out(OmxEncoderModule *mod, OMX_U32 width, OMX_U32 height, OMX_U32 framerate, OMX_U32 encbitrate)
{
    OMX_ERRORTYPE r;

    say("Default port definition for encoder input port 200");
    dump_port(mod->encoder, 200, OMX_TRUE);
    say("Default port definition for encoder output port 201");
    dump_port(mod->encoder, 201, OMX_TRUE);

    OMX_PARAM_PORTDEFINITIONTYPE encoder_portdef;
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 200;
    if((r = OMX_GetParameter(mod->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder input port 200");
    }
    encoder_portdef.format.video.nFrameWidth  = width;
    encoder_portdef.format.video.nFrameHeight = height;
    encoder_portdef.format.video.xFramerate   = framerate << 16;
    // Stolen from gstomxvideodec.c of gst-omx
    encoder_portdef.format.video.nStride      = (encoder_portdef.format.video.nFrameWidth + encoder_portdef.nBufferAlignment - 1) & (~(encoder_portdef.nBufferAlignment - 1));
    encoder_portdef.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    if((r = OMX_SetParameter(mod->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for encoder input port 200");
    }

    // Copy encoder input port definition as basis encoder output port definition
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 200;
    if((r = OMX_GetParameter(mod->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder input port 200");
    }
    encoder_portdef.nPortIndex = 201;
    encoder_portdef.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    encoder_portdef.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    // Which one is effective, this or the configuration just below?
    encoder_portdef.format.video.nBitrate     = encbitrate;
    if((r = OMX_SetParameter(mod->encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for encoder output port 201");
    }
    // Configure bitrate
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    OMX_INIT_STRUCTURE(bitrate);
    bitrate.eControlRate = OMX_Video_ControlRateVariable;
    bitrate.nTargetBitrate = encoder_portdef.format.video.nBitrate;
    bitrate.nPortIndex = 201;
    if((r = OMX_SetParameter(mod->encoder, OMX_IndexParamVideoBitrate, &bitrate)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set bitrate for encoder output port 201");
    }
    // Configure format
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_INIT_STRUCTURE(format);
    format.nPortIndex = 201;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    if((r = OMX_SetParameter(mod->encoder, OMX_IndexParamVideoPortFormat, &format)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set video format for encoder output port 201");
    }
}
