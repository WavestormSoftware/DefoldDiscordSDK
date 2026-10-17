// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// See discord_presence.h for the contract.

#include "discord_presence.h"

#include <string.h>
#include <time.h>

#include <string>
#include <utility>

#include "discord_client.h"
#include "discord_events.h"
#include "discordsdk_capi.h"
#include "discord_util.h"

namespace {

const uint64_t kRateWindowSeconds = 20;
const int kRateWindowUpdates = 5;

// Rolling window of dispatch timestamps (unix seconds).
struct PresenceState
{
    const DiscordSocialCapi* m_Capi;
    Discord_Client* m_Client;
    uint64_t m_Recent[kRateWindowUpdates];
    int m_RecentCount;
    bool m_HasPending;
    Discord_Activity m_Pending;

    PresenceState()
        : m_Capi(0)
        , m_Client(0)
        , m_RecentCount(0)
        , m_HasPending(false)
    {
        memset(m_Recent, 0, sizeof(m_Recent));
        memset(&m_Pending, 0, sizeof(m_Pending));
    }
};

PresenceState g_Presence;

bool AllowsDispatchNow()
{
    uint64_t now = (uint64_t)time(NULL);
    int in_window = 0;
    for (int i = 0; i < g_Presence.m_RecentCount; ++i) {
        if (g_Presence.m_Recent[i] + kRateWindowSeconds > now) {
            ++in_window;
        }
    }
    return in_window < kRateWindowUpdates;
}

void RecordDispatch()
{
    uint64_t now = (uint64_t)time(NULL);
    if (g_Presence.m_RecentCount < kRateWindowUpdates) {
        // Insertion keeps the array sorted by dispatch time.
        int pos = g_Presence.m_RecentCount;
        while (pos > 0 && g_Presence.m_Recent[pos - 1] > now) {
            g_Presence.m_Recent[pos] = g_Presence.m_Recent[pos - 1];
            --pos;
        }
        g_Presence.m_Recent[pos] = now;
        ++g_Presence.m_RecentCount;
        return;
    }
    // Overwrite the oldest slot whenever the window is legitimately full.
    int oldest = 0;
    for (int i = 1; i < kRateWindowUpdates; ++i) {
        if (g_Presence.m_Recent[i] < g_Presence.m_Recent[oldest]) {
            oldest = i;
        }
    }
    g_Presence.m_Recent[oldest] = now;
}

// ---------------------------------------------------------------------------
// Lua table reading helpers
// ---------------------------------------------------------------------------

std::string FieldString(lua_State* L, int table, const char* field, bool* present)
{
    *present = false;
    std::string value;
    lua_getfield(L, table, field);
    if (!lua_isnil(L, -1) && !lua_isstring(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "expected string or nil for field '%s'", field);
        return value;
    }
    if (lua_isstring(L, -1)) {
        size_t len = 0;
        const char* data = lua_tolstring(L, -1, &len);
        value.assign(data, len);
        *present = true;
    }
    lua_pop(L, 1);
    return value;
}

double FieldNumber(lua_State* L, int table, const char* field, bool* present)
{
    *present = false;
    double value = 0;
    lua_getfield(L, table, field);
    if (lua_isnumber(L, -1)) {
        value = lua_tonumber(L, -1);
        *present = true;
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "expected number or nil for field '%s'", field);
        return 0;
    }
    lua_pop(L, 1);
    return value;
}

// Enters a sub table when present (leaves it on the stack); returns false and
// leaves the stack balanced otherwise.
bool PushSubTable(lua_State* L, int table, const char* field)
{
    lua_getfield(L, table, field);
    if (lua_istable(L, -1)) {
        return true;
    }
    if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "expected table or nil for field '%s'", field);
        return false;
    }
    lua_pop(L, 1);
    return false;
}

// Reads an optional boolean-style field; tolerated for keys the SDK model of
// this version ignores (for example "instance").
bool FieldBoolForCompatibility(lua_State* L, int table, const char* field, bool* present)
{
    *present = false;
    bool value = false;
    lua_getfield(L, table, field);
    if (!lua_isnil(L, -1)) {
        value = lua_toboolean(L, -1) != 0;
        *present = true;
    }
    lua_pop(L, 1);
    return value;
}

// Reads an optional string field into `buffer`; the caller passes the buffer
// to pointer-based optional setters.
bool OptionalString(lua_State* L, int table, const char* field, std::string& buffer)
{
    bool present = false;
    buffer = FieldString(L, table, field, &present);
    return present;
}

void SetDiscordString(const std::string& value, Discord_String* s)
{
    s->ptr = (uint8_t*)value.data();
    s->size = value.size();
}

// ---------------------------------------------------------------------------
// Activity building
// ---------------------------------------------------------------------------

bool BuildActivity(lua_State* L, int table, Discord_Activity* activity)
{
    const DiscordSocialCapi& capi = *g_Presence.m_Capi;
    memset(activity, 0, sizeof(Discord_Activity));
    capi.Discord_Activity_Init(activity);

    int top = lua_gettop(L);

    // name
    {
        bool present = false;
        std::string name = FieldString(L, table, "name", &present);
        if (present) {
            Discord_String s;
            SetDiscordString(name, &s);
            capi.Discord_Activity_SetName(activity, s);
        }
    }

    // type
    {
        bool present = false;
        double raw = FieldNumber(L, table, "type", &present);
        if (present) {
            int type = (int)raw;
            if (type < 0 || type > 6) {
                luaL_error(L, "activity type %d out of range (0..6)", type);
            }
            capi.Discord_Activity_SetType(activity, (Discord_ActivityTypes)type);
        }
    }

    // status_display_type
    {
        bool present = false;
        double raw = FieldNumber(L, table, "status_display_type", &present);
        if (present) {
            int display = (int)raw;
            if (display < 0 || display > 2) {
                luaL_error(L, "status_display_type %d out of range (0..2)", display);
            }
            Discord_StatusDisplayTypes display_value = (Discord_StatusDisplayTypes)display;
            capi.Discord_Activity_SetStatusDisplayType(activity, &display_value);
        }
    }

    // state / state_url / details / details_url
    {
        std::string buffer;
        Discord_String s;
        if (OptionalString(L, table, "state", buffer)) {
            SetDiscordString(buffer, &s);
            capi.Discord_Activity_SetState(activity, &s);
        }
        if (OptionalString(L, table, "state_url", buffer)) {
            SetDiscordString(buffer, &s);
            capi.Discord_Activity_SetStateUrl(activity, &s);
        }
        if (OptionalString(L, table, "details", buffer)) {
            SetDiscordString(buffer, &s);
            capi.Discord_Activity_SetDetails(activity, &s);
        }
        if (OptionalString(L, table, "details_url", buffer)) {
            SetDiscordString(buffer, &s);
            capi.Discord_Activity_SetDetailsUrl(activity, &s);
        }
    }

    // supported_platforms
    {
        bool present = false;
        double raw = FieldNumber(L, table, "supported_platforms", &present);
        if (present) {
            uint64_t mask = (uint64_t)raw;
            if (mask == 0 || mask > 0xFFull) {
                luaL_error(L, "supported_platforms must be a non-zero mask in [1, 255]");
            }
            // The Lua surface carries the wiki PLATFORM_* mask values
            // (DESKTOP 1, WINDOWS 2, MACOS 4, LINUX 8, ANDROID 16, IOS 32).
            // The SDK enum only speaks Desktop/Xbox/Samsung/IOS/Android/...
            // (see cdiscourd.h), so the desktop granularity collapses onto
            // the SDK Desktop bit and the mobile flags route to their SDK
            // twins. Wiki values stay stable; join-button routing stays
            // correct.
            int sdk_platforms = 0;
            if (mask & 0x0Full) {
                sdk_platforms |= (int)Discord_ActivityGamePlatforms_Desktop;
            }
            if (mask & 0x10ull) {
                sdk_platforms |= (int)Discord_ActivityGamePlatforms_Android;
            }
            if (mask & 0x20ull) {
                sdk_platforms |= (int)Discord_ActivityGamePlatforms_IOS;
            }
            capi.Discord_Activity_SetSupportedPlatforms(activity, (Discord_ActivityGamePlatforms)sdk_platforms);
        }
    }

    // timestamps
    if (PushSubTable(L, table, "timestamps")) {
        bool start_present = false;
        bool end_present = false;
        double start_value = FieldNumber(L, -1, "start_ts", &start_present);
        double end_value = FieldNumber(L, -1, "end_ts", &end_present);
        if (start_present || end_present) {
            Discord_ActivityTimestamps timestamps;
            memset(&timestamps, 0, sizeof(timestamps));
            capi.Discord_ActivityTimestamps_Init(&timestamps);
            if (start_present && start_value > 0) {
                capi.Discord_ActivityTimestamps_SetStart(&timestamps, (uint64_t)start_value);
            }
            if (end_present && end_value > 0) {
                capi.Discord_ActivityTimestamps_SetEnd(&timestamps, (uint64_t)end_value);
            }
            capi.Discord_Activity_SetTimestamps(activity, &timestamps);
            capi.Discord_ActivityTimestamps_Drop(&timestamps);
        }
        lua_pop(L, 1);
    }

    // assets (large/small images and texts, urls, invite cover image). The
    // top-level invite_cover_image key also feeds the same asset field.
    {
        std::string buffer;
        bool have_cover = false;
        std::string cover;
        Discord_ActivityAssets assets;
        memset(&assets, 0, sizeof(assets));
        capi.Discord_ActivityAssets_Init(&assets);
        Discord_String s;

        bool have_any = false;
        if (PushSubTable(L, table, "assets")) {
            if (OptionalString(L, -1, "large_image", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetLargeImage(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "large_text", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetLargeText(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "large_url", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetLargeUrl(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "small_image", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetSmallImage(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "small_text", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetSmallText(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "small_url", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetSmallUrl(&assets, &s);
                have_any = true;
            }
            if (OptionalString(L, -1, "invite_cover_image", buffer)) {
                SetDiscordString(buffer, &s);
                capi.Discord_ActivityAssets_SetInviteCoverImage(&assets, &s);
                have_any = true;
            }
            lua_pop(L, 1);
        }
        cover = FieldString(L, table, "invite_cover_image", &have_cover);
        if (have_cover && !have_any) {
            SetDiscordString(cover, &s);
            capi.Discord_ActivityAssets_SetInviteCoverImage(&assets, &s);
            have_any = true;
        }
        if (have_any) {
            capi.Discord_Activity_SetAssets(activity, &assets);
        }
        capi.Discord_ActivityAssets_Drop(&assets);
    }

    // party
    if (PushSubTable(L, table, "party")) {
        Discord_ActivityParty party;
        memset(&party, 0, sizeof(party));
        capi.Discord_ActivityParty_Init(&party);
        bool present = false;
        std::string party_id = FieldString(L, -1, "id", &present);
        if (present) {
            Discord_String s;
            SetDiscordString(party_id, &s);
            capi.Discord_ActivityParty_SetId(&party, s);
        }
        double size_value = FieldNumber(L, -1, "size", &present);
        if (present) {
            capi.Discord_ActivityParty_SetCurrentSize(&party, (int32_t)size_value);
        }
        double max_value = FieldNumber(L, -1, "max", &present);
        if (present) {
            capi.Discord_ActivityParty_SetMaxSize(&party, (int32_t)max_value);
        }
        double privacy_value = FieldNumber(L, -1, "privacy", &present);
        if (present) {
            int privacy = (int)privacy_value;
            if (privacy < 0 || privacy > 1) {
                dmLogWarning("DefoldDiscordSDK: party privacy %d out of range (0..1), ignored", privacy);
            } else {
                capi.Discord_ActivityParty_SetPrivacy(&party, (Discord_ActivityPartyPrivacy)privacy);
            }
        }
        capi.Discord_Activity_SetParty(activity, &party);
        capi.Discord_ActivityParty_Drop(&party);
        lua_pop(L, 1);
    }

    // secrets: this SDK version only exposes the join secret
    if (PushSubTable(L, table, "secrets")) {
        bool present = false;
        std::string join = FieldString(L, -1, "join", &present);
        if (present) {
            Discord_ActivitySecrets secrets;
            memset(&secrets, 0, sizeof(secrets));
            capi.Discord_ActivitySecrets_Init(&secrets);
            Discord_String s;
            SetDiscordString(join, &s);
            capi.Discord_ActivitySecrets_SetJoin(&secrets, s);
            capi.Discord_Activity_SetSecrets(activity, &secrets);
            capi.Discord_ActivitySecrets_Drop(&secrets);
        }
        lua_pop(L, 1);
    }

    // buttons: array of {label, url}, maximum 2
    {
        lua_getfield(L, table, "buttons");
        if (!lua_isnil(L, -1)) {
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                luaL_error(L, "expected table or nil for field 'buttons'");
            } else {
                int count = 0;
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 2);
                        luaL_error(L, "buttons entries must be tables with 'label' and 'url'");
                    }
                    ++count;
                    if (count > 2) {
                        dmLogWarning("DefoldDiscordSDK: activities accept at most 2 buttons, extra entries ignored");
                        lua_pop(L, 1);
                        continue;
                    }
                    bool present = false;
                    std::string label = FieldString(L, -1, "label", &present);
                    std::string url = FieldString(L, -1, "url", &present);
                    Discord_ActivityButton button;
                    memset(&button, 0, sizeof(button));
                    capi.Discord_ActivityButton_Init(&button);
                    {
                        Discord_String s;
                        SetDiscordString(label, &s);
                        capi.Discord_ActivityButton_SetLabel(&button, s);
                    }
                    {
                        Discord_String s;
                        SetDiscordString(url, &s);
                        capi.Discord_ActivityButton_SetUrl(&button, s);
                    }
                    capi.Discord_Activity_AddButton(activity, &button);
                    capi.Discord_ActivityButton_Drop(&button);
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 1);
    }

    // instance: not part of this SDK's activity model; accepted and ignored
    // so the documented API shape keeps working.
    {
        bool unused_present = false;
        (void)FieldBoolForCompatibility(L, table, "instance", &unused_present);
    }

    if (top != lua_gettop(L)) {
        dmLogError("DefoldDiscordSDK: internal Lua stack imbalance while building an activity");
        return false;
    }
    return true;
}

// Dispatches a fully built activity. Takes ownership: always calls Drop on the
// struct payload after the SDK copies it synchronously into its own storage
// (mirroring the official wrapper, which lets its Activity object die right
// after the call returns).
void Dispatch(Discord_Activity* activity)
{
    if (!g_Presence.m_Capi || !g_Presence.m_Client) {
        return;
    }
    const DiscordSocialCapi& capi = *g_Presence.m_Capi;

    struct RichPresenceCallContext
    {
        int m_Kind;
    };

    RichPresenceCallContext* context = new RichPresenceCallContext();
    context->m_Kind = 0;
    capi.Discord_Client_UpdateRichPresence(g_Presence.m_Client, activity,
                                           &DiscordSocialClient_OnRichPresenceResult,
                                           &DiscordSocialClient_FreeCallbackData, context);

    capi.Discord_Activity_Drop(activity);
    memset(activity, 0, sizeof(Discord_Activity));
    RecordDispatch();
}

} // unnamed namespace

namespace discordsdk_presence {

void SetTarget(const DiscordSocialCapi* capi, Discord_Client* client)
{
    if (g_Presence.m_HasPending) {
        // Swap out the pending activity instance before losing the binding.
        Discord_Activity pending = g_Presence.m_Pending;
        g_Presence.m_HasPending = false;
        memset(&g_Presence.m_Pending, 0, sizeof(g_Presence.m_Pending));
        if (g_Presence.m_Capi) {
            g_Presence.m_Capi->Discord_Activity_Drop(&pending);
            memset(&pending, 0, sizeof(pending));
        }
    }
    g_Presence.m_Capi = capi;
    g_Presence.m_Client = client;
}

bool Update(lua_State* L, int index)
{
    if (!g_Presence.m_Capi || !g_Presence.m_Client) {
        return false;
    }
    if (!lua_istable(L, index)) {
        luaL_error(L, "update_presence expects a table describing the activity");
    }

    if (g_Presence.m_HasPending) {
        // A pending update is superseded by the new one.
        Discord_Activity pending = g_Presence.m_Pending;
        g_Presence.m_HasPending = false;
        memset(&g_Presence.m_Pending, 0, sizeof(g_Presence.m_Pending));
        g_Presence.m_Capi->Discord_Activity_Drop(&pending);
        memset(&pending, 0, sizeof(pending));
    }

    Discord_Activity built;
    memset(&built, 0, sizeof(built));
    if (!BuildActivity(L, index, &built)) {
        return false;
    }

    if (AllowsDispatchNow()) {
        Dispatch(&built);
        return true;
    }

    // Keep the built instance pending for the next Pump sweep.
    g_Presence.m_Pending = built;
    g_Presence.m_HasPending = true;
    return true;
}

void Clear()
{
    if (g_Presence.m_HasPending) {
        Discord_Activity pending = g_Presence.m_Pending;
        g_Presence.m_HasPending = false;
        memset(&g_Presence.m_Pending, 0, sizeof(g_Presence.m_Pending));
        if (g_Presence.m_Capi) {
            g_Presence.m_Capi->Discord_Activity_Drop(&pending);
        }
        memset(&pending, 0, sizeof(pending));
    }
    if (g_Presence.m_Capi && g_Presence.m_Client) {
        // Clearing is deliberately not rate limited.
        g_Presence.m_Capi->Discord_Client_ClearRichPresence(g_Presence.m_Client);
    }
}

void Pump()
{
    if (!g_Presence.m_HasPending || !g_Presence.m_Capi || !g_Presence.m_Client) {
        return;
    }
    if (!AllowsDispatchNow()) {
        return;
    }
    Discord_Activity pending = g_Presence.m_Pending;
    g_Presence.m_HasPending = false;
    memset(&g_Presence.m_Pending, 0, sizeof(g_Presence.m_Pending));
    Dispatch(&pending);
}

} // namespace discordsdk_presence
