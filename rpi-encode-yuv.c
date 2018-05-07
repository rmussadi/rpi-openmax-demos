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
 * `rpi-encode-yuv` reads raw YUV frame data from `stdin`, encodes the stream
 * using the VideoCore hardware encoder using H.264 codec and emits the H.264
 * stream to `stdout`.
 *
 *     $ ./rpi-encode-yuv <test.yuv >test.h264
 *
 * `rpi-encode-yuv` uses the `video_encode` component. Uncompressed raw YUV frame
 * data is read from `stdin` and passed to the buffer of input port of
 * `video_encode`. H.264 encoded video is read from the buffer of `video_encode`
 * output port and dumped to `stdout`.
 *
 * Please see README.mdwn for more detailed description of this
 * OpenMAX IL demos for Raspberry Pi bundle.
 *
 */

#include "rpi-i420-framing.hpp"
#include "rpi-video-params.hpp"

// Global variable used by the signal handler and encoding loop
static int want_quit = 0;

// Our application context passed around
// the main routine and callback handlers
typedef struct {
    appctx_sync sync_ ;
    OMX_HANDLETYPE encoder;
    OMX_BUFFERHEADERTYPE *encoder_ppBuffer_in;
    OMX_BUFFERHEADERTYPE *encoder_ppBuffer_out;
    int encoder_input_buffer_needed;
    int encoder_output_buffer_available;
    FILE *fd_in;
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
        case OMX_EventError:
            omx_die(nData1, "error event received");
            break;
        default:
            break;
    }

    return OMX_ErrorNone;
}

// Called by OMX when the encoder component requires
// the input buffer to be filled with YUV video data
static OMX_ERRORTYPE empty_input_buffer_done_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer) {
    appctx *ctx = ((appctx*)pAppData);
    vcos_semaphore_wait(&ctx->sync_.handler_lock);
    // The main loop can now fill the buffer from input file
    ctx->encoder_input_buffer_needed = 1;
    vcos_semaphore_post(&ctx->sync_.handler_lock);
    return OMX_ErrorNone;
}

// Called by OMX when the encoder component has filled
// the output buffer with H.264 encoded video data
static OMX_ERRORTYPE fill_output_buffer_done_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer) {
    appctx *ctx = ((appctx*)pAppData);
    vcos_semaphore_wait(&ctx->sync_.handler_lock);
    // The main loop can now flush the buffer to output file
    ctx->encoder_output_buffer_available = 1;
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
    memset(&ctx, 0, sizeof(callbacks));
    callbacks.EventHandler    = event_handler;
    callbacks.EmptyBufferDone = empty_input_buffer_done_handler;
    callbacks.FillBufferDone  = fill_output_buffer_done_handler;

    init_component_handle("video_encode", &ctx.encoder, &ctx, &callbacks);

    say("Configuring encoder...");

    say("Default port definition for encoder input port 200");
    dump_port(ctx.encoder, 200, OMX_TRUE);
    say("Default port definition for encoder output port 201");
    dump_port(ctx.encoder, 201, OMX_TRUE);

    OMX_PARAM_PORTDEFINITIONTYPE encoder_portdef;
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 200;
    if((r = OMX_GetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder input port 200");
    }
    encoder_portdef.format.video.nFrameWidth  = VIDEO_WIDTH;
    encoder_portdef.format.video.nFrameHeight = VIDEO_HEIGHT;
    encoder_portdef.format.video.xFramerate   = VIDEO_FRAMERATE << 16;
    // Stolen from gstomxvideodec.c of gst-omx
    encoder_portdef.format.video.nStride      = (encoder_portdef.format.video.nFrameWidth + encoder_portdef.nBufferAlignment - 1) & (~(encoder_portdef.nBufferAlignment - 1));
    encoder_portdef.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    if((r = OMX_SetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for encoder input port 200");
    }

    // Copy encoder input port definition as basis encoder output port definition
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 200;
    if((r = OMX_GetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder input port 200");
    }
    encoder_portdef.nPortIndex = 201;
    encoder_portdef.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    encoder_portdef.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    // Which one is effective, this or the configuration just below?
    encoder_portdef.format.video.nBitrate     = VIDEO_BITRATE;
    if((r = OMX_SetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set port definition for encoder output port 201");
    }
    // Configure bitrate
    OMX_VIDEO_PARAM_BITRATETYPE bitrate;
    OMX_INIT_STRUCTURE(bitrate);
    bitrate.eControlRate = OMX_Video_ControlRateVariable;
    bitrate.nTargetBitrate = encoder_portdef.format.video.nBitrate;
    bitrate.nPortIndex = 201;
    if((r = OMX_SetParameter(ctx.encoder, OMX_IndexParamVideoBitrate, &bitrate)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set bitrate for encoder output port 201");
    }
    // Configure format
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_INIT_STRUCTURE(format);
    format.nPortIndex = 201;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    if((r = OMX_SetParameter(ctx.encoder, OMX_IndexParamVideoPortFormat, &format)) != OMX_ErrorNone) {
        omx_die(r, "Failed to set video format for encoder output port 201");
    }

    // Switch components to idle state
    say("Switching state of the encoder component to idle...");
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to idle");
    }
    block_until_state_changed(ctx.encoder, OMX_StateIdle);

    // Enable ports
    say("Enabling ports...");
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandPortEnable, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable encoder input port 200");
    }
    block_until_port_changed(ctx.encoder, 200, OMX_TRUE);
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandPortEnable, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to enable encoder output port 201");
    }
    block_until_port_changed(ctx.encoder, 201, OMX_TRUE);

    // Allocate encoder input and output buffers
    say("Allocating buffers...");
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 200;
    if((r = OMX_GetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder input port 200");
    }
    if((r = OMX_AllocateBuffer(ctx.encoder, &ctx.encoder_ppBuffer_in, 200, NULL, encoder_portdef.nBufferSize)) != OMX_ErrorNone) {
        omx_die(r, "Failed to allocate buffer for encoder input port 200");
    }
    OMX_INIT_STRUCTURE(encoder_portdef);
    encoder_portdef.nPortIndex = 201;
    if((r = OMX_GetParameter(ctx.encoder, OMX_IndexParamPortDefinition, &encoder_portdef)) != OMX_ErrorNone) {
        omx_die(r, "Failed to get port definition for encoder output port 201");
    }
    if((r = OMX_AllocateBuffer(ctx.encoder, &ctx.encoder_ppBuffer_out, 201, NULL, encoder_portdef.nBufferSize)) != OMX_ErrorNone) {
        omx_die(r, "Failed to allocate buffer for encoder output port 201");
    }

    // Just use stdin for input and stdout for output
    say("Opening input and output files...");
    ctx.fd_in = stdin;
    ctx.fd_out = stdout;

    // Switch state of the components prior to starting
    // the video capture and encoding loop
    say("Switching state of the encoder component to executing...");
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandStateSet, OMX_StateExecuting, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to executing");
    }
    block_until_state_changed(ctx.encoder, OMX_StateExecuting);

    say("Configured port definition for encoder input port 200");
    dump_port(ctx.encoder, 200, OMX_FALSE);
    say("Configured port definition for encoder output port 201");
    dump_port(ctx.encoder, 201, OMX_FALSE);

    i420_frame_info frame_info, buf_info;
    get_i420_frame_info(encoder_portdef.format.image.nFrameWidth, encoder_portdef.format.image.nFrameHeight, encoder_portdef.format.image.nStride, encoder_portdef.format.video.nSliceHeight, &frame_info);
    get_i420_frame_info(frame_info.buf_stride, frame_info.buf_slice_height, -1, -1, &buf_info);

    dump_frame_info("Destination frame", &frame_info);
    dump_frame_info("Source buffer", &buf_info);

    if(ctx.encoder_ppBuffer_in->nAllocLen != buf_info.size) {
        die("Allocated encoder input port 200 buffer size %d doesn't equal to the expected buffer size %d", ctx.encoder_ppBuffer_in->nAllocLen, buf_info.size);
    }

    say("Enter encode loop, press Ctrl-C to quit...");

    int input_available = 1, frame_in = 0, frame_out = 0, i;
    size_t input_total_read, want_read, input_read, output_written;
    // I420 spec: U and V plane span size half of the size of the Y plane span size
    int plane_span_y = ROUND_UP_2(frame_info.height), plane_span_uv = plane_span_y / 2;

    ctx.encoder_input_buffer_needed = 1;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    while(1) {
        // empty_input_buffer_done_handler() has marked that there's
        // a need for a buffer to be filled by us
        if(ctx.encoder_input_buffer_needed && input_available) {
            input_total_read = 0;
            memset(ctx.encoder_ppBuffer_in->pBuffer, 0, ctx.encoder_ppBuffer_in->nAllocLen);
            // Pack Y, U, and V plane spans read from input file to the buffer
            for(i = 0; i < 3; i++) {
                want_read = frame_info.p_stride[i] * (i == 0 ? plane_span_y : plane_span_uv);
                input_read = fread(
                    ctx.encoder_ppBuffer_in->pBuffer + buf_info.p_offset[i],
                    1, want_read, ctx.fd_in);
                input_total_read += input_read;
                if(input_read != want_read) {
                    ctx.encoder_ppBuffer_in->nFlags = OMX_BUFFERFLAG_EOS;
                    want_quit = 1;
                    say("Input file EOF");
                    break;
                }
            }
            ctx.encoder_ppBuffer_in->nOffset = 0;
            ctx.encoder_ppBuffer_in->nFilledLen = (buf_info.size - frame_info.size) + input_total_read;
            frame_in++;
            say("Read from input file and wrote to input buffer %d/%d, frame %d", ctx.encoder_ppBuffer_in->nFilledLen, ctx.encoder_ppBuffer_in->nAllocLen, frame_in);
            // Mark input unavailable also if the signal handler was triggered
            if(want_quit) {
                input_available = 0;
            }
            if(input_total_read > 0) {
                ctx.encoder_input_buffer_needed = 0;
                if((r = OMX_EmptyThisBuffer(ctx.encoder, ctx.encoder_ppBuffer_in)) != OMX_ErrorNone) {
                    omx_die(r, "Failed to request emptying of the input buffer on encoder input port 200");
                }
            }
        }
        // fill_output_buffer_done_handler() has marked that there's
        // a buffer for us to flush
        if(ctx.encoder_output_buffer_available) {
            if(ctx.encoder_ppBuffer_out->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) {
                frame_out++;
            }
            // Flush buffer to output file
            output_written = fwrite(ctx.encoder_ppBuffer_out->pBuffer + ctx.encoder_ppBuffer_out->nOffset, 1, ctx.encoder_ppBuffer_out->nFilledLen, ctx.fd_out);
            if(output_written != ctx.encoder_ppBuffer_out->nFilledLen) {
                die("Failed to write to output file: %s", strerror(errno));
            }
            say("Read from output buffer and wrote to output file %d/%d, frame %d", ctx.encoder_ppBuffer_out->nFilledLen, ctx.encoder_ppBuffer_out->nAllocLen, frame_out + 1);
        }
        if(ctx.encoder_output_buffer_available || !frame_out) {
            // Buffer flushed, request a new buffer to be filled by the encoder component
            ctx.encoder_output_buffer_available = 0;
            if((r = OMX_FillThisBuffer(ctx.encoder, ctx.encoder_ppBuffer_out)) != OMX_ErrorNone) {
                omx_die(r, "Failed to request filling of the output buffer on encoder output port 201");
            }
        }
        // Don't exit the loop until all the input frames have been encoded.
        // Out frame count is larger than in frame count because 2 header
        // frames are emitted in the beginning.
        if(want_quit && frame_out == frame_in) {
            break;
        }
        // Would be better to use signaling here but hey this works too
        usleep(10);
    }
    say("Cleaning up...");

    // Restore signal handlers
    signal(SIGINT,  SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    // Flush the buffers on each component
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandFlush, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of encoder input port 200");
    }
    block_until_flushed(&ctx.sync_);
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandFlush, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to flush buffers of encoder output port 201");
    }
    block_until_flushed(&ctx.sync_);

    // Disable all the ports
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandPortDisable, 200, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable encoder input port 200");
    }
    block_until_port_changed(ctx.encoder, 200, OMX_FALSE);
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandPortDisable, 201, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to disable encoder output port 201");
    }
    block_until_port_changed(ctx.encoder, 201, OMX_FALSE);

    // Free all the buffers
    if((r = OMX_FreeBuffer(ctx.encoder, 200, ctx.encoder_ppBuffer_in)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for encoder input port 200");
    }
    if((r = OMX_FreeBuffer(ctx.encoder, 201, ctx.encoder_ppBuffer_out)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free buffer for encoder output port 201");
    }

    // Transition all the components to idle and then to loaded states
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to idle");
    }
    block_until_state_changed(ctx.encoder, OMX_StateIdle);
    if((r = OMX_SendCommand(ctx.encoder, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone) {
        omx_die(r, "Failed to switch state of the encoder component to loaded");
    }
    block_until_state_changed(ctx.encoder, OMX_StateLoaded);

    // Free the component handles
    if((r = OMX_FreeHandle(ctx.encoder)) != OMX_ErrorNone) {
        omx_die(r, "Failed to free encoder component handle");
    }

    // Exit
    fclose(ctx.fd_in);
    fclose(ctx.fd_out);

    vcos_semaphore_delete(&ctx.sync_.handler_lock);
    if((r = OMX_Deinit()) != OMX_ErrorNone) {
        omx_die(r, "OMX de-initalization failed");
    }

    say("Exit!");

    return 0;
}
