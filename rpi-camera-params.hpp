#pragma once

/*
 *
 */

// Hard coded parameters
#define CAM_DEVICE_NUMBER               0
#define CAM_SHARPNESS                   0                       // -100 .. 100
#define CAM_CONTRAST                    0                       // -100 .. 100
#define CAM_BRIGHTNESS                  50                      // 0 .. 100
#define CAM_SATURATION                  0                       // -100 .. 100
#define CAM_EXPOSURE_VALUE_COMPENSTAION 0
#define CAM_EXPOSURE_ISO_SENSITIVITY    100
#define CAM_EXPOSURE_AUTO_SENSITIVITY   OMX_FALSE
#define CAM_FRAME_STABILISATION         OMX_TRUE
#define CAM_WHITE_BALANCE_CONTROL       OMX_WhiteBalControlAuto // OMX_WHITEBALCONTROLTYPE
#define CAM_IMAGE_FILTER                OMX_ImageFilterNoise    // OMX_IMAGEFILTERTYPE
#define CAM_FLIP_HORIZONTAL             OMX_FALSE
#define CAM_FLIP_VERTICAL               OMX_FALSE


extern void config_omx_camera(OMX_U32 cam_width, OMX_U32 cam_height, OMX_U32 cam_framerate);
