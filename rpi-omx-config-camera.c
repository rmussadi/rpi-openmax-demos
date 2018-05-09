#include "rpi-i420-framing.hpp"
#include "rpi-camera-params.hpp"
#include "rpi-video-params.hpp"


void config_omx_camera(OMX_U32 cam_width, OMX_U32 cam_height, OMX_U32 cam_framerate)
{
    say("Configuring camera...");

    say("Default port definition for camera input port 73");
    dump_port(ctx.camera, 73, OMX_TRUE);
    say("Default port definition for camera preview output port 70");
    dump_port(ctx.camera, 70, OMX_TRUE);
    say("Default port definition for camera video output port 71");
    dump_port(ctx.camera, 71, OMX_TRUE);

    // Request a callback to be made when OMX_IndexParamCameraDeviceNumber is
    // changed signaling that the camera device is ready for use.
    OMX_CONFIG_REQUESTCALLBACKTYPE cbtype;
    OMX_INIT_STRUCTURE(cbtype);
    cbtype.nPortIndex = OMX_ALL;
    cbtype.nIndex     = OMX_IndexParamCameraDeviceNumber;
    cbtype.bEnable    = OMX_TRUE;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigRequestCallback, &cbtype)) != OMX_ErrorNone) {
        omx_die(r, "Failed to request camera device number parameter change callback for camera");
    }
    // Set device number, this triggers the callback configured just above
    OMX_PARAM_U32TYPE device;
    OMX_INIT_STRUCTURE(device);
    device.nPortIndex = OMX_ALL;
    device.nU32 = CAM_DEVICE_NUMBER;
    if((r = OMX_SetParameter(ctx.camera, OMX_IndexParamCameraDeviceNumber, &device)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera parameter device number");
    }
    // Configure video format emitted by camera preview output port
    OMX_PARAM_PORTDEFINITIONTYPE camera_portdef;
    OMX_INIT_STRUCTURE(camera_portdef);
    camera_portdef.nPortIndex = 70;
    if((r = OMX_GetParameter(ctx.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for camera preview output port 70");
    }
    camera_portdef.format.video.nFrameWidth  = cam_width;
    camera_portdef.format.video.nFrameHeight = cam_height;
    camera_portdef.format.video.xFramerate   = cam_framerate << 16;
    // Stolen from gstomxvideodec.c of gst-omx
    camera_portdef.format.video.nStride      = (camera_portdef.format.video.nFrameWidth + camera_portdef.nBufferAlignment - 1) & (~(camera_portdef.nBufferAlignment - 1));
    camera_portdef.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    if((r = OMX_SetParameter(ctx.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for camera preview output port 70");
    }
    // Configure video format emitted by camera video output port
    // Use configuration from camera preview output as basis for
    // camera video output configuration
    OMX_INIT_STRUCTURE(camera_portdef);
    camera_portdef.nPortIndex = 70;
    if((r = OMX_GetParameter(ctx.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for camera preview output port 70");
    }
    camera_portdef.nPortIndex = 71;
    if((r = OMX_SetParameter(ctx.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for camera video output port 71");
    }
    // Configure frame rate
    OMX_CONFIG_FRAMERATETYPE framerate;
    OMX_INIT_STRUCTURE(framerate);
    framerate.nPortIndex = 70;
    framerate.xEncodeFramerate = camera_portdef.format.video.xFramerate;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigVideoFramerate, &framerate)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set framerate configuration for camera preview output port 70");
    }
    framerate.nPortIndex = 71;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigVideoFramerate, &framerate)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set framerate configuration for camera video output port 71");
    }
    // Configure sharpness
    OMX_CONFIG_SHARPNESSTYPE sharpness;
    OMX_INIT_STRUCTURE(sharpness);
    sharpness.nPortIndex = OMX_ALL;
    sharpness.nSharpness = CAM_SHARPNESS;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonSharpness, &sharpness)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera sharpness configuration");
    }
    // Configure contrast
    OMX_CONFIG_CONTRASTTYPE contrast;
    OMX_INIT_STRUCTURE(contrast);
    contrast.nPortIndex = OMX_ALL;
    contrast.nContrast = CAM_CONTRAST;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonContrast, &contrast)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera contrast configuration");
    }
    // Configure saturation
    OMX_CONFIG_SATURATIONTYPE saturation;
    OMX_INIT_STRUCTURE(saturation);
    saturation.nPortIndex = OMX_ALL;
    saturation.nSaturation = CAM_SATURATION;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonSaturation, &saturation)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera saturation configuration");
    }
    // Configure brightness
    OMX_CONFIG_BRIGHTNESSTYPE brightness;
    OMX_INIT_STRUCTURE(brightness);
    brightness.nPortIndex = OMX_ALL;
    brightness.nBrightness = CAM_BRIGHTNESS;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonBrightness, &brightness)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera brightness configuration");
    }
    // Configure exposure value
    OMX_CONFIG_EXPOSUREVALUETYPE exposure_value;
    OMX_INIT_STRUCTURE(exposure_value);
    exposure_value.nPortIndex = OMX_ALL;
    exposure_value.xEVCompensation = CAM_EXPOSURE_VALUE_COMPENSTAION;
    exposure_value.bAutoSensitivity = CAM_EXPOSURE_AUTO_SENSITIVITY;
    exposure_value.nSensitivity = CAM_EXPOSURE_ISO_SENSITIVITY;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonExposureValue, &exposure_value)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera exposure value configuration");
    }
    // Configure frame frame stabilisation
    OMX_CONFIG_FRAMESTABTYPE frame_stabilisation_control;
    OMX_INIT_STRUCTURE(frame_stabilisation_control);
    frame_stabilisation_control.nPortIndex = OMX_ALL;
    frame_stabilisation_control.bStab = CAM_FRAME_STABILISATION;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonFrameStabilisation, &frame_stabilisation_control)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera frame frame stabilisation control configuration");
    }
    // Configure frame white balance control
    OMX_CONFIG_WHITEBALCONTROLTYPE white_balance_control;
    OMX_INIT_STRUCTURE(white_balance_control);
    white_balance_control.nPortIndex = OMX_ALL;
    white_balance_control.eWhiteBalControl = CAM_WHITE_BALANCE_CONTROL;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonWhiteBalance, &white_balance_control)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera frame white balance control configuration");
    }
    // Configure image filter
    OMX_CONFIG_IMAGEFILTERTYPE image_filter;
    OMX_INIT_STRUCTURE(image_filter);
    image_filter.nPortIndex = OMX_ALL;
    image_filter.eImageFilter = CAM_IMAGE_FILTER;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonImageFilter, &image_filter)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set camera image filter configuration");
    }
    // Configure mirror
    OMX_MIRRORTYPE eMirror = OMX_MirrorNone;
    if(CAM_FLIP_HORIZONTAL && !CAM_FLIP_VERTICAL) {
        eMirror = OMX_MirrorHorizontal;
    } else if(!CAM_FLIP_HORIZONTAL && CAM_FLIP_VERTICAL) {
        eMirror = OMX_MirrorVertical;
    } else if(CAM_FLIP_HORIZONTAL && CAM_FLIP_VERTICAL) {
        eMirror = OMX_MirrorBoth;
    }
    OMX_CONFIG_MIRRORTYPE mirror;
    OMX_INIT_STRUCTURE(mirror);
    mirror.nPortIndex = 71;
    mirror.eMirror = eMirror;
    if((r = OMX_SetConfig(ctx.camera, OMX_IndexConfigCommonMirror, &mirror)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set mirror configuration for camera video output port 71");
    }

    // Ensure camera is ready
    while(!ctx.camera_ready) {
        usleep(10000);
    }
}
