#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <node_api.h>

// Ensure that the lavf we're building against is like the one this code is written for
_Static_assert(LIBAVCODEC_VERSION_MAJOR == 58
               && LIBAVCODEC_VERSION_MINOR >= 18
               && (LIBAVCODEC_VERSION_MINOR > 18 || LIBAVCODEC_VERSION_MICRO >= 100),
               "software written against libavcodec 58.18.100");
_Static_assert(LIBAVFORMAT_VERSION_MAJOR == 58
               && LIBAVFORMAT_VERSION_MINOR >= 12
               && (LIBAVFORMAT_VERSION_MINOR > 12 || LIBAVFORMAT_VERSION_MICRO >= 100),
               "software written against libavformat 58.12.100");

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
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->close), napi_fail);
    }
    if (fn_env->read != NULL) {
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->read), napi_fail);
    }
    if (fn_env->seek != NULL) {
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->seek), napi_fail);
    }
    if (fn_env->length != NULL) {
        EXT_TRYGOTO(env, napi_delete_reference(env, fn_env->length), napi_fail);
    }

napi_fail:;
    AVFormatContext *context = data;
    if (context != NULL) {
        if (context->pb != NULL && context->pb->buffer != NULL && context->pb->buffer == fn_env->allocations.buffer) {
            av_freep(&context->pb->buffer);
        }
        if (context->pb != NULL && context->pb == fn_env->allocations.io_context) {
            avio_context_free(&context->pb);
        }
        if (!fn_env->allocations.context_freed) {
            if (fn_env->allocations.context_input_open) {
                avformat_close_input(&context);
            } else {
                avformat_free_context(context);
            }
        }
    }

    free(fn_env);
}

static bool check_type(napi_env env, napi_value value, napi_valuetype expected) {
    napi_valuetype actual;
    EXT_TRYRET(env, napi_typeof(env, value, &actual), false);

    return expected == actual;
}
static bool check_arity(napi_env env, napi_value fn, int expected) {
    napi_value length;
    EXT_TRYRET(env, napi_create_string_utf8(env, "length", 6, &length), false);
    EXT_TRYRET(env, napi_get_property(env, fn, length, &length), false);

    int actual;
    EXT_TRYRET(env, napi_get_value_int32(env, length, &actual), false);

    return actual >= expected;
}

static int read(void *opaque, uint8_t *buf, int len) {
    fn_env fns = opaque;

    napi_value null;
    EXT_TRYRET(fns->env, napi_get_null(fns->env, &null), AVERROR_EXTERNAL);

    napi_value buffer;
    EXT_TRYRET(fns->env, napi_create_external_buffer(fns->env, len, buf, NULL, NULL, &buffer), AVERROR_EXTERNAL);

    napi_value read;
    EXT_TRYRET(fns->env, napi_get_reference_value(fns->env, fns->read, &read), AVERROR_EXTERNAL);
    EXT_TRYRET(fns->env, napi_call_function(fns->env, null, read, 1, &buffer, &read), AVERROR_EXTERNAL);
    if (!check_type(fns->env, read, napi_number)) {
        EXT_THROW(fns->env, "Read did not return number");
        return AVERROR_EXTERNAL;
    }

    EXT_TRYRET(fns->env, napi_get_value_int32(fns->env, read, &len), AVERROR_EXTERNAL);
    return len;
}

static int64_t seek_or_len(void *opaque, int64_t offset, int whence) {
    fn_env fn_env = opaque;

    napi_ref fn_ref;
    size_t argc;
    napi_value argv[2];
    if (whence != AVSEEK_SIZE) {
        if (fn_env->seek == NULL) {
            return AVERROR_EXTERNAL;
        }

        fn_ref = fn_env->seek;
        argc = 2;
        EXT_TRYRET(fn_env->env, napi_create_int64(fn_env->env, offset, &argv[0]), AVERROR_EXTERNAL);
        EXT_TRYRET(fn_env->env, napi_create_int32(fn_env->env, whence, &argv[1]), AVERROR_EXTERNAL);
    } else {
        if (fn_env->length == NULL) {
            return AVERROR_EXTERNAL;
        }

        fn_ref = fn_env->length;
        argc = 0;
    }

    napi_value fn;
    EXT_TRYRET(fn_env->env, napi_get_reference_value(fn_env->env, fn_ref, &fn), AVERROR_EXTERNAL);
    EXT_TRYRET(fn_env->env, napi_call_function(fn_env->env, NULL, fn, 2, argv, &fn), AVERROR_EXTERNAL);
    if (!check_type(fn_env->env, fn, napi_number)) {
        EXT_THROW(fn_env->env, "Seek or length did not return number");
        return AVERROR_EXTERNAL;
    }

    EXT_TRYRET(fn_env->env, napi_get_value_int64(fn_env->env, fn, &offset), AVERROR_EXTERNAL);
    return offset;
}

static napi_value create_context(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value argv[4], this;
    void *unused_data;
    if (napi_get_cb_info(env, info, &argc, argv, &this, &unused_data) != napi_ok) {
        EXT_THROW_AUTO(env);
        goto err;
    }

    // check argument count (we take 2-4)
    if (argc < 2) {
        EXT_THROW(env, "Not enough arguments");
    }

    // 'close' argument
    if (!check_type(env, argv[0], napi_function)) {
        EXT_THROW(env, "'close' argument must be a function");
    }
    // 'read' argument
    if (!check_type(env, argv[1], napi_function) || !check_arity(env, argv[1], 1)) {
        EXT_THROW(env, "'read' argument must be a function taking at least 1 argument");
        goto err;
    }
    // 'seek' argument
    if (argc >= 3) {
        napi_valuetype type;
        EXT_TRYGOTO(env, napi_typeof(env, argv[2], &type), err);

        if (type != napi_undefined && (type != napi_function || !check_arity(env, argv[2], 2))) {
            EXT_THROW(env, "'seek' argument must be a function taking at least 2 arguments");
            goto err;
        }
    }
    // 'length' argument
    if (argc >= 4) {
        napi_valuetype type;
        EXT_TRYGOTO(env, napi_typeof(env, argv[3], &type), err);

        if (type != napi_undefined && type != napi_function) {
            EXT_THROW(env, "'length' argument must be a function");
            goto err;
        }
    }

    fn_env fn_env = calloc(1, sizeof(*fn_env));
    fn_env->env = env;

    napi_value undefined;
    EXT_TRYGOTO(env, napi_get_undefined(env, &undefined), err_unwrapped_alloc);
    bool has_seek, has_length;
    EXT_TRYGOTO(env, napi_strict_equals(env, undefined, argv[2], &has_seek), err_unwrapped_alloc);
    has_seek = !has_seek; // negate, undefined === undefined is true
    EXT_TRYGOTO(env, napi_strict_equals(env, undefined, argv[3], &has_length), err_unwrapped_alloc);
    has_length = !has_length; // ditto

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

    napi_ref object_ref;
    EXT_TRYGOTO(env, napi_wrap(env, this, context, finalize, fn_env, &object_ref), err);

    EXT_TRYGOTO(env, napi_create_reference(env, argv[0], 1, &fn_env->close), err);
    EXT_TRYGOTO(env, napi_create_reference(env, argv[1], 1, &fn_env->read), err);
    if (has_seek) {
        EXT_TRYGOTO(env, napi_create_reference(env, argv[2], 1, &fn_env->seek), err);
    }
    if (has_length) {
        EXT_TRYGOTO(env, napi_create_reference(env, argv[3], 1, &fn_env->length), err);
    }

    int code = avformat_open_input(&context, "", NULL, NULL);
    if (code < 0) {
        // avformat_open_input frees context on failure, but the finalizer is already
        // set in napi
        fn_env->allocations.context_freed = true;

        const size_t err_buffer_len = 1024;
        char err[err_buffer_len] = { 0 };
        av_strerror(code, err, err_buffer_len);

        EXT_THROW(env, err);
        goto err;
    }

    // now that avformat_open_input succeeded, we have to close it differently
    // than if we hadn't opened
    fn_env->allocations.context_input_open = true;

    napi_value object;
    EXT_TRYGOTO(env, napi_get_reference_value(env, object_ref, &object), err);
    return object;

err_unwrapped_alloc:
    free(fn_env);
err:
    return NULL;
}

static napi_value module_init(napi_env env, napi_value exports) {
    // Ensure we're running against the same version of ffmpeg we compiled against.
    if (LIBAVCODEC_VERSION_INT != avcodec_version()) {
        EXT_THROW(env, "Runtime and compile time avcodec version mismatch");
        return NULL;
    }
    if (LIBAVFORMAT_VERSION_INT != avformat_version()) {
        EXT_THROW(env, "Runtime and compile time avformat version mismatch");
        return NULL;
    }

    const size_t num_properties = 0;
    napi_property_descriptor properties[num_properties] = {
    };

    napi_value av_context_class;
    EXT_TRYRET(env, napi_define_class(env, "Decoder", NAPI_AUTO_LENGTH, create_context, NULL, num_properties, properties, &av_context_class), NULL);

    const size_t num_descriptors = 1;
    napi_property_descriptor descriptors[num_descriptors] = {
        { "Decoder", NULL, NULL, NULL, NULL, av_context_class, napi_default, NULL }
    };
    EXT_TRYRET(env, napi_define_properties(env, exports, num_descriptors, descriptors), NULL);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, module_init)
