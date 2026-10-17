// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Implementation of the typed function pointer table. The static_assert block
// verifies every entry below against the exact declaration in the vendored
// header include/cdiscourd.h. If the vendored header is ever refreshed with a
// new SDK build and a signature changes, compilation fails here first instead
// of crashing at runtime.

#include "discordsdk_capi.h"

#include <string.h>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

bool DiscordSocialCapi_Resolve(DiscordSocialCapi* table, void* module,
                               void* (*get_symbol)(void* module, const char* name),
                               void (*log_missing)(const char* name, const char* reason))
{
    if (!table || !module || !get_symbol) {
        return false;
    }

    bool all_found = true;

#define X(ret_type, fn_name, fn_params)                                                                     \
    {                                                                                                       \
        void* sym = get_symbol(module, #fn_name);                                                           \
        if (sym) {                                                                                          \
            table->fn_name = reinterpret_cast<ret_type (*) fn_params>(sym);                                  \
        } else {                                                                                            \
            if (log_missing) {                                                                              \
                log_missing(#fn_name, "symbol not found in discord partner sdk library");                    \
            }                                                                                               \
            all_found = false;                                                                              \
        }                                                                                                   \
    }
    DISCORD_SOCIAL_CAPI_FUNCTIONS(X)
#undef X

    if (!all_found) {
        // Never leave a half-bound table behind; callers must not probe it.
        memset(table, 0, sizeof(*table));
        return false;
    }
    table->m_Loaded = true;
    return true;
}

std::string DiscordSocialCapi_ReadString(const DiscordSocialCapi& capi, const Discord_String& s)
{
    std::string out;
    if (s.ptr && s.size > 0) {
        out.assign(reinterpret_cast<const char*>(s.ptr), s.size);
    }
    if (s.ptr) {
        capi.Discord_Free(s.ptr);
    }
    return out;
}

std::string DiscordSocialCapi_ReadGetter(const DiscordSocialCapi& capi, Discord_String* s)
{
    // Read-sensitive: the getter call sites hand over their own instance that
    // was just filled; ownership of s->ptr transfers to this helper.
    if (!s) {
        return std::string();
    }
    return DiscordSocialCapi_ReadString(capi, *s);
}

void DiscordSocialCapi_ReadProperties(const DiscordSocialCapi& capi, const Discord_Properties& props,
                                      std::map<std::string, std::string>* out)
{
    if (!out) {
        return;
    }
    out->clear();
    for (size_t i = 0; i < props.size; ++i) {
        const Discord_String& key = props.keys[i];
        const Discord_String& value = props.values[i];
        std::string k;
        std::string v;
        if (key.ptr && key.size > 0) {
            k.assign(reinterpret_cast<const char*>(key.ptr), key.size);
        }
        if (value.ptr && value.size > 0) {
            v.assign(reinterpret_cast<const char*>(value.ptr), value.size);
        }
        (*out)[k] = v;
        if (key.ptr) {
            capi.Discord_Free(key.ptr);
        }
        if (value.ptr) {
            capi.Discord_Free(value.ptr);
        }
    }
    if (props.keys) {
        capi.Discord_Free(props.keys);
    }
    if (props.values) {
        capi.Discord_Free(props.values);
    }
}

Discord_Properties DiscordSocialCapi_MakeProperties(const DiscordSocialCapi& capi,
                                                    const std::map<std::string, std::string>& map)
{
    // Mirrors the official wrapper (ConvertedProperties): keys and values are
    // allocated with the SDK allocator so Discord_FreeProperties can release
    // the whole payload regardless of which side frees it.
    Discord_Properties props;
    memset(&props, 0, sizeof(props));
    props.size = map.size();
    if (props.size == 0) {
        return props;
    }
    props.keys = reinterpret_cast<Discord_String*>(capi.Discord_Alloc(props.size * sizeof(Discord_String)));
    props.values = reinterpret_cast<Discord_String*>(capi.Discord_Alloc(props.size * sizeof(Discord_String)));
    if (!props.keys || !props.values) {
        if (props.keys) {
            capi.Discord_Free(props.keys);
        }
        if (props.values) {
            capi.Discord_Free(props.values);
        }
        memset(&props, 0, sizeof(props));
        return props;
    }
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = map.begin(); it != map.end(); ++it, ++i) {
        Discord_String k;
        k.ptr = reinterpret_cast<uint8_t*>(capi.Discord_Alloc(it->first.size()));
        k.size = it->first.size();
        if (k.ptr && k.size > 0) {
            memcpy(k.ptr, it->first.data(), k.size);
        }
        Discord_String v;
        v.ptr = reinterpret_cast<uint8_t*>(capi.Discord_Alloc(it->second.size()));
        v.size = it->second.size();
        if (v.ptr && v.size > 0) {
            memcpy(v.ptr, it->second.data(), v.size);
        }
        props.keys[i] = k;
        props.values[i] = v;
    }
    return props;
}

// ---------------------------------------------------------------------------
// Compile-time signature verification.
// Every bound function pointer must be exactly the type declared by the
// vendored header. decltype on the function address is an unevaluated context,
// so no SDK symbols end up referenced at link time.
// ---------------------------------------------------------------------------
#define DISCORD_SOCIAL_CAPI_CHECK(fn_name)                                                                              \
    static_assert(std::is_same<decltype(&fn_name), decltype(std::declval<DiscordSocialCapi&>().fn_name)>::value,        \
                  "DiscordSocialCapi signature drift for " #fn_name);

struct DiscordSocialCapiChecker
{
#define X(ret_type, fn_name, fn_params) DISCORD_SOCIAL_CAPI_CHECK(fn_name)
    DISCORD_SOCIAL_CAPI_FUNCTIONS(X)
#undef X
};

// Unused-reporting guard not needed beyond the struct above; it exists only
// to give the member static_asserts a lexical home. No code instantiates it.
