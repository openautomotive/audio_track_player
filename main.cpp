#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdint.h>
#include <signal.h>

#include <binder/MemoryBase.h>
#include <binder/MemoryDealer.h>
#include <binder/MemoryHeapBase.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>

using namespace android;

#define NUM_ARGUMENTS 10
#define VERSION_VALUE "1.0"

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

struct riff_wave_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t wave_id;
};

struct chunk_header {
    uint32_t id;
    uint32_t sz;
};

struct chunk_fmt {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

static int stop_flag = 0;

void stream_close(int sig)
{
    /* allow the stream to be closed gracefully */
    signal(sig, SIG_IGN);
    stop_flag = 1;
}

void callback(int event __unused, void* user __unused, void *info __unused)
{
}

void play_sample(FILE *file, unsigned int card, unsigned int device, unsigned int channels,
                 unsigned int rate, unsigned int bits, unsigned int period_size,
                 unsigned int period_count)
{
    char *buffer;
    int size;
    int num_read;
    audio_stream_type_t streamType;
    uint32_t sampleRate;
    audio_format_t format;
    audio_channel_mask_t channelMask;
    size_t frameCount;
    int32_t notificationFrames;
    uint32_t useSharedBuffer;
    audio_output_flags_t flags;
    audio_session_t sessionId;
    audio_usage_t usage;
    audio_content_type_t contentType;
    audio_attributes_t attributes;
    sp<IMemory> sharedBuffer;
    sp<MemoryDealer> heap;
    audio_offload_info_t offloadInfo = AUDIO_INFO_INITIALIZER;
    status_t status;
    bool offload = false;
    bool fast = false;
    sp<AudioTrack> track = new AudioTrack();

    if (card == 1) {
        flags = AUDIO_OUTPUT_FLAG_PRIMARY;
    } else if (card == 2) {
        flags = AUDIO_OUTPUT_FLAG_FAST;
    } else if (card == 3) {
        flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
    } else {
        flags = AUDIO_OUTPUT_FLAG_NONE;
    }

    if (device == AUDIO_STREAM_MUSIC) {
        streamType = AUDIO_STREAM_MUSIC;
        usage = AUDIO_USAGE_MEDIA;
        contentType = AUDIO_CONTENT_TYPE_MUSIC;
    } else if (device == AUDIO_STREAM_TTS) {
        streamType = AUDIO_STREAM_TTS;
        usage = AUDIO_USAGE_ASSISTANT;
        contentType = AUDIO_CONTENT_TYPE_AUDIO_ASSISTANT;
    } else if (device == AUDIO_STREAM_NAVI) {
        streamType = AUDIO_STREAM_NAVI;
        usage = AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE;
        contentType = AUDIO_CONTENT_TYPE_NAVI_GUIDANCE;
    } else if (device > AUDIO_STREAM_VOICE_CALL && device <= AUDIO_STREAM_ECALL) {
        streamType = (audio_stream_type_t)device;
        usage = AUDIO_USAGE_MEDIA;
        contentType = AUDIO_CONTENT_TYPE_MUSIC;
    } else {
        streamType = AUDIO_STREAM_MUSIC;
        usage = AUDIO_USAGE_MEDIA;
        contentType = AUDIO_CONTENT_TYPE_MUSIC;
    }

    sampleRate = rate;
    if (bits == 32)
        format = AUDIO_FORMAT_PCM_32_BIT;
    else if (bits == 24)
        format = AUDIO_FORMAT_PCM_8_24_BIT;
    else if (bits == 16)
        format = AUDIO_FORMAT_PCM_16_BIT;
    else
        format = AUDIO_FORMAT_INVALID;
        

    if (channels == 4)
        channelMask = AUDIO_CHANNEL_OUT_QUAD;
    else if (channels == 6)
        channelMask = AUDIO_CHANNEL_OUT_5POINT1;
    else
        channelMask = AUDIO_CHANNEL_OUT_STEREO;
    //frameCount = 48000;
    frameCount = period_size * period_count;
    notificationFrames = frameCount;
    useSharedBuffer = 0;
    sessionId = AUDIO_SESSION_NONE;

    if ((flags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
        /**/
        useSharedBuffer = true;
    }

    if (useSharedBuffer != 0) {
        size_t heapSize = audio_channel_count_from_out_mask(channelMask) *
                audio_bytes_per_sample(format) * frameCount;
        heap = new MemoryDealer(heapSize, "AudioTrack Heap Base");
        sharedBuffer = heap->allocate(heapSize);
        frameCount = 0;
        notificationFrames = 0;
    }
    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0) {
        offloadInfo.sample_rate = sampleRate;
        offloadInfo.channel_mask = channelMask;
        offloadInfo.format = format;
        offload = true;
    }
    if ((flags & AUDIO_OUTPUT_FLAG_FAST) != 0) {
        fast = true;
    }
#if 0
    /* set music stream */
    AudioSystem::setStreamVolumeIndex(AUDIO_STREAM_MUSIC,
                                           6,
                                           AUDIO_DEVICE_OUT_AUX_DIGITAL);
#endif
#if 0
    /* get device states */
    fprintf(stderr, "get connnet state speaker:%d, aux:%d\n",
        AudioSystem::getDeviceConnectionState(AUDIO_DEVICE_OUT_SPEAKER, ""),
        AudioSystem::getDeviceConnectionState(AUDIO_DEVICE_OUT_AUX_DIGITAL, ""));
    /* set device states */
    fprintf(stderr, "set connnet ret aux:%d, speaker:%d\n",
        AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_AUX_DIGITAL, AUDIO_POLICY_DEVICE_STATE_AVAILABLE, "", ""),
        AudioSystem::setDeviceConnectionState(AUDIO_DEVICE_OUT_SPEAKER, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE, "", ""));
    usleep(500 * 1000);
    fprintf(stderr, "get connnet state speaker:%d, aux:%d\n",
        AudioSystem::getDeviceConnectionState(AUDIO_DEVICE_OUT_SPEAKER, ""),
        AudioSystem::getDeviceConnectionState(AUDIO_DEVICE_OUT_AUX_DIGITAL, ""));
#endif
    memset(&attributes, 0, sizeof(attributes));
    attributes.content_type = contentType;
    attributes.usage = usage;

    track->set(streamType,
               sampleRate,
               format,
               channelMask,
               frameCount,
               flags,
               (fast || offload) ? callback : nullptr,
               nullptr,
               notificationFrames,
               sharedBuffer,
               false,
               sessionId,
               ((fast && sharedBuffer == 0) || offload) ?
                       AudioTrack::TRANSFER_CALLBACK : AudioTrack::TRANSFER_DEFAULT,
               offload ? &offloadInfo : nullptr,
               getuid(),
               getpid(),
               &attributes, /*&attributes*/
               false,
               1.0f,
               AUDIO_PORT_HANDLE_NONE);
    status = track->initCheck();
    if (status != NO_ERROR) {
        return;
    }

    size = track->getBufferSizeInFrames();
    buffer = (char *)malloc(size);
    if (!buffer) {
        fprintf(stderr, "Unable to allocate %d bytes\n", size);
        //free(buffer);
        return;
    }

    printf("Playing sample: %u ch, %u hz, %u bit\n", channels, rate, bits);

    /* catch ctrl-c to shutdown cleanly */
    signal(SIGINT, stream_close);

    track->start();
    do {
        num_read = fread(buffer, 1, size, file);
        if (num_read > 0) {
            if (track->write(buffer, num_read, true) < 0) {
                fprintf(stderr, "Error playing sample\n");
                break;
            }
        }
    } while (!stop_flag && num_read > 0);

    track->stop();

    free(buffer);
}

int main(int argc __unused, char **argv)
{
/** copy from tinyplay start **/
    FILE *file;
    struct riff_wave_header riff_wave_header;
    struct chunk_header chunk_header;
    struct chunk_fmt chunk_fmt;
    unsigned int device = 0;
    unsigned int card = 0;
    unsigned int period_size = 1024;
    unsigned int period_count = 4;
    char *filename;
    int more_chunks = 1;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s file.wav [-D card] [-d device] [-p period_size]"
                " [-n n_periods] \n", argv[0]);
        return 1;
    }

    filename = argv[1];
    file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open file '%s'\n", filename);
        return 1;
    }

    fread(&riff_wave_header, sizeof(riff_wave_header), 1, file);
    if ((riff_wave_header.riff_id != ID_RIFF) ||
        (riff_wave_header.wave_id != ID_WAVE)) {
        fprintf(stderr, "Error: '%s' is not a riff/wave file\n", filename);
        fclose(file);
        return 1;
    }

    do {
        fread(&chunk_header, sizeof(chunk_header), 1, file);

        switch (chunk_header.id) {
        case ID_FMT:
            fread(&chunk_fmt, sizeof(chunk_fmt), 1, file);
            /* If the format header is larger, skip the rest */
            if (chunk_header.sz > sizeof(chunk_fmt))
                fseek(file, chunk_header.sz - sizeof(chunk_fmt), SEEK_CUR);
            break;
        case ID_DATA:
            /* Stop looking for chunks */
            more_chunks = 0;
            break;
        default:
            /* Unknown chunk, skip bytes */
            fseek(file, chunk_header.sz, SEEK_CUR);
        }
    } while (more_chunks);

    /* parse command line arguments */
    argv += 2;
    while (*argv) {
        if (strcmp(*argv, "-d") == 0) {
            argv++;
            if (*argv)
                device = atoi(*argv);
        }
        if (strcmp(*argv, "-p") == 0) {
            argv++;
            if (*argv)
                period_size = atoi(*argv);
        }
        if (strcmp(*argv, "-n") == 0) {
            argv++;
            if (*argv)
                period_count = atoi(*argv);
        }
        if (strcmp(*argv, "-D") == 0) {
            argv++;
            if (*argv)
                card = atoi(*argv);
        }
        if (*argv)
            argv++;
    }
    play_sample(file, card, device, chunk_fmt.num_channels, chunk_fmt.sample_rate,
                chunk_fmt.bits_per_sample, period_size, period_count);
    fclose(file);
/** copy from tinyplay end **/
    return 0;
}
