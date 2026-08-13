#include <errno.h>
#include <iconv.h>
#include <stddef.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM != 501
#error "liconv first version requires the Lua 5.1 API (including LuaJIT)"
#endif

#define LICONV_METATABLE "liconv.converter"
#define LICONV_BUFFER_SIZE 4096

typedef struct {
    iconv_t cd;
    int closed;
} liconv_converter;

static const char *error_code(int error_number)
{
    switch (error_number) {
    case EILSEQ:
        return "EILSEQ";
    case EINVAL:
        return "EINVAL";
    case E2BIG:
        return "E2BIG";
    case ENOMEM:
        return "ENOMEM";
#ifdef EACCES
    case EACCES:
        return "EACCES";
#endif
#ifdef EBUSY
    case EBUSY:
        return "EBUSY";
#endif
#ifdef EBADF
    case EBADF:
        return "EBADF";
#endif
#ifdef EEXIST
    case EEXIST:
        return "EEXIST";
#endif
#ifdef EFAULT
    case EFAULT:
        return "EFAULT";
#endif
#ifdef EINTR
    case EINTR:
        return "EINTR";
#endif
#ifdef EMFILE
    case EMFILE:
        return "EMFILE";
#endif
#ifdef EIO
    case EIO:
        return "EIO";
#endif
#ifdef ENFILE
    case ENFILE:
        return "ENFILE";
#endif
#ifdef ENOENT
    case ENOENT:
        return "ENOENT";
#endif
#ifdef ENODEV
    case ENODEV:
        return "ENODEV";
#endif
#ifdef ENOTDIR
    case ENOTDIR:
        return "ENOTDIR";
#endif
#ifdef ENOSPC
    case ENOSPC:
        return "ENOSPC";
#endif
#ifdef EOVERFLOW
    case EOVERFLOW:
        return "EOVERFLOW";
#endif
#ifdef EPERM
    case EPERM:
        return "EPERM";
#endif
#ifdef EROFS
    case EROFS:
        return "EROFS";
#endif
#ifdef ENOTSUP
    case ENOTSUP:
        return "ENOTSUP";
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP:
        return "EOPNOTSUPP";
#endif
#ifdef ECANCELED
    case ECANCELED:
        return "ECANCELED";
#endif
    default:
        return "EUNKNOWN";
    }
}

static int push_error(lua_State *L, int error_number, const char *operation,
                      const char *message)
{
    lua_newtable(L);

    lua_pushstring(L, error_code(error_number));
    lua_setfield(L, -2, "code");

    lua_pushstring(L, message != NULL ? message : strerror(error_number));
    lua_setfield(L, -2, "message");

    if (error_number != 0) {
        lua_pushinteger(L, (lua_Integer)error_number);
        lua_setfield(L, -2, "errno");
    }

    lua_pushstring(L, operation);
    lua_setfield(L, -2, "operation");

    return 1;
}

static int return_error(lua_State *L, int error_number, const char *operation)
{
    lua_pushnil(L);
    push_error(L, error_number, operation, NULL);
    return 2;
}

static int return_closed_error(lua_State *L, const char *operation)
{
    lua_pushnil(L);
    push_error(L, ECANCELED, operation, "conversion descriptor is closed");
    return 2;
}

static liconv_converter *check_converter(lua_State *L, int index)
{
    return (liconv_converter *)luaL_checkudata(L, index, LICONV_METATABLE);
}

static int contains_nul(const char *value, size_t length)
{
    return memchr(value, '\0', length) != NULL;
}

/* Discard any shift sequence while putting the descriptor in its initial state. */
static int reset_descriptor(iconv_t cd)
{
    char output[LICONV_BUFFER_SIZE];

    for (;;) {
        char *output_pointer = output;
        size_t output_left = sizeof(output);
        size_t result = iconv(cd, NULL, NULL, &output_pointer, &output_left);

        if (result != (size_t)-1) {
            return 0;
        }
        if (errno != E2BIG) {
            return errno;
        }
    }
}

static int add_reset_output(iconv_t cd, luaL_Buffer *buffer)
{
    for (;;) {
        char *output_pointer = luaL_prepbuffer(buffer);
        size_t output_left = LUAL_BUFFERSIZE;
        size_t result = iconv(cd, NULL, NULL, &output_pointer, &output_left);
        size_t produced = LUAL_BUFFERSIZE - output_left;

        luaL_addsize(buffer, produced);

        if (result != (size_t)-1) {
            return 0;
        }
        if (errno != E2BIG) {
            return errno;
        }
    }
}

static int convert_input(liconv_converter *converter,
                         const char *input, size_t input_length,
                         luaL_Buffer *buffer)
{
    char *input_pointer = (char *)input;
    size_t input_left = input_length;

    for (;;) {
        char *output_pointer = luaL_prepbuffer(buffer);
        size_t output_left = LUAL_BUFFERSIZE;
        size_t result = iconv(converter->cd, &input_pointer, &input_left,
                              &output_pointer, &output_left);
        size_t produced = LUAL_BUFFERSIZE - output_left;

        luaL_addsize(buffer, produced);

        if (result != (size_t)-1) {
            if (input_left == 0) {
                return 0;
            }
            continue;
        }

        if (errno == E2BIG) {
            continue;
        }

        return errno;
    }
}

static int liconv_open(lua_State *L)
{
    size_t to_length;
    size_t from_length;
    const char *to = luaL_checklstring(L, 1, &to_length);
    const char *from = luaL_checklstring(L, 2, &from_length);
    iconv_t cd;
    liconv_converter *converter;
    int saved_errno;

    if (contains_nul(to, to_length) || contains_nul(from, from_length)) {
        return return_error(L, EINVAL, "open");
    }

    converter = (liconv_converter *)lua_newuserdata(L, sizeof(*converter));
    converter->cd = (iconv_t)-1;
    converter->closed = 1;

    cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) {
        saved_errno = errno;
        lua_pop(L, 1);
        return return_error(L, saved_errno, "open");
    }

    converter->cd = cd;
    converter->closed = 0;

    luaL_getmetatable(L, LICONV_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int convert_value(lua_State *L, liconv_converter *converter,
                         const char *input, size_t input_length)
{
    luaL_Buffer buffer;
    int error_number;

    error_number = reset_descriptor(converter->cd);
    if (error_number != 0) {
        lua_pushnil(L);
        push_error(L, error_number, "convert", NULL);
        return error_number;
    }

    luaL_buffinit(L, &buffer);
    error_number = convert_input(converter, input, input_length, &buffer);
    if (error_number != 0) {
        lua_pushnil(L);
        push_error(L, error_number, "convert", NULL);
        return error_number;
    }

    error_number = add_reset_output(converter->cd, &buffer);
    if (error_number != 0) {
        lua_pushnil(L);
        push_error(L, error_number, "convert", NULL);
        return error_number;
    }

    luaL_pushresult(&buffer);
    return 0;
}

static int liconv_convert(lua_State *L)
{
    liconv_converter *converter = check_converter(L, 1);
    size_t input_length;
    const char *input;

    if (converter->closed) {
        return return_closed_error(L, "convert");
    }

    input = luaL_checklstring(L, 2, &input_length);
    if (convert_value(L, converter, input, input_length) != 0) {
        return 2;
    }
    return 1;
}

static int liconv_reset(lua_State *L)
{
    liconv_converter *converter = check_converter(L, 1);
    int error_number;

    if (converter->closed) {
        return return_closed_error(L, "reset");
    }

    error_number = reset_descriptor(converter->cd);
    if (error_number != 0) {
        return return_error(L, error_number, "reset");
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int liconv_close(lua_State *L)
{
    liconv_converter *converter = check_converter(L, 1);
    int result;
    int saved_errno = 0;

    if (converter->closed) {
        lua_pushboolean(L, 1);
        return 1;
    }

    result = iconv_close(converter->cd);
    if (result == -1) {
        saved_errno = errno;
    }
    converter->cd = (iconv_t)-1;
    converter->closed = 1;

    if (saved_errno != 0) {
        return return_error(L, saved_errno, "close");
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int liconv_gc(lua_State *L)
{
    liconv_converter *converter = check_converter(L, 1);

    if (!converter->closed) {
        (void)iconv_close(converter->cd);
        converter->cd = (iconv_t)-1;
        converter->closed = 1;
    }
    return 0;
}

static int liconv_is_closed(lua_State *L)
{
    liconv_converter *converter = check_converter(L, 1);

    lua_pushboolean(L, converter->closed);
    return 1;
}

static int liconv_convert_function(lua_State *L)
{
    size_t to_length;
    size_t from_length;
    size_t input_length;
    const char *to = luaL_checklstring(L, 1, &to_length);
    const char *from = luaL_checklstring(L, 2, &from_length);
    const char *input = luaL_checklstring(L, 3, &input_length);
    iconv_t cd;
    liconv_converter *converter;
    int error_number;

    if (contains_nul(to, to_length) || contains_nul(from, from_length)) {
        return return_error(L, EINVAL, "open");
    }

    converter = (liconv_converter *)lua_newuserdata(L, sizeof(*converter));
    converter->cd = (iconv_t)-1;
    converter->closed = 1;

    cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) {
        int saved_errno = errno;
        lua_pop(L, 1);
        return return_error(L, saved_errno, "open");
    }

    converter->cd = cd;
    converter->closed = 0;
    luaL_getmetatable(L, LICONV_METATABLE);
    lua_setmetatable(L, -2);

    error_number = convert_value(L, converter, input, input_length);
    converter->closed = 1;
    converter->cd = (iconv_t)-1;
    if (error_number == 0) {
        (void)iconv_close(cd);
        return 1;
    }

    (void)iconv_close(cd);
    return 2;
}

static const luaL_Reg converter_methods[] = {
    {"convert", liconv_convert},
    {"reset", liconv_reset},
    {"close", liconv_close},
    {"is_closed", liconv_is_closed},
    {"__gc", liconv_gc},
    {NULL, NULL}
};

static const luaL_Reg module_functions[] = {
    {"open", liconv_open},
    {"convert", liconv_convert_function},
    {NULL, NULL}
};

LUA_API int luaopen_iconv(lua_State *L)
{
    luaL_newmetatable(L, LICONV_METATABLE);

    lua_pushliteral(L, "converter");
    lua_setfield(L, -2, "__name");

    lua_pushcfunction(L, liconv_gc);
    lua_setfield(L, -2, "__gc");

    lua_newtable(L);
    luaL_register(L, NULL, converter_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_register(L, "iconv", module_functions);
    return 1;
}
