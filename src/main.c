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
    bool lavf_format_context_input_open;
    AVFormatContext *lavf_format_context;

    AVIOContext *lavf_io_context;

    uint8_t *lavf_io_buffer;

    int lavf_desired_stream;
    AVStream *lavf_stream;

    AVCodecContext *lavf_codec_context;

    AVFrame* lavf_frame;

    napi_env env;   // make the hacky assumption we only ever get called in response to js
    napi_ref decodable;
} *Decoder;

static void decoder_finalize(napi_env env, void *data, void *hint) {
    Decoder decoder = data;

    if (decoder->decodable != NULL) {
        napi_value decodable;
        TRYGOTO_NAPI(env, napi_get_reference_value(env, decoder->decodable, &decodable), napi_fail);

        napi_value close;
        TRYGOTO_NAPI(env, napi_create_string_utf8(env, "close", 5, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_get_property(env, decodable, close, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_call_function(env, decodable, close, 0, &decodable, &close), napi_fail);
        TRYGOTO_NAPI(env, napi_delete_reference(env, decoder->decodable), napi_fail);
    }

napi_fail:;
    if (decoder->lavf_format_context != NULL) {
        AVFormatContext *context = decoder->lavf_format_context;

        if (context != NULL) {
            if (decoder->lavf_frame != NULL) {
                av_frame_free(&decoder->lavf_frame);
            }

            if (decoder->lavf_codec_context != NULL) {
                avcodec_close(decoder->lavf_codec_context);
                avcodec_free_context(&decoder->lavf_codec_context);
            }

            if (context->pb != NULL && context->pb->buffer != NULL && context->pb->buffer == decoder->lavf_io_buffer) {
                av_freep(&context->pb->buffer);
            }

            if (context->pb != NULL && context->pb == decoder->lavf_io_context) {
                avio_context_free(&context->pb);
            }

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

    napi_handle_scope scope;
    TRYRET_NAPI(decoder->env, napi_open_handle_scope(decoder->env, &scope), AVERROR_EXTERNAL);

    napi_value buffer;
    TRYGOTO_NAPI(decoder->env, napi_create_external_buffer(decoder->env, len, buf, NULL, NULL, &buffer), err);

    napi_value decodable;
    TRYGOTO_NAPI(decoder->env, napi_get_reference_value(decoder->env, decoder->decodable, &decodable), err);

    napi_value read;
    TRYGOTO_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "read", 4, &read), err);
    TRYGOTO_NAPI(decoder->env, napi_get_property(decoder->env, decodable, read, &read), err);
    TRYGOTO_NAPI(decoder->env, napi_call_function(decoder->env, decodable, read, 1, &buffer, &read), err);

    napi_valuetype type;
    TRYGOTO_NAPI(decoder->env, napi_typeof(decoder->env, read, &type), err);
    if (type == napi_undefined) {
        TRYRET_NAPI(decoder->env, napi_close_handle_scope(decoder->env, scope), AVERROR_EXTERNAL);
        return AVERROR_EOF;
    }

    if (type != napi_number) {
        THROW_TYPE(decoder->env, "Read did not return number | undefined");
        goto err;
    }

    TRYGOTO_NAPI(decoder->env, napi_get_value_int32(decoder->env, read, &len), err);
    TRYRET_NAPI(decoder->env, napi_close_handle_scope(decoder->env, scope), AVERROR_EXTERNAL);
    return len;

err:
    TRYRET_NAPI(decoder->env, napi_close_handle_scope(decoder->env, scope), AVERROR_EXTERNAL);
    return AVERROR_EXTERNAL;
}

static int64_t seek_or_len(void *opaque, int64_t offset, int whence) {
    Decoder decoder = opaque;

    napi_handle_scope scope;
    TRYRET_NAPI(decoder->env, napi_open_handle_scope(decoder->env, &scope), AVERROR_EXTERNAL);

    napi_value decodable;
    TRYGOTO_NAPI(decoder->env, napi_get_reference_value(decoder->env, decoder->decodable, &decodable), err);

    napi_value fn;
    size_t argc;
    napi_value argv[2];
    if (whence != AVSEEK_SIZE) {
        napi_value seek;
        TRYGOTO_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "seek", 4, &seek), err);

        bool has_seek;
        TRYGOTO_NAPI(decoder->env, napi_has_property(decoder->env, decodable, seek, &has_seek), err);
        if (!has_seek) {
            goto err;
        }

        TRYGOTO_NAPI(decoder->env, napi_get_property(decoder->env, decodable, seek, &seek), err);
        fn = seek;
        argc = 2;
        TRYGOTO_NAPI(decoder->env, napi_create_int64(decoder->env, offset, &argv[0]), err);
        TRYGOTO_NAPI(decoder->env, napi_create_int32(decoder->env, whence, &argv[1]), err);
    } else {
        napi_value length;
        TRYGOTO_NAPI(decoder->env, napi_create_string_utf8(decoder->env, "length", 6, &length), err);

        bool has_length;
        TRYGOTO_NAPI(decoder->env, napi_has_property(decoder->env, decodable, length, &has_length), err);
        if (!has_length) {
            goto err;
        }

        TRYGOTO_NAPI(decoder->env, napi_get_property(decoder->env, decodable, length, &length), err);
        fn = length;
        argc = 0;
    }

    TRYGOTO_NAPI(decoder->env, napi_call_function(decoder->env, decodable, fn, argc, argv, &fn), err);
    napi_valuetype type;
    TRYGOTO_NAPI(decoder->env, napi_typeof(decoder->env, fn, &type), err);
    if (type != napi_number) {
        THROW_TYPE(decoder->env, "Seek or length did not return number");
        goto err;
    }

    TRYGOTO_NAPI(decoder->env, napi_get_value_int64(decoder->env, fn, &offset), err);
    TRYRET_NAPI(decoder->env, napi_close_handle_scope(decoder->env, scope), AVERROR_EXTERNAL);
    return offset;

err:
    TRYRET_NAPI(decoder->env, napi_close_handle_scope(decoder->env, scope), AVERROR_EXTERNAL);
    return AVERROR_EXTERNAL;
}

static napi_value decoder_metadata(napi_env env, napi_callback_info info) {
    const size_t argc_limit = 1;
    size_t argc = argc_limit;
    napi_value argv[argc_limit], this;
    void *unused_data;
    TRYRET_NAPI(env, napi_get_cb_info(env, info, &argc, argv, &this, &unused_data), NULL);

    Decoder decoder;
    TRYRET_NAPI(env, napi_unwrap(env, this, (void**)&decoder), NULL);
    decoder->env = env;

    napi_value result;
    if (argc <= 0) {
        TRYRET_NAPI(env, napi_get_global(env, &result), NULL);

        napi_value fn;
        TRYRET_NAPI(env, napi_create_string_utf8(env, "Map", 3, &fn), NULL);
        TRYRET_NAPI(env, napi_get_property(env, result, fn, &fn), NULL);
        TRYRET_NAPI(env, napi_new_instance(env, fn, 0, &result, &result), NULL);
        TRYRET_NAPI(env, napi_create_string_utf8(env, "set", 3, &fn), NULL);
        TRYRET_NAPI(env, napi_get_property(env, result, fn, &fn), NULL);

        AVDictionaryEntry *tag = NULL;
            while ((tag = av_dict_get(decoder->lavf_format_context->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
            napi_value kvp[3];
            TRYRET_NAPI(env, napi_create_string_utf8(env, tag->key, NAPI_AUTO_LENGTH, &kvp[0]), NULL);
            TRYRET_NAPI(env, napi_create_string_utf8(env, tag->value, NAPI_AUTO_LENGTH, &kvp[1]), NULL);
            TRYRET_NAPI(env, napi_call_function(env, result, fn, 2, kvp, &kvp[2]), NULL);
        }
    } else {
        size_t length;
        TRYRET_NAPI(env, napi_get_value_string_utf8(env, argv[0], NULL, 0, &length), NULL);
        char key[length + 1];
        TRYRET_NAPI(env, napi_get_value_string_utf8(env, argv[0], key, length + 1, &length), NULL);

        AVDictionaryEntry *tag = av_dict_get(decoder->lavf_format_context->metadata, key, NULL, 0);
        if (tag == NULL) {
            TRYRET_NAPI(env, napi_get_undefined(env, &result), NULL);
        } else {
            TRYRET_NAPI(env, napi_create_string_utf8(env, tag->value, NAPI_AUTO_LENGTH, &result), NULL);
        }
    }

    return result;
}

static void decoder_read_finalize(napi_env env, void *data, void *hint) {
    AVFrame *backing = hint;
    av_frame_free(&backing);
}

static napi_value decoder_read(napi_env env, napi_callback_info info) {
    int code;

    size_t argc = 0;
    napi_value this;
    void *unused_data;
    TRYRET_NAPI(env, napi_get_cb_info(env, info, &argc, &this, &this, &unused_data), NULL);

    Decoder decoder;
    TRYRET_NAPI(env, napi_unwrap(env, this, (void**)&decoder), NULL);
    decoder->env = env;

    if (decoder->lavf_stream == NULL) {
        code = av_find_best_stream(decoder->lavf_format_context, AVMEDIA_TYPE_AUDIO, decoder->lavf_desired_stream, -1, NULL, 0);
        if (code < 0) {
            THROW(env, "Unknown stream");
            return NULL;
        } else {
            decoder->lavf_stream = decoder->lavf_format_context->streams[code];
        }
    }

    if (decoder->lavf_codec_context == NULL) {
        AVCodec *codec = avcodec_find_decoder(decoder->lavf_stream->codecpar->codec_id);
        if (codec == NULL) {
            THROW(env, "Failed to find codec");
            goto err;
        }

        decoder->lavf_codec_context = avcodec_alloc_context3(codec);
        code = avcodec_parameters_to_context(decoder->lavf_codec_context, decoder->lavf_stream->codecpar);
        if (code < 0) {
            avcodec_free_context(&decoder->lavf_codec_context);
            THROW_AVERR(env, code);
            goto err;
        }

        AVDictionary *options = NULL;
        av_dict_set(&options, "refcounted_frames", "1", 0);
        code = avcodec_open2(decoder->lavf_codec_context, codec, &options);
        if (code < 0) {
            // avcodec_open2 frees context on failure
            decoder->lavf_codec_context = NULL;
            THROW_AVERR(env, code);
            goto err;
        }
    }

    // See https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
    napi_value result;

retry:
    switch (code = avcodec_receive_frame(decoder->lavf_codec_context, decoder->lavf_frame)) {
        case AVERROR(EAGAIN):
        // Send more data to the decoder to be able to receive
        {
            AVPacket packet;
            av_init_packet(&packet);
            packet.data = NULL;
            packet.size = 0;

            code = av_read_frame(decoder->lavf_format_context, &packet);
            if (code == AVERROR_EOF) {
                goto eof;
            } else if (code < 0) {
                THROW_AVERR(env, code);
                goto err;
            }

            // Successfully read frame! Feed it to the codec
            if (packet.stream_index == decoder->lavf_stream->index) {
                switch (code = avcodec_send_packet(decoder->lavf_codec_context, &packet)) {
                    case AVERROR(EAGAIN):
                    // Receive more data before being able to send - this should not happen,
                    // as we always try to read before we write
                    THROW_AVERR(env, code);
                    goto read_err;

                    case AVERROR_EOF:
                    // Decoder flushed, no more data to be sent
                    break;

                    case AVERROR(EINVAL):
                    // Codec is not open or other error; codec should always be open
                    THROW_AVERR(env, code);
                    goto read_err;

                    case AVERROR(ENOMEM):
                    THROW_AVERR(env, code);
                    goto read_err;

                    default:
                    if (code < 0) {
                        // Other generic error
                        THROW_AVERR(env, code);
                        goto read_err;
                    }
                    break;
                }
            }

            av_packet_unref(&packet);
            goto retry;

read_err:
            av_packet_unref(&packet);
            goto err;
        }

        case AVERROR_EOF:
        // Decoder has been fully flushed, no more output available
        goto eof;

        case AVERROR(EINVAL):
        // Codec is not open or other error; codec should always be open
        THROW_AVERR(env, code);
        goto err;

        default:
        if (code < 0) {
            // Other generic error
            THROW_AVERR(env, code);
            goto err;
        } else {
            AVFrame *backing = NULL;
            bool is_planar = av_sample_fmt_is_planar(decoder->lavf_codec_context->sample_fmt);
            if (is_planar) {
                TRYGOTO_NAPI(env, napi_create_array_with_length(env, decoder->lavf_codec_context->channels, &result), err);

                // Each channel has a separate data pointer, linesize[0] is the size of each
                for (int i = 0; i < decoder->lavf_codec_context->channels; ++i) {
                    backing = av_frame_clone(decoder->lavf_frame);
                    napi_value wrapping;
                    TRYGOTO_NAPI(env, napi_create_external_buffer(env, backing->linesize[0], backing->extended_data[i], decoder_read_finalize, backing, &wrapping), return_err);
                    TRYGOTO_NAPI(env, napi_set_element(env, result, i, wrapping), err);
                }
            } else {
                // All channels are interleaved, linesize[0] is the size of them all
                backing = av_frame_clone(decoder->lavf_frame);
                TRYGOTO_NAPI(env, napi_create_external_buffer(env, backing->linesize[0], backing->extended_data[0], decoder_read_finalize, backing, &result), return_err);
            }
            goto success;

return_err:
            av_frame_free(&backing);
            goto err;
        }
    }

err:
    return NULL;
success:
    return result;
eof:
    TRYGOTO_NAPI(env, napi_get_undefined(env, &result), err);
    return result;
}

static napi_value decoder_constructor(napi_env env, napi_callback_info info) {
    const size_t argc_limit = 1;
    size_t argc = argc_limit;
    napi_value argv[argc_limit], this;
    void *unused_data;
    TRYGOTO_NAPI(env, napi_get_cb_info(env, info, &argc, argv, &this, &unused_data), err);

    Decoder decoder = calloc(1, sizeof(*decoder));
    decoder->env = env;

    if (argc < 1) {
        THROW(env, "Expected at least 1 argument");
        goto err_unwrapped_alloc;
    }

    TRYGOTO_NAPI(env, napi_create_reference(env, argv[0], 1, &decoder->decodable), err_unwrapped_alloc);

    decoder->lavf_frame = av_frame_alloc();

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
    decoder->lavf_format_context = context;

    TRYGOTO_NAPI(env, napi_wrap(env, this, decoder, decoder_finalize, NULL, NULL), err_unwrapped_alloc);

    int code = avformat_open_input(&context, "", NULL, NULL);
    if (code < 0) {
        // avformat_open_input frees context on failure, but the finalizer is already
        // set in napi
        decoder->lavf_format_context = NULL;
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

    const size_t num_properties = 2;
    napi_property_descriptor properties[num_properties] = {
        { "metadata", NULL, decoder_metadata, NULL, NULL, NULL, napi_default, NULL },
        { "read", NULL, decoder_read, NULL, NULL, NULL, napi_default, NULL }
    };

    napi_value decoder_class;
    TRYRET_NAPI(env, napi_define_class(env, "Decoder", NAPI_AUTO_LENGTH, decoder_constructor, NULL, num_properties, properties, &decoder_class), NULL);

    const size_t num_descriptors = 1;
    napi_property_descriptor descriptors[num_descriptors] = {
        { "Decoder", NULL, NULL, NULL, NULL, decoder_class, napi_default, NULL }
    };
    TRYRET_NAPI(env, napi_define_properties(env, exports, num_descriptors, descriptors), NULL);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, module_init)
