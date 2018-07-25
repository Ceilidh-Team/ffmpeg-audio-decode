#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <node_api.h>
#include "napi_helpers.h"

// Ensure that the lavf we're building against is like the one this code is written for
_Static_assert(LIBAVCODEC_VERSION_MAJOR == 58
               && LIBAVCODEC_VERSION_MINOR >= 18
               && (LIBAVCODEC_VERSION_MINOR > 18 || LIBAVCODEC_VERSION_MICRO >= 100),
               "software written against libavcodec 58.18.100");
_Static_assert(LIBAVFORMAT_VERSION_MAJOR == 58
               && LIBAVFORMAT_VERSION_MINOR >= 12
               && (LIBAVFORMAT_VERSION_MINOR > 12 || LIBAVFORMAT_VERSION_MICRO >= 100),
               "software written against libavformat 58.12.100");

typedef struct Decoder__ {
    bool lavf_format_context_freed;
    bool lavf_format_context_input_open;
    AVFormatContext *lavf_format_context;
    AVIOContext *lavf_io_context;
    uint8_t *lavf_io_buffer;

    napi_env env;   // make the hacky assumption we only ever get called in response to js
    napi_ref decodeable;
} *Decoder;

static void finalize(napi_env env, void *data, void *hint) {
    Decoder decoder = hint;

    if (decoder->decodeable != NULL) {
        napi_value decodeable;
        TRYGOTO_NAPI(env, napi_get_reference_value(env, decoder->decodeable, &decodeable), napi_fail);

        napi_value close;
        TRYGOTO_NAPI(env, napi_create_string_utf8(env, "close", 5, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_get_property(env, decodeable, close, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_call_function(env, decodeable, close, 0, &decodeable, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_delete_reference(env, decoder->decodeable), napi_fail);
    }

napi_fail:;
    AVFormatContext *context = data;
    if (context != NULL) {
        if (context->pb != NULL && context->pb->buffer != NULL && context->pb->buffer == decoder->lavf_io_buffer) {
            av_freep(&context->pb->buffer);
        }
        if (context->pb != NULL && context->pb == decoder->lavf_io_context) {
            avio_context_free(&context->pb);
        }
        if (!decoder->lavf_format_context_freed) {
            if (decoder->lavf_format_context_input_open) {
                avformat_close_input(&context);
            } else {
                avformat_free_context(context);
            }
        }
    }

    free(decoder);
}

static int read(void *opaque, uint8_t *buf, int len) {
    Decoder decoder = opaque;

    napi_value buffer;
    TRYRET_NAPI(decoder->env, napi_create_external_buffer(decoder->env, len, buf, NULL, NULL, &buffer), AVERROR_EXTERNAL);

    napi_value decodeable;
    TRYRET_NAPI(decoder->env, napi_get_reference_value(decoder->env, decoder->decodeable, &decodeable), AVERROR_EXTERNAL);

    napi_value read;
    TRYRET_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "read", 4, &read), AVERROR_EXTERNAL);
    TRYRET_NAPI(decoder->env, napi_get_property(decoder->env, decodeable, read, &read), AVERROR_EXTERNAL);
    TRYRET_NAPI(decoder->env, napi_call_function(decoder->env, decodeable, read, 1, &buffer, &read), AVERROR_EXTERNAL);

    napi_valuetype type;
    TRYRET_NAPI(decoder->env, napi_typeof(decoder->env, read, &type), AVERROR_EXTERNAL);
    if (type == napi_undefined) {
        return AVERROR_EOF;
    }

    if (type != napi_number) {
        THROW_TYPE(decoder->env, "Read did not return number | undefined");
        return AVERROR_EXTERNAL;
    }

    TRYRET_NAPI(decoder->env, napi_get_value_int32(decoder->env, read, &len), AVERROR_EXTERNAL);
    return len;
}

static int64_t seek_or_len(void *opaque, int64_t offset, int whence) {
    Decoder decoder = opaque;

    napi_value decodeable;
    TRYRET_NAPI(decoder->env, napi_get_reference_value(decoder->env, decoder->decodeable, &decodeable), AVERROR_EXTERNAL);

    napi_value fn;
    size_t argc;
    napi_value argv[2];
    if (whence != AVSEEK_SIZE) {
        napi_value seek;
        TRYRET_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "seek", 4, &seek), AVERROR_EXTERNAL);

        bool has_seek;
        TRYRET_NAPI(decoder->env, napi_has_property(decoder->env, decodeable, seek, &has_seek), AVERROR_EXTERNAL);
        if (!has_seek) {
            return AVERROR_EXTERNAL;
        }

        TRYRET_NAPI(decoder->env, napi_get_property(decoder->env, decodeable, seek, &seek), AVERROR_EXTERNAL);
        fn = seek;
        argc = 2;
        TRYRET_NAPI(decoder->env, napi_create_int64(decoder->env, offset, &argv[0]), AVERROR_EXTERNAL);
        TRYRET_NAPI(decoder->env, napi_create_int32(decoder->env, whence, &argv[1]), AVERROR_EXTERNAL);
    } else {
        napi_value length;
        TRYRET_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "length", 6, &length), AVERROR_EXTERNAL);

        bool has_length;
        TRYRET_NAPI(decoder->env, napi_has_property(decoder->env, decodeable, length, &has_length), AVERROR_EXTERNAL);
        if (!has_length) {
            return AVERROR_EXTERNAL;
        }

        TRYRET_NAPI(decoder->env, napi_get_property(decoder->env, decodeable, length, &length), AVERROR_EXTERNAL);
        fn = length;
        argc = 0;
    }

    TRYRET_NAPI(decoder->env, napi_call_function(decoder->env, decodeable, fn, argc, argv, &fn), AVERROR_EXTERNAL);
    napi_valuetype type;
    TRYRET_NAPI(decoder->env, napi_typeof(decoder->env, fn, &type), AVERROR_EXTERNAL);
    if (type != napi_number) {
        THROW_TYPE(decoder->env, "Seek or length did not return number");
        return AVERROR_EXTERNAL;
    }

    TRYRET_NAPI(decoder->env, napi_get_value_int64(decoder->env, fn, &offset), AVERROR_EXTERNAL);
    return offset;
}

static napi_value create_decoder(napi_env env, napi_callback_info info) {
    const size_t argc_limit = 1;
    size_t argc = argc_limit;
    napi_value argv[argc_limit], this;
    void *unused_data;
    if (napi_get_cb_info(env, info, &argc, argv, &this, &unused_data) != napi_ok) {
        THROW_AUTO(env);
        goto err;
    }

    Decoder decoder = calloc(1, sizeof(*decoder));
    decoder->env = env;

    if (argc < 1) {
        THROW(env, "Expected at least 1 argument");
        goto err_unwrapped_alloc;
    }

    TRYGOTO_NAPI(env, napi_create_reference(env, argv[0], 1, &decoder->decodeable), err_unwrapped_alloc);

    const size_t initial_buffer_size = 4 * 1024; // 4 KB
    uint8_t *buffer = av_malloc(initial_buffer_size);
    decoder->lavf_io_buffer = buffer;

    AVIOContext *io_context = avio_alloc_context(
        buffer, initial_buffer_size, 0,
        decoder,
        read, NULL, seek_or_len
    );
    decoder->lavf_io_context = io_context;

    AVFormatContext *context = avformat_alloc_context();
    context->pb = io_context;

    TRYGOTO_NAPI(env, napi_wrap(env, this, context, finalize, decoder, NULL), err_unwrapped_alloc);

    int code = avformat_open_input(&context, "", NULL, NULL);
    if (code < 0) {
        // avformat_open_input frees context on failure, but the finalizer is already
        // set in napi
        decoder->lavf_format_context_freed = true;
        THROW_AVERR(env, code);
        goto err;
    }

    // now that avformat_open_input succeeded, we have to close it differently
    // than if we hadn't opened it
    decoder->lavf_format_context_input_open = true;

    code = avformat_find_stream_info(context, NULL);
    if (code < 0) {
        THROW_AVERR(env, code);
        goto err;
    }

    return this;

err_unwrapped_alloc:
    free(decoder);
err:
    return NULL;
}

static napi_value module_init(napi_env env, napi_value exports) {
    // Ensure we're running against the same version of ffmpeg we compiled against.
    if (LIBAVCODEC_VERSION_INT != avcodec_version()) {
        THROW(env, "Runtime and compile time avcodec version mismatch");
        return NULL;
    }
    if (LIBAVFORMAT_VERSION_INT != avformat_version()) {
        THROW(env, "Runtime and compile time avformat version mismatch");
        return NULL;
    }

    const size_t num_properties = 0;
    napi_property_descriptor properties[num_properties] = {
    };

    napi_value av_context_class;
    TRYRET_NAPI(env, napi_define_class(env, "Decoder", NAPI_AUTO_LENGTH, create_decoder, NULL, num_properties, properties, &av_context_class), NULL);

    const size_t num_descriptors = 1;
    napi_property_descriptor descriptors[num_descriptors] = {
        { "Decoder", NULL, NULL, NULL, NULL, av_context_class, napi_default, NULL }
    };
    TRYRET_NAPI(env, napi_define_properties(env, exports, num_descriptors, descriptors), NULL);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, module_init)
