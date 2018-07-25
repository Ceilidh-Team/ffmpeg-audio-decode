#ifndef NAPI_HELPERS_H
#define NAPI_HELPERS_H

#define STRINGIFY_INTERNAL__(val) #val

#define STRINGIFY__(val) STRINGIFY_INTERNAL__(val)

#define THROW(env, message) napi_throw_error((env), "ffmpeg-audio-decode@" STRINGIFY__(__LINE__), (message))

#define THROW_TYPE(env, message) napi_throw_type_error((env), "ffmpeg-audio-decode@" STRINGIFY__(__LINE__), (message))

#define THROW_AUTO(env) do {                \
    const napi_extended_error_info *info;   \
    napi_get_last_error_info((env), &info); \
    THROW((env), info->error_message);      \
} while (0)

#define THROW_AVERR(env, code) do {    \
    const size_t buf_len = 256;        \
    char buf[buf_len] = { 0 };         \
    av_strerror((code), buf, buf_len); \
    THROW((env), buf);                 \
} while (0)

/**
 * @brief Forward NAPI call exception.
 * 
 * Check the result of a call for errors, throw it to JS if it's found
 * and return the status object right away. Otherwise, continue execution.
 */
#define TRY_NAPI(env, call) do {                \
    napi_status status = (call);                \
    if (status != napi_ok) {                    \
        if (status != napi_pending_exception) { \
            THROW_AUTO((env));                  \
        }                                       \
        return status;                          \
    }                                           \
} while(0)

/**
 * @brief Forward NAPI call exception, with a custom return value
 * 
 * Check the result of a call for errors, throw it to JS if it's found
 * and return the specified value right away. Otherwise, continue execution.
 */
#define TRYRET_NAPI(env, call, res) do {        \
    napi_status status = (call);                \
    if (status != napi_ok) {                    \
        if (status != napi_pending_exception) { \
            THROW_AUTO((env));                  \
        }                                       \
        return (res);                           \
    }                                           \
} while(0)

/**
 * @brief Forward NAPI call exception, with goto label.
 * 
 * Check the result of a call for errors, throw it to JS if it's found
 * and goto the specified label. Otherwise, continue execution.
 */
#define TRYGOTO_NAPI(env, call, label) do {     \
    napi_status status = (call);                \
    if (status != napi_ok) {                    \
        if (status != napi_pending_exception) { \
            THROW_AUTO((env));                  \
        }                                       \
        goto label;                             \
    }                                           \
} while(0)

#endif