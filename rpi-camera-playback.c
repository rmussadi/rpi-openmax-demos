/*
 * Copyright © 2013 Tuomas Jormola <tj@solitudo.net> <http://solitudo.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * <http://www.apache.org/licenses/LICENSE-2.0>
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Short intro about this program:
 *
 * `rpi-camera-playback` records video using the RaspiCam module and displays it
 * on the Raspberry Pi frame buffer display device, i.e. it should be run on the
 * Raspbian console.
 *
 *     $ ./rpi-camera-playback
 *
 * `rpi-camera-playback` uses `camera`, `video_render` and `null_sink` components.
 * `camera` video output port is tunneled to `video_render` input port and
 * `camera` preview output port is tunneled to `null_sink` input port.
 * `video_render` component uses a display region to show the video on local
 * display.
 *
 * Please see README.mdwn for more detailed description of this
 * OpenMAX IL demos for Raspberry Pi bundle.
 *
 */


/*
 * I420 frame stuff
 */
#include "rpi-i420-framing.hpp"
#include "rpi-camera-params.hpp"
#include "rpi-video-params.hpp"


#define DISPLAY_DEVICE                  0

// Global variable used by the signal handler and capture/encoding loop
static int want_quit = 0;

// Our application context passed around
// the main routine and callback handlers
typedef struct {
    OmxCameraModule cammodule_;
    appctx_sync sync_;
    OMX_HANDLETYPE render;
    OMX_HANDLETYPE null_sink;
} appctx;


// Global signal handler for trapping SIGINT, SIGTERM, and SIGQUIT
static void signal_handler(int signal) {
    want_quit = 1;
}

// OMX calls this handler for all the events it emits
static OMX_ERRORTYPE event_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_EVENTTYPE eEvent,
        OMX_U32 nData1,
        OMX_U32 nData2,
        OMX_PTR pEventData) {

    dump_event(hComponent, eEvent, nData1, nData2);

    appctx *ctx = (appctx *)pAppData;

    switch(eEvent) {
        case OMX_EventCmdComplete:
            vcos_semaphore_wait(&ctx->sync_.handler_lock);
            if(nData1 == OMX_CommandFlush) {
                ctx->sync_.flushed = 1;
            }
            vcos_semaphore_post(&ctx->sync_.handler_lock);
            break;
        case OMX_EventParamOrConfigChanged:
            vcos_semaphore_wait(&ctx->sync_.handler_lock);
            if(nData2 == OMX_IndexParamCameraDeviceNumber) {
                ctx->cammodule_.camera_ready = 1;
            }
            vcos_semaphore_post(&ctx->sync_.handler_lock);
            break;
        case OMX_EventError:
            omx_die(nData1, "error event received");
            break;
        default:
            break;
    }

    return OMX_ErrorNone;
}

int main(int argc, char **argv) {
    bcm_host_init();

    OMX_ERRORTYPE r;

    if((r = OMX_Init()) != OMX_ErrorNone) {
        omx_die(r, "OMX initalization failed");
    }

    // Init context
    appctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if(vcos_semaphore_create(&ctx.sync_.handler_lock, "handler_lock", 1) != VCOS_SUCCESS) {
        die("Failed to create handler lock semaphore");
    }

    // Init component handles
    OMX_CALLBACKTYPE callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.EventHandler = event_handler;

    init_component_handle("camera", &ctx.cammodule_.camera , &ctx, &callbacks);
    init_component_handle("video_render", &ctx.render, &ctx, &callbacks);
    init_component_handle("null_sink", &ctx.null_sink, &ctx, &callbacks);

    OMX_U32 screen_width = 0, screen_height = 0;
    if(graphics_get_display_size(DISPLAY_DEVICE, &screen_width, &screen_height) < 0) {
        die("Failed to get display size");
    }
    say("Configuring camera...");
    config_omx_camera(&ctx.cammodule_, screen_width/2, screen_height/2, VIDEO_FRAMERATE);

    say("Configuring render...");
    say("Default port definition for render input port 90");
    dump_port(ctx.render, 90, OMX_TRUE);

    // Render input port definition is done automatically upon tunneling

    // Configure display region
    OMX_CONFIG_DISPLAYREGIONTYPE display_region;
    OMX_INIT_STRUCTURE(display_region);
    display_region.nPortIndex = 90;
    display_region.set = OMX_DISPLAY_SET_NUM | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_MODE | OMX_DISPLAY_SET_DEST_RECT;
    display_region.num = DISPLAY_DEVICE;
    display_region.fullscreen = OMX_FALSE;
    display_region.mode = OMX_DISPLAY_MODE_FILL;
    display_region.dest_rect.width = screen_width/2;
    display_region.dest_rect.height = screen_height/2;
    display_region.dest_rect.x_offset = display_region.dest_rect.width / 2;
    display_region.dest_rect.y_offset = display_region.dest_rect.height / 2;
    if((r = OMX_SetConfig(ctx.render, OMX_IndexConfigDisplayRegion, &display_region)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set display region for render output port 90");
    }

    say("Configuring null sink...");

    say("Default port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_TRUE);

    // Null sink input port definition is done automatically upon tunneling

    // Tunnel camera preview output port and null sink input port
    say("Setting up tunnel from camera preview output port 70 to null sink input port 240...");
    if((r = OMX_SetupTunnel(ctx.cammodule_.camera, 70, ctx.null_sink, 240)) != OMX_ErrorNone) {
        omx_die(r, "Failed to setup tunnel between camera preview output port 70 and null sink input port 240");
    }

    // Tunnel camera video output port and render input port
    say("Setting up tunnel from camera video output port 71 to render input port 90...");
    if((r = OMX_SetupTunnel(ctx.cammodule_.camera, 71, ctx.render, 90)) != OMX_ErrorNone) {
        omx_die(r, "Failed to setup tunnel between camera video output port 71 and render input port 90");
    }

    // Switch components to idle state
    say("Switching state of the camera component to idle...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
    say("Switching state of the render component to idle...");
    if((r = OMX_SendCommand(ctx.render, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the render component to idle");
    }
    block_until_state_changed(ctx.render, OMX_StateIdle);
    say("Switching state of the null sink component to idle...");
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to idle");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateIdle);

    // Enable ports
    say("Enabling ports...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortEnable, 73, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable camera input port 73");
    }
    block_until_port_changed(ctx.cammodule_.camera, 73, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortEnable, 70, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable camera preview output port 70");
    }
    block_until_port_changed(ctx.cammodule_.camera, 70, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortEnable, 71, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable camera video output port 71");
    }
    block_until_port_changed(ctx.cammodule_.camera, 71, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.render, OMX_CommandPortEnable, 90, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable render input port 90");
    }
    block_until_port_changed(ctx.render, 90, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortEnable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_TRUE);

    // Allocate camera input buffer, buffers for tunneled
    // ports are allocated internally by OMX
    say("Allocating buffers...");
    OMX_PARAM_PORTDEFINITIONTYPE camera_portdef;
    OMX_INIT_STRUCTURE(camera_portdef);
    camera_portdef.nPortIndex = 73;
    if((r = OMX_GetParameter(ctx.cammodule_.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for camera input port 73");
    }
    if((r = OMX_AllocateBuffer(ctx.cammodule_.camera, &ctx.cammodule_.camera_ppBuffer_in, 73, NULL, camera_portdef.nBufferSize)) != OMX_ErrorNone) {
        omx_die(r, "Failed to allocate buffer for camera input port 73");
    }

    // Switch state of the components prior to starting
    // the video capture and encoding loop
    say("Switching state of the camera component to executing...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to executing");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateExecuting);
    say("Switching state of the render component to executing...");
    if((r = OMX_SendCommand(ctx.render, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the render component to executing");
    }
    block_until_state_changed(ctx.render, OMX_StateExecuting);
    say("Switching state of the null sink component to executing...");
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to executing");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateExecuting);

    // Start capturing video with the camera
    say("Switching on capture on camera video output port 71...");
    OMX_CONFIG_PORTBOOLEANTYPE capture;
    OMX_INIT_STRUCTURE(capture);
    capture.nPortIndex = 71;
    capture.bEnabled = OMX_TRUE;
    if((r = OMX_SetParameter(ctx.cammodule_.camera, OMX_IndexConfigPortCapturing, &capture)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch on capture on camera video output port 71");
    }

    say("Configured port definition for camera input port 73");
    dump_port(ctx.cammodule_.camera, 73, OMX_FALSE);
    say("Configured port definition for camera preview output port 70");
    dump_port(ctx.cammodule_.camera, 70, OMX_FALSE);
    say("Configured port definition for camera video output port 71");
    dump_port(ctx.cammodule_.camera, 71, OMX_FALSE);
    say("Configured port definition for render input port 90");
    dump_port(ctx.render, 90, OMX_FALSE);
    say("Configured port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_FALSE);

    say("Enter capture and playback loop, press Ctrl-C to quit...");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    while(!want_quit) {
        // Would be better to use signaling here but hey this works too
        usleep(1000);
    }
    say("Cleaning up...");

    // Restore signal handlers
    signal(SIGINT,  SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    // Stop capturing video with the camera
    OMX_INIT_STRUCTURE(capture);
    capture.nPortIndex = 71;
    capture.bEnabled = OMX_FALSE;
    if((r = OMX_SetParameter(ctx.cammodule_.camera, OMX_IndexConfigPortCapturing, &capture)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch off capture on camera video output port 71");
    }

    // Flush the buffers on each component
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandFlush, 73, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of camera input port 73");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandFlush, 70, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of camera preview output port 70");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandFlush, 71, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of camera video output port 71");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.render, OMX_CommandFlush, 90, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of render input port 90");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandFlush, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of null sink input port 240");
    }
    block_until_flushed(&ctx.sync_);

    // Disable all the ports
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortDisable, 73, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable camera input port 73");
    }
    block_until_port_changed(ctx.cammodule_.camera, 73, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortDisable, 70, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable camera preview output port 70");
    }
    block_until_port_changed(ctx.cammodule_.camera, 70, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandPortDisable, 71, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable camera video output port 71");
    }
    block_until_port_changed(ctx.cammodule_.camera, 71, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.render, OMX_CommandPortDisable, 90, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable render input port 90");
    }
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortDisable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_FALSE);

    // Free all the buffers
    if((r = OMX_FreeBuffer(ctx.cammodule_.camera, 73, ctx.cammodule_.camera_ppBuffer_in)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for camera input port 73");
    }

    // Transition all the components to idle and then to loaded states
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.render, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the render component to idle");
    }
    block_until_state_changed(ctx.render, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to idle");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to loaded");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateLoaded);
    if((r = OMX_SendCommand(ctx.render, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the render component to loaded");
    }
    block_until_state_changed(ctx.render, OMX_StateLoaded);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to loaded");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateLoaded);

    // Free the component handles
    if((r = OMX_FreeHandle(ctx.cammodule_.camera)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free camera component handle");
    }
    if((r = OMX_FreeHandle(ctx.render)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free render component handle");
    }
    if((r = OMX_FreeHandle(ctx.null_sink)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free null sink component handle");
    }

    // Exit
    vcos_semaphore_delete(&ctx.sync_.handler_lock);
    if((r = OMX_Deinit()) != OMX_ErrorNone) {
        omx_die(r, "OMX de-initalization failed");
    }

    say("Exit!");

    return 0;
}
