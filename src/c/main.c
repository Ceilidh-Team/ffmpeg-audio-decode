#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <node_api.h>

// #define DEBUG

#ifdef DEBUG
#include <stdio.h>
#define TRACE(args...) do { fprintf(stderr, args); fflush(stderr); } while(0)
#else
#define TRACE(args...) do { } while(0)
#endif

_Static_assert(LIBAVCODEC_VERSION_MAJOR == 58
               && LIBAVCODEC_VERSION_MINOR >= 18
               && (LIBAVCODEC_VERSION_MINOR > 18 || LIBAVCODEC_VERSION_MICRO >= 100),
               "package written against libavcodec 58.18.100");
_Static_assert(LIBAVFORMAT_VERSION_MAJOR == 58
               && LIBAVFORMAT_VERSION_MINOR >= 12
               && (LIBAVFORMAT_VERSION_MINOR > 12 || LIBAVFORMAT_VERSION_MICRO >= 100),
               "package written against libavformat 58.12.100");

// Helpers for throwing, shortening code
#define STRINGIFY_INTERNAL__(val) #val
#define STRINGIFY__(val) STRINGIFY_INTERNAL__(val)

#define EXT_THROW(env, message) napi_throw_error((env), "line " STRINGIFY__(__LINE__), (message))
#define EXT_THROW_AUTO(env) do {            \
    const napi_extended_error_info *info;   \
    napi_get_last_error_info((env), &info); \
    EXT_THROW(env, info->error_message);    \
} while (0)

#define EXT_TRY(env, call) do { \
    if ((call) != napi_ok) {    \
        EXT_THROW_AUTO((env));  \
        return;                 \
    }                           \
} while(0)
#define EXT_TRYRET(env, call, res) do { \
    if ((call) != napi_ok) {            \
        EXT_THROW_AUTO((env));          \
        return (res);                   \
    }                                   \
} while(0)
#define EXT_TRYGOTO(env, call, label) do { \
    if ((call) != napi_ok) {               \
        EXT_THROW_AUTO(env);               \
        goto label;                        \
    }                                      \
} while(0)

typedef struct allocated_objects__ {
    bool context_freed;
    bool context_input_open;
    AVIOContext *io_context;
    uint8_t *buffer;
} *allocated_objects;
typedef struct fn_env__ {
    struct allocated_objects__ allocations;
    napi_env env;
    napi_ref close;
    napi_ref read;
    napi_ref seek;
    napi_ref length;
} *fn_env;

static void finalize(napi_env env, void *data, void *hint) {
    fn_env fn_env = hint;

    if (fn_env->close != NULL) {
        TRACE("finalize|release close\n");
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->close), napi_fail);
    }
    if (fn_env->read != NULL) {
        TRACE("finalize|release read\n");
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->read), napi_fail);
    }
    if (fn_env->seek != NULL) {
        TRACE("finalize|release seek\n");
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->seek), napi_fail);
    }
    if (fn_env->length != NULL) {
        TRACE("finalize|release length\n");
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->length), napi_fail);
    }

napi_fail:;
    AVFormatContext *context = data;
    if (context != NULL) {
        if (context->pb != NULL && context->pb->buffer != NULL && context->pb->buffer == fn_env->allocations.buffer) {
            TRACE("finalize|free allocated buffer\n");
            av_freep(&context->pb->buffer);
        }
        if (context->pb != NULL && context->pb == fn_env->allocations.io_context) {
            TRACE("finalize|free allocated io context\n");
            avio_context_free(&context->pb);
        }
        if (!fn_env->allocations.context_freed) {
            TRACE("finalize|free context\n");
            if (fn_env->allocations.context_input_open) {
                avformat_close_input(&context);
            } else {
                avformat_free_context(context);
            }
        }
    }

    TRACE("finalize|free fn_env %p\n", hint);
    free(fn_env);

    TRACE("finalize|success\n");
}

static int read(void *opaque, uint8_t *buf, int len) {
    TRACE("read|%d bytes\n", len);

    fn_env fns = opaque;

    TRACE("read|get js null\n");
    napi_value null;
    EXT_TRYRET(fns->env, napi_get_null(fns->env, &null), AVERROR_EXTERNAL);

    TRACE("read|create buffer\n");
    napi_value buffer;
    EXT_TRYRET(fns->env, napi_create_external_buffer(fns->env, len, buf, NULL, NULL, &buffer), AVERROR_EXTERNAL);

    TRACE("read|get fn\n");
    napi_value read_fn;
    EXT_TRYRET(fns->env, napi_get_reference_value(fns->env, fns->read, &read_fn), AVERROR_EXTERNAL);

    TRACE("read|call\n");
    napi_value res;
    if (napi_call_function(fns->env, null, read_fn, 1, &buffer, &res) != napi_ok) {
        TRACE("read|call failed\n");
        const napi_extended_error_info *info;
        napi_get_last_error_info(fns->env, &info);
        TRACE("read|err: %s\n", info->error_message);
        return AVERROR_EXTERNAL;
    }

    TRACE("read|call success\n");

    napi_valuetype type;
    EXT_TRYRET(fns->env, napi_typeof(fns->env, res, &type), AVERROR_EXTERNAL);
    if (type != napi_number) {
        EXT_THROW(fns->env, "Read did not return number");
        return AVERROR_EXTERNAL;
    }

    EXT_TRYRET(fns->env, napi_get_value_int32(fns->env, res, &len), AVERROR_EXTERNAL);
    return len;
}

static int64_t seek_or_len(void *opaque, int64_t offset, int whence) {
    fn_env fn_env = opaque;

    if (whence != AVSEEK_SIZE) {
        // looking for seek
        if (fn_env->seek == NULL) {
            return AVERROR_EXTERNAL;
        }

        napi_value seek_fn;
        EXT_TRYRET(fn_env->env, napi_get_reference_value(fn_env->env, fn_env->seek, &seek_fn), AVERROR_EXTERNAL);

        napi_value argv[2];
        EXT_TRYRET(fn_env->env, napi_create_int64(fn_env->env, offset, &argv[0]), AVERROR_EXTERNAL);
        EXT_TRYRET(fn_env->env, napi_create_int32(fn_env->env, whence, &argv[1]), AVERROR_EXTERNAL);

        napi_value res;
        EXT_TRYRET(fn_env->env, napi_call_function(fn_env->env, NULL, seek_fn, 2, argv, &res), AVERROR_EXTERNAL);

        napi_valuetype type;
        EXT_TRYRET(fn_env->env, napi_typeof(fn_env->env, res, &type), AVERROR_EXTERNAL);
        if (type != napi_number) {
            EXT_THROW(fn_env->env, "Seek did not return number");
            return AVERROR_EXTERNAL;
        }

        EXT_TRYRET(fn_env->env, napi_get_value_int64(fn_env->env, res, &offset), AVERROR_EXTERNAL);
        return offset;
    } else {
        // looking for length
        if (fn_env->length == NULL) {
            return AVERROR_EXTERNAL;
        }

        napi_value length_fn;
        EXT_TRYRET(fn_env->env, napi_get_reference_value(fn_env->env, fn_env->length, &length_fn), AVERROR_EXTERNAL);

        napi_value res;
        EXT_TRYRET(fn_env->env, napi_call_function(fn_env->env, NULL, length_fn, 0, NULL, &res), AVERROR_EXTERNAL);

        napi_valuetype type;
        EXT_TRYRET(fn_env->env, napi_typeof(fn_env->env, res, &type), AVERROR_EXTERNAL);
        if (type != napi_number) {
            EXT_THROW(fn_env->env, "Length did not return number");
            return AVERROR_EXTERNAL;
        }

        EXT_TRYRET(fn_env->env, napi_get_value_int64(fn_env->env, res, &offset), AVERROR_EXTERNAL);
        return offset;
    }
}

static napi_value create_context(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value argv[4], unused_this;
    void *unused_data;
    if (napi_get_cb_info(env, info, &argc, argv, &unused_this, &unused_data) != napi_ok) {
        TRACE("create_context|cb_info fail\n");
        EXT_THROW_AUTO(env);
        goto err;
    }

    // Assume we were passed a close, read, seek, and length function (in that order),
    // with the last two optionally null. Type checking will be the job of Javascript.
    fn_env fn_env = calloc(1, sizeof(*fn_env));
    fn_env->env = env;

    TRACE("create_context|check undefined\n");
    napi_value undefined;
    EXT_TRYGOTO(env, napi_get_undefined(env, &undefined), err_unwrapped_alloc);
    bool has_seek, has_length;
    EXT_TRYGOTO(env, napi_strict_equals(env, undefined, argv[2], &has_seek), err_unwrapped_alloc);
    has_seek = !has_seek; // negate, undefined === undefined is true
    EXT_TRYGOTO(env, napi_strict_equals(env, undefined, argv[3], &has_length), err_unwrapped_alloc);
    has_length = !has_length; // ditto

    TRACE("create_context|alloc av\n");
    const size_t initial_buffer_size = 4 * 1024; // 4 KB
    uint8_t *buffer = av_malloc(initial_buffer_size);
    fn_env->allocations.buffer = buffer;

    AVIOContext *io_context = avio_alloc_context(
        buffer, initial_buffer_size, 0,
        fn_env,
        read, NULL, (has_seek || has_length) ? seek_or_len : NULL
    );
    fn_env->allocations.io_context = io_context;

    AVFormatContext *context = avformat_alloc_context();
    context->pb = io_context;

    TRACE("create_context|create external\n");
    napi_value external;
    EXT_TRYGOTO(env, napi_create_external(env, context, finalize, fn_env, &external), err);

    TRACE("create_context|create references\n");
    EXT_TRYGOTO(env, napi_create_reference(env, argv[0], 1, &fn_env->close), err);
    EXT_TRYGOTO(env, napi_create_reference(env, argv[1], 1, &fn_env->read), err);
    if (has_seek) {
        TRACE("create_context|has seek\n");
        EXT_TRYGOTO(env, napi_create_reference(env, argv[2], 1, &fn_env->seek), err);
    }
    if (has_length) {
        TRACE("create_context|has length\n");
        EXT_TRYGOTO(env, napi_create_reference(env, argv[3], 1, &fn_env->length), err);
    }

    TRACE("create_context|open\n");
    int code = avformat_open_input(&context, "", NULL, NULL);
    if (code < 0) {
        // avformat_open_input frees context on failure, but the finalizer is already
        // set in napi.
        fn_env->allocations.context_freed = true;

        const size_t err_buffer_len = 1024;
        char err[err_buffer_len] = { 0 };
        av_strerror(code, err, err_buffer_len);

        TRACE("create_context|open failed\n");
        EXT_THROW(env, err);
        goto err;
    }

    fn_env->allocations.context_input_open = true;
    return external;

err_unwrapped_alloc:
    free(fn_env);
err:
    return NULL;
}

static napi_value module_init(napi_env env, napi_value exports) {
    // Ensure we're running against the same version of ffmpeg we compiled against.
    if (LIBAVCODEC_VERSION_INT != avcodec_version()) {
        EXT_THROW(env, "Compiled and runtime avcodec version mismatch");
        return NULL;
    }
    if (LIBAVFORMAT_VERSION_INT != avformat_version()) {
        EXT_THROW(env, "Compiled and runtime avformat version mismatch");
        return NULL;
    }

    const size_t num_descriptors = 1;
    napi_property_descriptor descriptors[num_descriptors] = {
        { "createContext", NULL, create_context, NULL, NULL, NULL, napi_default, NULL }
    };
    EXT_TRYRET(env, napi_define_properties(env, exports, num_descriptors, descriptors), NULL);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, module_init)