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
 * `rpi-camera-encode` records video using the RaspiCam module and encodes the
 * stream using the VideoCore hardware encoder using H.264 codec. The raw H.264
 * stream is emitted to `stdout`. In order to properly display the encoded video,
 * it must be wrapped inside a container format, e.g.
 * [Matroska](http://matroska.org/technical/specs/).
 *
 * The following exaple uses `mkvmerge` tool from the
 * [MKVToolNix](http://www.bunkus.org/videotools/mkvtoolnix/) software package to
 * create a Matroska video file from the recorded H.264 file and then play it using
 * [omxplayer](https://github.com/huceke/omxplayer) (although omxplayer happens to
 * deal also with the raw H.264 stream, but generally other players, such
 * [avplay](http://libav.org/avplay.html), don't).
 *
 *     $ ./rpi-camera-encode >test.h264
 *     # Press Ctrl-C to interrupt the recording...
 *     $ mkvmerge -o test.mkv test.h264
 *     $ omxplayer test.mkv
 *
 * `rpi-camera-encode` uses `camera`, `video_encode` and `null_sink` components.
 * `camera` video output port is tunneled to `video_encode` input port and
 * `camera` preview output port is tunneled to `null_sink` input port. H.264
 * encoded video is read from the buffer of `video_encode` output port and dumped
 * to `stdout`.
 *
 * Please see README.mdwn for more detailed description of this
 * OpenMAX IL demos for Raspberry Pi bundle.
 *
 */

#include "rpi-i420-framing.hpp"
#include "rpi-camera-params.hpp"
#include "rpi-video-params.hpp"

// Global variable used by the signal handler and capture/encoding loop
static int want_quit = 0;

// Our application context passed around
// the main routine and callback handlers
typedef struct {
    appctx_sync sync_ ;
    OmxCameraModule cammodule_;
    OmxEncoderModule encodermodule_;

    // display module
    //OMX_HANDLETYPE render;

    // null_sink module
    OMX_HANDLETYPE null_sink;

    // stdin/out
    //FILE *fd_in;
    FILE *fd_out;
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

// Called by OMX when the encoder component has filled
// the output buffer with H.264 encoded video data
static OMX_ERRORTYPE fill_output_buffer_done_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer)
{
    appctx *ctx = ((appctx*)pAppData);
    vcos_semaphore_wait(&ctx->sync_.handler_lock);
    // The main loop can now flush the buffer to output file
    ctx->encodermodule_.encoder_output_buffer_available = 1;
    vcos_semaphore_post(&ctx->sync_.handler_lock);
    return OMX_ErrorNone;
}

int main(int argc, char **argv)
{
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
    callbacks.EventHandler   = event_handler;
    callbacks.FillBufferDone = fill_output_buffer_done_handler;

    init_component_handle("camera", &ctx.cammodule_.camera , &ctx, &callbacks);
    init_component_handle("video_encode", &ctx.encodermodule_.encoder, &ctx, &callbacks);
    init_component_handle("null_sink", &ctx.null_sink, &ctx, &callbacks);

    say("Configuring camera...");
    config_omx_camera(&ctx.cammodule_, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FRAMERATE);
    
    say("Configuring encoder...");
    OMX_U32 stride = VIDEO_WIDTH;
    config_omx_encoder_out(&ctx.encodermodule_, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FRAMERATE, stride, VIDEO_BITRATE);

    say("Configuring null sink...");

    say("Default port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_TRUE);

    // Null sink input port definition is done automatically upon tunneling

    // Tunnel camera preview output port and null sink input port
    say("Setting up tunnel from camera preview output port 70 to null sink input port 240...");
    if((r = OMX_SetupTunnel(ctx.cammodule_.camera, 70, ctx.null_sink, 240)) != OMX_ErrorNone) {
        omx_die(r, "Failed to setup tunnel between camera preview output port 70 and null sink input port 240");
    }

    // Tunnel camera video output port and encoder input port
    say("Setting up tunnel from camera video output port 71 to encoder input port 200...");
    if((r = OMX_SetupTunnel(ctx.cammodule_.camera, 71, ctx.encodermodule_.encoder, 200)) != OMX_ErrorNone) {
        omx_die(r, "Failed to setup tunnel between camera video output port 71 and encoder input port 200");
    }

    // Switch components to idle state
    say("Switching state of the camera component to idle...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
    say("Switching state of the encoder component to idle...");
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to idle");
    }
    block_until_state_changed(ctx.encodermodule_.encoder, OMX_StateIdle);
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
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandPortEnable, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable encoder input port 200");
    }
    block_until_port_changed(ctx.encodermodule_.encoder, 200, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandPortEnable, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable encoder output port 201");
    }
    block_until_port_changed(ctx.encodermodule_.encoder, 201, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortEnable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_TRUE);

    // Allocate camera input buffer and encoder output buffer,
    // buffers for tunneled ports are allocated internally by OMX
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
    OMX_PARAM_PORTDEFINITIONTYPE encoder_portdef;
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 201;
    if((r = OMX_GetParameter(ctx.encodermodule_.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder output port 201");
    }
    if((r = OMX_AllocateBuffer(ctx.encodermodule_.encoder, &ctx.encodermodule_.encoder_ppBuffer_out, 201, NULL, encoder_portdef.nBufferSize)) != OMX_ErrorNone) {
        omx_die(r, "Failed to allocate buffer for encoder output port 201");
    }

    // Just use stdout for output
    say("Opening output file...");
    ctx.fd_out = stdout;

    // Switch state of the components prior to starting
    // the video capture and encoding loop
    say("Switching state of the camera component to executing...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to executing");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateExecuting);
    say("Switching state of the encoder component to executing...");
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to executing");
    }
    block_until_state_changed(ctx.encodermodule_.encoder, OMX_StateExecuting);
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
    say("Configured port definition for encoder input port 200");
    dump_port(ctx.encodermodule_.encoder, 200, OMX_FALSE);
    say("Configured port definition for encoder output port 201");
    dump_port(ctx.encodermodule_.encoder, 201, OMX_FALSE);
    say("Configured port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_FALSE);

    say("Enter capture and encode loop, press Ctrl-C to quit...");

    int quit_detected = 0, quit_in_keyframe = 0, need_next_buffer_to_be_filled = 1;
    size_t output_written;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    while(1) {
        // fill_output_buffer_done_handler() has marked that there's
        // a buffer for us to flush
        if(ctx.encodermodule_.encoder_output_buffer_available) {
            // Print a message if the user wants to quit, but don't exit
            // the loop until we are certain that we have processed
            // a full frame till end of the frame, i.e. we're at the end
            // of the current key frame if processing one or until
            // the next key frame is detected. This way we should always
            // avoid corruption of the last encoded at the expense of
            // small delay in exiting.
            if(want_quit && !quit_detected) {
                say("Exit signal detected, waiting for next key frame boundry before exiting...");
                quit_detected = 1;
                quit_in_keyframe = ctx.encodermodule_.encoder_ppBuffer_out->nFlags & OMX_BUFFERFLAG_SYNCFRAME;
            }
            if(quit_detected && (quit_in_keyframe ^ (ctx.encodermodule_.encoder_ppBuffer_out->nFlags & OMX_BUFFERFLAG_SYNCFRAME))) {
                say("Key frame boundry reached, exiting loop...");
                break;
            }
            // Flush buffer to output file
            output_written = fwrite(ctx.encodermodule_.encoder_ppBuffer_out->pBuffer + ctx.encodermodule_.encoder_ppBuffer_out->nOffset, 1, ctx.encodermodule_.encoder_ppBuffer_out->nFilledLen, ctx.fd_out);
            if(output_written != ctx.encodermodule_.encoder_ppBuffer_out->nFilledLen) {
                die("Failed to write to output file: %s", strerror(errno));
            }
            say("Read from output buffer and wrote to output file %d/%d", ctx.encodermodule_.encoder_ppBuffer_out->nFilledLen, ctx.encodermodule_.encoder_ppBuffer_out->nAllocLen);
            need_next_buffer_to_be_filled = 1;
        }
        // Buffer flushed, request a new buffer to be filled by the encoder component
        if(need_next_buffer_to_be_filled) {
            need_next_buffer_to_be_filled = 0;
            ctx.encodermodule_.encoder_output_buffer_available = 0;
            if((r = OMX_FillThisBuffer(ctx.encodermodule_.encoder, ctx.encodermodule_.encoder_ppBuffer_out)) != OMX_ErrorNone) {
                omx_die(r, "Failed to request filling of the output buffer on encoder output port 201");
            }
        }
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

    // Return the last full buffer back to the encoder component
    ctx.encodermodule_.encoder_ppBuffer_out->nFlags = OMX_BUFFERFLAG_EOS;
    if((r = OMX_FillThisBuffer(ctx.encodermodule_.encoder, ctx.encodermodule_.encoder_ppBuffer_out)) != OMX_ErrorNone) {
        omx_die(r, "Failed to request filling of the output buffer on encoder output port 201");
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
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandFlush, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of encoder input port 200");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandFlush, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of encoder output port 201");
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
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandPortDisable, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable encoder input port 200");
    }
    block_until_port_changed(ctx.encodermodule_.encoder, 200, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandPortDisable, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable encoder output port 201");
    }
    block_until_port_changed(ctx.encodermodule_.encoder, 201, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortDisable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_FALSE);

    // Free all the buffers
    if((r = OMX_FreeBuffer(ctx.cammodule_.camera, 73, ctx.cammodule_.camera_ppBuffer_in)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for camera input port 73");
    }
    if((r = OMX_FreeBuffer(ctx.encodermodule_.encoder, 201, ctx.encodermodule_.encoder_ppBuffer_out)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for encoder output port 201");
    }

    // Transition all the components to idle and then to loaded states
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to idle");
    }
    block_until_state_changed(ctx.encodermodule_.encoder, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to idle");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to loaded");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateLoaded);
    if((r = OMX_SendCommand(ctx.encodermodule_.encoder, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to loaded");
    }
    block_until_state_changed(ctx.encodermodule_.encoder, OMX_StateLoaded);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to loaded");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateLoaded);

    // Free the component handles
    if((r = OMX_FreeHandle(ctx.cammodule_.camera)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free camera component handle");
    }
    if((r = OMX_FreeHandle(ctx.encodermodule_.encoder)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free encoder component handle");
    }
    if((r = OMX_FreeHandle(ctx.null_sink)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free null sink component handle");
    }

    // Exit
    fclose(ctx.fd_out);

    vcos_semaphore_delete(&ctx.sync_.handler_lock);
    if((r = OMX_Deinit()) != OMX_ErrorNone) {
        omx_die(r, "OMX de-initalization failed");
    }

    say("Exit!");

    return 0;
}
