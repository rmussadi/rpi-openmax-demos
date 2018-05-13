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
 * `rpi-camera-dump-yuv` records video using the RaspiCam module and dumps the raw
 * YUV frame data to `stdout`.
 *
 *     $ ./rpi-camera-dump-yuv >test.yuv
 *
 * `rpi-camera-dump-yuv` uses `camera` and `null_sink` components. Uncompressed
 * raw YUV frame data is read from the buffer of `camera` video output port and
 * dumped to stdout and `camera` preview output port is tunneled to `null_sink`
 * input port.
 *
 * Please see README.mdwn for more detailed description of this
 * OpenMAX IL demos for Raspberry Pi bundle.
 *
 */

#include "rpi-i420-framing.hpp"
#include "rpi-camera-params.hpp"
#include "rpi-video-params.hpp"

// Global variable used by the signal handler and capture loop
static int want_quit = 0;

// Our application context passed around
// the main routine and callback handlers
typedef struct
{
    appctx_sync sync_ ;
    OmxCameraModule cammodule_;

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

// Called by OMX when the camera component has filled
// the output buffer with captured video data
static OMX_ERRORTYPE fill_output_buffer_done_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer) {
    appctx *ctx = ((appctx*)pAppData);
    vcos_semaphore_wait(&ctx->sync_.handler_lock);
    // The main loop can now flush the buffer to output file
    ctx->cammodule_.camera_output_buffer_available = 1;
    vcos_semaphore_post(&ctx->sync_.handler_lock);
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
    callbacks.EventHandler   = event_handler;
    callbacks.FillBufferDone = fill_output_buffer_done_handler;

    init_component_handle("camera", &ctx.cammodule_.camera , &ctx, &callbacks);
    init_component_handle("null_sink", &ctx.null_sink, &ctx, &callbacks);

    say("Configuring camera...");
    config_omx_camera(&ctx.cammodule_, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FRAMERATE);
 
    say("Configuring null sink...");

    say("Default port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_TRUE);

    // Null sink input port definition is done automatically upon tunneling

    // Tunnel camera preview output port and null sink input port
    say("Setting up tunnel from camera preview output port 70 to null sink input port 240...");
    if((r = OMX_SetupTunnel(ctx.cammodule_.camera, 70, ctx.null_sink, 240)) != OMX_ErrorNone) {
        omx_die(r, "Failed to setup tunnel between camera preview output port 70 and null sink input port 240");
    }

    // Switch components to idle state
    say("Switching state of the camera component to idle...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
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
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortEnable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_TRUE);

    // Allocate camera input and video output buffers,
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
    camera_portdef.nPortIndex = 71;
    if((r = OMX_GetParameter(ctx.cammodule_.camera, OMX_IndexParamPortDefinition, &camera_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for camera vіdeo output port 71");
    }
    if((r = OMX_AllocateBuffer(ctx.cammodule_.camera, &ctx.cammodule_.camera_ppBuffer_out, 71, NULL, camera_portdef.nBufferSize)) != OMX_ErrorNone) {
        omx_die(r, "Failed to allocate buffer for camera video output port 71");
    }

    // Just use stdout for output
    say("Opening input and output files...");
    ctx.fd_out = stdout;

    // Switch state of the components prior to starting
    // the video capture loop
    say("Switching state of the camera component to executing...");
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to executing");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateExecuting);
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
    say("Configured port definition for null sink input port 240");
    dump_port(ctx.null_sink, 240, OMX_FALSE);

    i420_frame_info frame_info, buf_info;
    get_i420_frame_info(camera_portdef.format.image.nFrameWidth, camera_portdef.format.image.nFrameHeight, camera_portdef.format.image.nStride, camera_portdef.format.video.nSliceHeight, &frame_info);
    get_i420_frame_info(frame_info.buf_stride, frame_info.buf_slice_height, -1, -1, &buf_info);
    dump_frame_info("Destination frame", &frame_info);
    dump_frame_info("Source buffer", &buf_info);

    // Buffer representing an I420 frame where to unpack
    // the fragmented Y, U, and V plane spans from the OMX buffers
    char *frame = calloc(1, frame_info.size);
    if(frame == NULL) {
        die("Failed to allocate frame buffer");
    }

    // Some counters
    int frame_num = 1, buf_num = 0;
    size_t output_written, frame_bytes = 0, buf_size, buf_bytes_read = 0, buf_bytes_copied;
    int i;
    // I420 spec: U and V plane span size half of the size of the Y plane span size
    int max_spans_y = buf_info.height, max_spans_uv = max_spans_y / 2;
    int valid_spans_y, valid_spans_uv;
    // For unpack memory copy operation
    unsigned char *buf_start;
    int max_spans, valid_spans;
    int dst_offset, src_offset, span_size;
    // For controlling the loop
    int quit_detected = 0, quit_in_frame_boundry = 0, need_next_buffer_to_be_filled = 1;

    say("Enter capture loop, press Ctrl-C to quit...");

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    while(1) {
        // fill_output_buffer_done_handler() has marked that there's
        // a buffer for us to flush
        if(ctx.cammodule_.camera_output_buffer_available) {
            // Print a message if the user wants to quit, but don't exit
            // the loop until we are certain that we have processed
            // a full frame till end of the frame. This way we should always
            // avoid corruption of the last encoded at the expense of
            // small delay in exiting.
            if(want_quit && !quit_detected) {
                say("Exit signal detected, waiting for next frame boundry before exiting...");
                quit_detected = 1;
                quit_in_frame_boundry = ctx.cammodule_.camera_ppBuffer_out->nFlags & OMX_BUFFERFLAG_ENDOFFRAME;
            }
            if(quit_detected &&
                    (quit_in_frame_boundry ^
                    (ctx.cammodule_.camera_ppBuffer_out->nFlags & OMX_BUFFERFLAG_ENDOFFRAME))) {
                say("Frame boundry reached, exiting loop...");
                break;
            }
            // Start of the OMX buffer data
            buf_start = ctx.cammodule_.camera_ppBuffer_out->pBuffer
                + ctx.cammodule_.camera_ppBuffer_out->nOffset;
            // Size of the OMX buffer data;
            buf_size = ctx.cammodule_.camera_ppBuffer_out->nFilledLen;
            buf_bytes_read += buf_size;
            buf_bytes_copied = 0;
            // Detect the possibly non-full buffer in the last buffer of a frame
            valid_spans_y = max_spans_y
                - ((ctx.cammodule_.camera_ppBuffer_out->nFlags & OMX_BUFFERFLAG_ENDOFFRAME)
                    ? frame_info.buf_extra_padding
                    : 0);
            // I420 spec: U and V plane span size half of the size of the Y plane span size
            valid_spans_uv = valid_spans_y / 2;
            // Unpack Y, U, and V plane spans from the buffer to the I420 frame
            for(i = 0; i < 3; i++) {
                // Number of maximum and valid spans for this plane
                max_spans   = (i == 0 ? max_spans_y   : max_spans_uv);
                valid_spans = (i == 0 ? valid_spans_y : valid_spans_uv);
                dst_offset =
                    // Start of the plane span in the I420 frame
                    frame_info.p_offset[i] +
                    // Plane spans copied from the previous buffers
                    (buf_num * frame_info.p_stride[i] * max_spans);
                src_offset =
                    // Start of the plane span in the buffer
                    buf_info.p_offset[i];
                span_size =
                    // Plane span size multiplied by the available spans in the buffer
                    frame_info.p_stride[i] * valid_spans;
                memcpy(
                    // Destination starts from the beginning of the frame and move forward by offset
                    frame + dst_offset,
                    // Source starts from the beginning of the OMX component buffer and move forward by offset
                    buf_start + src_offset,
                    // The final plane span size, possible padding at the end of
                    // the plane span section in the buffer isn't included
                    // since the size is based on the final frame plane span size
                    span_size);
                buf_bytes_copied += span_size;
            }
            frame_bytes += buf_bytes_copied;
            buf_num++;
            say("Read %d bytes from buffer %d of frame %d, copied %d bytes from %d Y spans and %d U/V spans available",
                buf_size, buf_num, frame_num, buf_bytes_copied, valid_spans_y, valid_spans_uv);
            if(ctx.cammodule_.camera_ppBuffer_out->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) {
                // Dump the complete I420 frame
                say("Captured frame %d, %d packed bytes read, %d bytes unpacked, writing %d unpacked frame bytes",
                    frame_num, buf_bytes_read, frame_bytes, frame_info.size);
                if(frame_bytes != frame_info.size) {
                    die("Frame bytes read %d doesn't match the frame size %d",
                        frame_bytes, frame_info.size);
                }
                output_written = fwrite(frame, 1, frame_info.size, ctx.fd_out);
                if(output_written != frame_info.size) {
                    die("Failed to write to output file: Requested to write %d bytes, but only %d bytes written: %s",
                        frame_info.size, output_written, strerror(errno));
                }
                frame_num++;
                buf_num = 0;
                buf_bytes_read = 0;
                frame_bytes = 0;
                memset(frame, 0, frame_info.size);
            }
            need_next_buffer_to_be_filled = 1;
        }
        // Buffer flushed, request a new buffer to be filled by the camera component
        if(need_next_buffer_to_be_filled) {
            need_next_buffer_to_be_filled = 0;
            ctx.cammodule_.camera_output_buffer_available = 0;
            if((r = OMX_FillThisBuffer(ctx.cammodule_.camera, ctx.cammodule_.camera_ppBuffer_out)) != OMX_ErrorNone) {
                omx_die(r, "Failed to request filling of the output buffer on camera video output port 71");
            }
        }
        // Would be better to use signaling here but hey this works too
        usleep(10);
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

    // Return the last full buffer back to the camera component
    if((r = OMX_FillThisBuffer(ctx.cammodule_.camera, ctx.cammodule_.camera_ppBuffer_out)) != OMX_ErrorNone) {
        omx_die(r, "Failed to request filling of the output buffer on camera video output port 71");
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
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandPortDisable, 240, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable null sink input port 240");
    }
    block_until_port_changed(ctx.null_sink, 240, OMX_FALSE);

    // Free all the buffers
    if((r = OMX_FreeBuffer(ctx.cammodule_.camera, 73, ctx.cammodule_.camera_ppBuffer_in)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for camera input port 73");
    }
    if((r = OMX_FreeBuffer(ctx.cammodule_.camera, 71, ctx.cammodule_.camera_ppBuffer_out)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for camera video output port 71");
    }

    // Transition all the components to idle and then to loaded states
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to idle");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to idle");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.cammodule_.camera, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the camera component to loaded");
    }
    block_until_state_changed(ctx.cammodule_.camera, OMX_StateLoaded);
    if((r = OMX_SendCommand(ctx.null_sink, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the null sink component to loaded");
    }
    block_until_state_changed(ctx.null_sink, OMX_StateLoaded);

    // Free the component handles
    if((r = OMX_FreeHandle(ctx.cammodule_.camera)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free camera component handle");
    }
    if((r = OMX_FreeHandle(ctx.null_sink)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free null sink component handle");
    }

    // Exit
    fclose(ctx.fd_out);
    free(frame);

    vcos_semaphore_delete(&ctx.sync_.handler_lock);
    if((r = OMX_Deinit()) != OMX_ErrorNone) {
        omx_die(r, "OMX de-initalization failed");
    }

    say("Exit!");

    return 0;
}
