#pragma once

#include "remixapi/bridge_remix_api.h"
#include "remix_logger.h"

// Minimal RTX Remix API wrapper.
// bridge_initRemixApi() is all that's needed — the game process already has
// d3d9_remix.dll (the Remix bridge) loaded before this code runs.
// Startup / Present / Shutdown are NOT exported in x86 bridge mode; do not use them.

namespace remix_api
{
    inline remixapi_Interface g_api         = {};
    inline bool               g_initialized = false;

    inline bool init()
    {
        if (g_initialized) return true;

        RemixLog("remix_api::init() calling bridge_initRemixApi...");
        const remixapi_ErrorCode r = remixapi::bridge_initRemixApi(&g_api);
        RemixLog("  result = %d", static_cast<int>(r));
        RemixLog("  CreateLight       = %p", g_api.CreateLight);
        RemixLog("  DestroyLight      = %p", g_api.DestroyLight);
        RemixLog("  DrawLightInstance = %p", g_api.DrawLightInstance);

        g_initialized = (r == REMIXAPI_ERROR_CODE_SUCCESS)
                        && g_api.CreateLight
                        && g_api.DestroyLight
                        && g_api.DrawLightInstance;

        RemixLog("  g_initialized = %s", g_initialized ? "true" : "false");
        return g_initialized;
    }
}