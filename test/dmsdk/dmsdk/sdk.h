// Minimal stand-in for Defold dmsdk/sdk.h used ONLY for local syntax checking
// of the DefoldDiscordSDK extension. Not part of the extension itself.
#ifndef __FAKE_DMSDK_SDK_H__
#define __FAKE_DMSDK_SDK_H__
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ---- platform macros to emulate a linux build for the syntax check ----
#ifndef DM_PLATFORM_LINUX
#define DM_PLATFORM_LINUX 1
#endif

// ---- lua ----
extern "C" {
typedef struct lua_State lua_State;
typedef struct luaL_Buffer luaL_Buffer;
typedef int (*lua_CFunction)(lua_State* L);
struct luaL_reg { const char* name; lua_CFunction func; };

#define LUA_NOREF (-2)
#define LUA_REFNIL (-1)
#define LUA_REGISTRYINDEX (-10000)
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER 3
#define LUA_TSTRING 4
#define LUA_TTABLE 5
#define LUA_TFUNCTION 6
#define LUA_TUSERDATA 7
#define LUA_TTHREAD 8

int lua_gettop(lua_State* L);
void lua_pop(lua_State* L, int n);
void lua_pushvalue(lua_State* L, int idx);
int lua_type(lua_State* L, int idx);
const char* lua_typename(lua_State* L, int t);
void lua_pushnil(lua_State* L);
void lua_pushboolean(lua_State* L, int b);
void lua_pushnumber(lua_State* L, double n);
void lua_pushinteger(lua_State* L, long n);
void lua_pushlightuserdata(lua_State* L, void* p);
const char* lua_pushstring(lua_State* L, const char* s);
const char* lua_pushlstring(lua_State* L, const char* s, size_t len);
void lua_pushcclosure(lua_State* L, lua_CFunction fn, int n);
int lua_isnumber(lua_State* L, int idx);
int lua_isstring(lua_State* L, int idx);
int lua_istable(lua_State* L, int idx);
int lua_isnil(lua_State* L, int idx);
int lua_isfunction(lua_State* L, int idx);
int lua_toboolean(lua_State* L, int idx);
int lua_isboolean(lua_State* L, int idx);
double lua_tonumber(lua_State* L, int idx);
const char* lua_tostring(lua_State* L, int idx);
size_t lua_strlen(lua_State* L, int idx);
void lua_getfield(lua_State* L, int idx, const char* k);
void lua_setfield(lua_State* L, int idx, const char* k);
void lua_settable(lua_State* L, int idx);
void lua_rawgeti(lua_State* L, int idx, int n);
void lua_rawseti(lua_State* L, int idx, int n);
void lua_newtable(lua_State* L);
void lua_createtable(lua_State* L, int narr, int nrec);
void lua_insert(lua_State* L, int idx);
void lua_remove(lua_State* L, int idx);
int lua_next(lua_State* L, int idx);
void* lua_touserdata(lua_State* L, int idx);
int lua_pcall(lua_State* L, int nargs, int nresults, int errfunc);
const char* lua_tolstring(lua_State* L, int idx, size_t* len);
typedef double lua_Number;
typedef long lua_Integer;
 lua_State* lua_newstate(int (*f)(void* ud), void* ud);
void lua_pushcfunction(lua_State* L, lua_CFunction f);

int luaL_error(lua_State* L, const char* fmt, ...);
const char* luaL_checkstring(lua_State* L, int n);
const char* luaL_optstring(lua_State* L, int n, const char* d);
double luaL_checknumber(lua_State* L, int n);
long luaL_checkinteger(lua_State* L, int n);
int luaL_checktype(lua_State* L, int n, int t);
int luaL_newmetatable(lua_State* L, const char* tname);
void luaL_getmetatable(lua_State* L, const char* tname);
int luaL_ref(lua_State* L, int t);
void luaL_unref(lua_State* L, int t, int ref);
int luaL_register(lua_State* L, const char* libname, const struct luaL_reg* l);
} // extern "C"

#ifdef __cplusplus
extern "C" {
#endif
void dmLogError(const char* format, ...);
void dmLogWarning(const char* format, ...);
void dmLogInfo(const char* format, ...);
void dmLogDebug(const char* format, ...);
#ifdef __cplusplus
}
#endif

// ---- dmScript ----
namespace dmScript {
lua_State* GetMainThread(lua_State* L);
int Ref(lua_State* L, int table_index);
void Unref(lua_State* L, int table_index, int ref);
void GetInstance(lua_State* L);
void SetInstance(lua_State* L);
}

// ---- dmConfigFile ----
namespace dmConfigFile {
typedef struct ConfigFile* HConfig;
typedef char* GetStringResult; // unused
const char* GetString(HConfig config, const char* key, const char* default_value);
int32_t GetInt(HConfig config, const char* key, int32_t default_value);
}

// ---- dmExtension ----
namespace dmExtension {
enum Result { RESULT_OK = 0, RESULT_INIT_ERROR = 1, RESULT_SCRIPT_ERROR = 2 };
struct AppParams { dmConfigFile::HConfig m_ConfigFile; };
struct Params { lua_State* m_L; dmConfigFile::HConfig m_ConfigFile; };
struct Event { int m_Event; };
enum EventIds { EVENT_ID_ACTIVATEAPP = 0, EVENT_ID_DEACTIVATEAPP = 1, EVENT_ID_ICONIFYAPP = 2, EVENT_ID_DEICONIFYAPP = 3 };
}

#define DM_LUA_STACK_CHECK(L, n) assert((n) < 0 ? (lua_gettop(L) > -(n)) : (lua_gettop(L) == 0 || lua_gettop(L) >= 0))

#define DM_DECLARE_EXTENSION(Sym, Lib, AppInit, AppFinal, Init, Update, OnEvent, Final) \
    int Sym_SymbolTable = 0;

#endif // __FAKE_DMSDK_SDK_H__
