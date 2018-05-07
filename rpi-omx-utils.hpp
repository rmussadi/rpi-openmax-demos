#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <bcm_host.h>

#include <interface/vcos/vcos_semaphore.h>
#include <interface/vmcs_host/vchost.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

// Dunno where this is originally stolen from...
#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP

extern void say(const char* message, ...);
extern void die(const char* message, ...);
extern void omx_die(OMX_ERRORTYPE error, const char* message, ...);
extern void dump_event(OMX_HANDLETYPE hComponent, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
extern const char* dump_compression_format(OMX_VIDEO_CODINGTYPE c);
extern const char* dump_color_format(OMX_COLOR_FORMATTYPE c);
extern void dump_portdef(OMX_PARAM_PORTDEFINITIONTYPE* portdef);
extern void dump_port(OMX_HANDLETYPE hComponent, OMX_U32 nPortIndex, OMX_BOOL dumpformats);
extern void init_component_handle(const char *name, OMX_HANDLETYPE* hComponent, OMX_PTR pAppData, OMX_CALLBACKTYPE* callbacks);

// busy loops to verify we're running in order
extern void block_until_state_changed(OMX_HANDLETYPE hComponent, OMX_STATETYPE wanted_eState);
extern void block_until_port_changed(OMX_HANDLETYPE hComponent, OMX_U32 nPortIndex, OMX_BOOL bEnabled);

// Our appl context passed around main routine & callback handlers
typedef struct
{
    int flushed;
    VCOS_SEMAPHORE_T handler_lock;
} appctx_sync;

extern void block_until_flushed(appctx_sync *ctx);
