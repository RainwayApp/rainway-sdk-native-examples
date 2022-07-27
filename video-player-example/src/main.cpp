// This is a command-line utility that uses MediaFoundation together with
// Rainway's BYOFB mode to stream a video file over Rainway. Use it as:
//
//     video-player-example.exe pk_live_YourRainwayApiKey C:\path\to\media.mp4
//
// Then connect from https://webdemo.rainway.com/ to see your video.
//
// For more info, see: https://docs.rainway.com/docs/byofb

// we don't want windows MIN/MAX macros, we'll use stl versions
#define NOMINMAX 1

#include <winrt/base.h>

#include <algorithm>
#include <cassert>
#include <mfapi.h>
#include <mferror.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <optional>
#include <string>
#include <unordered_set>
#include <wmcodecdsp.h>

#include <d3d11.h>
#include <d3d11_4.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#define WI_VERIFY_SUCCEEDED(x) WINRT_VERIFY((x) == S_OK)

DEFINE_ENUM_FLAG_OPERATORS(D3D11_CREATE_DEVICE_FLAG);

constexpr auto AUDIO_SAMPLE_RATE = 44100u;

namespace dx
{
    winrt::com_ptr<ID3D11Device> create_device()
    {
        D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1,
        };
        auto feature_levels_len = ARRAYSIZE(feature_levels);

        auto flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

#if !defined(NDEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        winrt::com_ptr<ID3D11Device> device = nullptr;
        winrt::com_ptr<ID3D11DeviceContext> context = nullptr;

        WI_VERIFY_SUCCEEDED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            feature_levels,
            feature_levels_len,
            D3D11_SDK_VERSION,
            device.put(),
            nullptr,
            context.put()));

        // Enable multithread protection: "It is mandatory since Media
        // Foundation is heavilty multithreaded by its nature and running
        // unprotected you quickly hit a corruption."
        // https://stackoverflow.com/a/59760070/257418
        winrt::com_ptr<ID3D11Multithread> multithread;
        WI_VERIFY_SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(multithread.put())));
        multithread->SetMultithreadProtected(true);

        return device;
    }

    /// @brief Create a D3D11 texture
    /// @param device Device to create texture with
    /// @param w Width of texture
    /// @param h Height of texture
    /// @param format DXGI format of texture to create.
    winrt::com_ptr<ID3D11Texture2D> create_texture(
        winrt::com_ptr<ID3D11Device>& device,
        uint32_t w,
        uint32_t h,
        DXGI_FORMAT format)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

        winrt::com_ptr<ID3D11Texture2D> texture = nullptr;
        WI_VERIFY_SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.put()));

        return texture;
    }
} // namespace dx

namespace mf
{
    /// @brief Print debug information about the video format of the source reader
    /// @param source_reader Source reader
    void debug_video_format(winrt::com_ptr<IMFSourceReader>& source_reader)
    {
        winrt::com_ptr<IMFMediaType> native_type = nullptr;
        winrt::com_ptr<IMFMediaType> first_output = nullptr;
        WI_VERIFY_SUCCEEDED(
            source_reader->GetNativeMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                MF_SOURCE_READER_CURRENT_TYPE_INDEX,
                native_type.put()));

        WI_VERIFY_SUCCEEDED(
            source_reader->GetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                first_output.put()));

        uint32_t width = 0, height = 0;
        WI_VERIFY_SUCCEEDED(MFGetAttributeSize(first_output.get(), MF_MT_FRAME_SIZE, &width, &height));

        uint64_t val = 0;
        WI_VERIFY_SUCCEEDED(native_type->GetUINT64(MF_MT_FRAME_RATE, &val));
        auto fps = float(HI32(val)) / float(LO32(val));

        printf("VO: %dx%d (@ %f fps)\n", width, height, fps);
    }

    /// @brief Print debug information about the audio format of the source reader
    /// and resampler
    /// @param source_reader Source reader
    /// @param resampler Resampler
    void debug_audio_format(
        winrt::com_ptr<IMFSourceReader>& source_reader,
        winrt::com_ptr<IMFTransform>& resampler)
    {
        winrt::com_ptr<IMFMediaType> native_type = nullptr;
        winrt::com_ptr<IMFMediaType> first_output = nullptr;

        WI_VERIFY_SUCCEEDED(
            source_reader->GetNativeMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                MF_SOURCE_READER_CURRENT_TYPE_INDEX,
                native_type.put()));

        WI_VERIFY_SUCCEEDED(
            source_reader->GetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                first_output.put()));

        uint32_t channels = 0;
        uint32_t samples_per_sec = 0;
        uint32_t bits_per_sample = 0;
        uint32_t block_align = 0;
        uint32_t avg_per_sec = 0;

        WI_VERIFY_SUCCEEDED(native_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
        WI_VERIFY_SUCCEEDED(native_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samples_per_sec));
        WI_VERIFY_SUCCEEDED(native_type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample));
        WI_VERIFY_SUCCEEDED(native_type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_align));
        WI_VERIFY_SUCCEEDED(native_type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg_per_sec));

        printf("AI: c:%d sps:%d bps:%d ba:%d aps:%d\n", channels, samples_per_sec, bits_per_sample, block_align, avg_per_sec);

        WI_VERIFY_SUCCEEDED(first_output->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
        WI_VERIFY_SUCCEEDED(first_output->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samples_per_sec));
        WI_VERIFY_SUCCEEDED(first_output->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample));
        WI_VERIFY_SUCCEEDED(first_output->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_align));
        WI_VERIFY_SUCCEEDED(first_output->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg_per_sec));

        printf("AO: c:%d sps:%d bps:%d ba:%d aps:%d\n", channels, samples_per_sec, bits_per_sample, block_align, avg_per_sec);

        winrt::com_ptr<IMFMediaType> resampler_input = nullptr;
        winrt::com_ptr<IMFMediaType> resamper_output = nullptr;

        WI_VERIFY_SUCCEEDED(
            resampler->GetInputCurrentType(0, resampler_input.put()));

        WI_VERIFY_SUCCEEDED(
            resampler->GetOutputCurrentType(0, resamper_output.put()));

        WI_VERIFY_SUCCEEDED(resampler_input->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
        WI_VERIFY_SUCCEEDED(resampler_input->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samples_per_sec));
        WI_VERIFY_SUCCEEDED(resampler_input->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample));
        WI_VERIFY_SUCCEEDED(resampler_input->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_align));
        WI_VERIFY_SUCCEEDED(resampler_input->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg_per_sec));

        printf("RI: c:%d sps:%d bps:%d ba:%d aps:%d\n", channels, samples_per_sec, bits_per_sample, block_align, avg_per_sec);

        WI_VERIFY_SUCCEEDED(resamper_output->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
        WI_VERIFY_SUCCEEDED(resamper_output->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samples_per_sec));
        WI_VERIFY_SUCCEEDED(resamper_output->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample));
        WI_VERIFY_SUCCEEDED(resamper_output->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_align));
        WI_VERIFY_SUCCEEDED(resamper_output->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg_per_sec));

        printf("RO: c:%d sps:%d bps:%d ba:%d aps:%d\n", channels, samples_per_sec, bits_per_sample, block_align, avg_per_sec);
    }

    void debug_media_format(
        winrt::com_ptr<IMFSourceReader>& source_reader,
        winrt::com_ptr<IMFTransform>& resampler)
    {
        debug_video_format(source_reader);
        debug_audio_format(source_reader, resampler);
    }

    struct OpenMediaResult
    {
        winrt::com_ptr<IMFMediaSource> source;
        winrt::com_ptr<IMFSourceReader> source_reader;
        winrt::com_ptr<IMFTransform> resampler;
        winrt::com_ptr<IMFDXGIDeviceManager> device_manager;
    };

    /// @brief Open a media file (.mp4)
    /// @param device Device to initialise media foundation source reader with
    /// @param utf8_filename filename to open
    /// @return result of opening the media file
    OpenMediaResult open_media(winrt::com_ptr<ID3D11Device>& device, const char* utf8_filename)
    {
        // Make sure that MF is loaded
        WI_VERIFY_SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));

        wchar_t filename[256];
        mbstowcs(filename, utf8_filename, sizeof(filename) / sizeof(wchar_t));

        winrt::com_ptr<IMFSourceResolver> source_resolver = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateSourceResolver(source_resolver.put()));

        MF_OBJECT_TYPE object_type = MF_OBJECT_INVALID;
        winrt::com_ptr<IUnknown> source_unknown = nullptr;
        winrt::com_ptr<IMFMediaSource> source = nullptr;

        WI_VERIFY_SUCCEEDED(
            source_resolver->CreateObjectFromURL(
                filename,
                MF_RESOLUTION_MEDIASOURCE,
                nullptr,
                &object_type,
                source_unknown.put()));

        WI_VERIFY_SUCCEEDED(
            source_unknown->QueryInterface(IID_PPV_ARGS(source.put())));

        // Set up video

        uint32_t reset_token = 0;
        winrt::com_ptr<IMFDXGIDeviceManager> device_manager = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateDXGIDeviceManager(&reset_token, device_manager.put()));
        WI_VERIFY_SUCCEEDED(device_manager->ResetDevice(device.get(), reset_token));

        winrt::com_ptr<IMFAttributes> source_attributes = nullptr;
        WI_VERIFY_SUCCEEDED(
            MFCreateAttributes(source_attributes.put(), 2));

        // Set up the source reader to use D3D and produce textures.
        // First we set a D3D Manager for the source reader, this allows it to create D3D textures instead of decoding
        // to CPU memory buffers
        WI_VERIFY_SUCCEEDED(source_attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, (IUnknown*)device_manager.get()));
        // We must enable shared without mutex here, it looks like the MF Mpeg2 decoder both does not respect keyed mutex
        // sharing AND runs on a separate thread.
        WI_VERIFY_SUCCEEDED(source_attributes->SetUINT32(MF_SA_D3D11_SHARED_WITHOUT_MUTEX, 1));

        // We might want these if we wanted to decode to BGRA8 instead of NV12.
        WI_VERIFY_SUCCEEDED(source_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, 1));
        WI_VERIFY_SUCCEEDED(source_attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1));
        WI_VERIFY_SUCCEEDED(source_attributes->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));

        winrt::com_ptr<IMFSourceReader> source_reader = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateSourceReaderFromMediaSource(source.get(), source_attributes.get(), source_reader.put()));

        winrt::com_ptr<IMFMediaType> video_type = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateMediaType(video_type.put()));
        WI_VERIFY_SUCCEEDED(video_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        WI_VERIFY_SUCCEEDED(video_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));

        WI_VERIFY_SUCCEEDED(
            source_reader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr,
                video_type.get()));

        // Set up audio
        winrt::com_ptr<IMFMediaType> source_output_audio_type = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateMediaType(source_output_audio_type.put()));

        WI_VERIFY_SUCCEEDED(source_output_audio_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        WI_VERIFY_SUCCEEDED(source_output_audio_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));

        WI_VERIFY_SUCCEEDED(source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, source_output_audio_type.get()));
        // we grab the type back out of the output so that it populates the other properties that we want
        source_output_audio_type = nullptr;
        WI_VERIFY_SUCCEEDED(source_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, source_output_audio_type.put()));

        // Set up resamplers' output type
        winrt::com_ptr<IMFMediaType> resampler_output_type = nullptr;
        WI_VERIFY_SUCCEEDED(MFCreateMediaType(resampler_output_type.put()));

        WI_VERIFY_SUCCEEDED(resampler_output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_SAMPLE_RATE * 4));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        WI_VERIFY_SUCCEEDED(resampler_output_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));

        // Create our resampler (takes audio in arbitrary formats and converts to AUDIO_SAMPLE_RATEhz PCM)
        winrt::com_ptr<IUnknown> resampler_unknown = nullptr;
        winrt::com_ptr<IMFTransform> resampler = nullptr;
        WI_VERIFY_SUCCEEDED(CoCreateInstance(CLSID_CResamplerMediaObject, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)resampler_unknown.put()));
        WI_VERIFY_SUCCEEDED(resampler_unknown->QueryInterface(IID_PPV_ARGS(resampler.put())));
        winrt::com_ptr<IWMResamplerProps> resampler_props = nullptr;
        WI_VERIFY_SUCCEEDED(resampler_unknown->QueryInterface(IID_PPV_ARGS(resampler_props.put())));
        resampler_props->SetHalfFilterLength(60);

        // Setup the resamplers input and output types to match our source on one end
        // and desktopcapture on the other
        resampler->SetInputType(0, source_output_audio_type.get(), 0);
        resampler->SetOutputType(0, resampler_output_type.get(), 0);

        WI_VERIFY_SUCCEEDED(resampler->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
        WI_VERIFY_SUCCEEDED(resampler->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        WI_VERIFY_SUCCEEDED(resampler->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

        return OpenMediaResult {
            source,
            source_reader,
            resampler,
            device_manager,
        };
    }
} // namespace mf

struct Media
{
    // winrt::com_ptr<IUnknown> unknown_resampler;
    winrt::com_ptr<IMFTransform> resampler;
    winrt::com_ptr<IMFSourceReader> source_reader;

    winrt::com_ptr<IMFDXGIDeviceManager> device_manager;
    HANDLE device_handle;

    LONGLONG cur_time = 0;
    LONGLONG video_timestamp = 0;
    LONGLONG audio_timestamp = 0;

    /// @brief Maybe get a video frame
    /// @param output texture to copy frame into
    /// @return whether a sample was copied
    bool video_frame(winrt::com_ptr<ID3D11Texture2D>& output)
    {
        if (cur_time < video_timestamp)
            return false;

        DWORD flags = 0;
        winrt::com_ptr<IMFSample> sample = nullptr;

        WI_VERIFY_SUCCEEDED(
            source_reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                nullptr,
                &flags,
                &video_timestamp,
                sample.put()));

        // Not sure we have to care about flags...

        if (!sample)
            return false;

        WI_VERIFY_SUCCEEDED(sample->SetSampleTime(video_timestamp));

        // Extract the texture and subresource from the media buffer.
        winrt::com_ptr<IMFMediaBuffer> media_buffer = nullptr;
        winrt::com_ptr<IMFDXGIBuffer> dxgi_buffer = nullptr;
        WI_VERIFY_SUCCEEDED(sample->GetBufferByIndex(0, media_buffer.put()));
        WI_VERIFY_SUCCEEDED(media_buffer->QueryInterface(IID_PPV_ARGS(dxgi_buffer.put())));
        winrt::com_ptr<ID3D11Texture2D> texture;
        uint32_t subresource_index = 0;
        WI_VERIFY_SUCCEEDED(dxgi_buffer->GetResource(IID_PPV_ARGS(texture.put())));
        WI_VERIFY_SUCCEEDED(dxgi_buffer->GetSubresourceIndex(&subresource_index));

        // At this point, we have a texture, and a subresource with which to
        // index into that texture. What we want to do now is to copy this texture out of
        // the array of textures (that MF uses internally) and into our own texture.

        // Check whether either the input texture or the ouput texture
        // are keyed mutex, and accquire accordingly.
        // We know that our input isn't keyed mutexed because we set up media foundation to
        // to use shared withouy keyed mutex.

        D3D11_TEXTURE2D_DESC in_desc = {};
        D3D11_TEXTURE2D_DESC out_desc = {};
        texture->GetDesc(&in_desc);
        output->GetDesc(&out_desc);

        winrt::com_ptr<IDXGIKeyedMutex> input_mutex;
        winrt::com_ptr<IDXGIKeyedMutex> output_mutex;

        if (in_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        {
            WI_VERIFY_SUCCEEDED(texture->QueryInterface(IID_PPV_ARGS(input_mutex.put())));
            WI_VERIFY_SUCCEEDED(input_mutex->AcquireSync(0, INFINITE));
        }
        if (out_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        {
            WI_VERIFY_SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(output_mutex.put())));
            WI_VERIFY_SUCCEEDED(output_mutex->AcquireSync(0, INFINITE));
        }

        winrt::com_ptr<ID3D11Device> device;
        winrt::com_ptr<ID3D11DeviceContext> context;
        texture->GetDevice(device.put());
        device->GetImmediateContext(context.put());

        auto box = D3D11_BOX {0, 0, 0, out_desc.Width, out_desc.Height, 1};
        context->CopySubresourceRegion(output.get(), 0, 0, 0, 0, texture.get(), subresource_index, &box);

        if (input_mutex)
        {
            WI_VERIFY_SUCCEEDED(input_mutex->ReleaseSync(0));
        }
        if (output_mutex)
        {
            WI_VERIFY_SUCCEEDED(output_mutex->ReleaseSync(0));
        }
        return true;
    }

    struct AudioSampleResult
    {
        winrt::com_ptr<IMFSample> sample;
        LONGLONG time;
    };

    /// @brief Get an audio sample
    /// @return An MF sample, and the time it relates to
    AudioSampleResult audio_sample()
    {
        auto result = AudioSampleResult {};

        DWORD flags = 0;
        winrt::com_ptr<IMFSample> sample = nullptr;
        WI_VERIFY_SUCCEEDED(
            source_reader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                nullptr,
                &flags,
                &result.time,
                sample.put()));

        if (sample)
        {
            WI_VERIFY_SUCCEEDED(sample->SetSampleTime(result.time));
        }

        result.sample = sample;

        return result;
    }

    /// @brief Get an audio frame
    /// @param output buffer to copy frame into
    void audio_frame(std::vector<uint8_t>& output)
    {
        if (cur_time < audio_timestamp)
            return;

        // Feed resampler with samples until it will no longer accept data
        while (true)
        {
            DWORD status = 0;
            // Make sure the resampler can take more input before we read and throw away a sample
            resampler->GetInputStatus(0, &status);
            if (status != MFT_INPUT_STATUS_ACCEPT_DATA)
                break;

            auto sample = audio_sample();
            if (sample.sample == nullptr)
                break;

            WI_VERIFY_SUCCEEDED(resampler->ProcessInput(0, sample.sample.get(), 0));
        }

        // Create a media buffer and a sample for the resampler
        // to output data into
        winrt::com_ptr<IMFMediaBuffer> buffer;
        auto output_buffer = MFT_OUTPUT_DATA_BUFFER {};
        memset(&output_buffer, 0, sizeof(MFT_OUTPUT_DATA_BUFFER));
        WI_VERIFY_SUCCEEDED(MFCreateSample(&output_buffer.pSample));
        WI_VERIFY_SUCCEEDED(MFCreateMemoryBuffer(1024, buffer.put()));
        WI_VERIFY_SUCCEEDED(output_buffer.pSample->AddBuffer(buffer.get()));
        output_buffer.dwStreamID = 0;
        output_buffer.pEvents = nullptr;
        output_buffer.dwStatus = 0;

        DWORD status = 0;
        WI_VERIFY_SUCCEEDED(resampler->ProcessOutput(0, 1, &output_buffer, &status));
        auto sample = output_buffer.pSample;

        // Use the resampled output time as the new timestamp
        WI_VERIFY_SUCCEEDED(sample->GetSampleTime(&audio_timestamp));

        winrt::com_ptr<IMFMediaBuffer> media_buffer = nullptr;
        WI_VERIFY_SUCCEEDED(sample->ConvertToContiguousBuffer(media_buffer.put()));
        sample->Release();

        uint8_t* begin = nullptr;
        DWORD len = 0;
        WI_VERIFY_SUCCEEDED(media_buffer->Lock(&begin, nullptr, &len));
        output.resize(len, 0);
        memcpy(output.data(), begin, len);
        media_buffer->Unlock();
    }

    /// @brief Get media frames if they are ready
    /// @param elapsed How much time has elapsed since the last frame
    /// @param output_audio Output buffer for audio
    /// @param output_texture Output texture for video
    /// @param produced_audio Whether a texture has been produced
    void frame(
        LONGLONG elapsed,
        std::vector<uint8_t>& output_audio,
        winrt::com_ptr<ID3D11Texture2D>& output_texture,
        bool& produced_video)
    {
        cur_time += elapsed;
        produced_video = video_frame(output_texture);
        audio_frame(output_audio);
    }
};

#include <chrono>
#include <mutex>

#include <cstdio>

#include <rainwaysdk.h>

using namespace rainway;

static void log_sink(LogLevel level, const char* target, const char* message)
{
    const char* LOG_LEVELS[] = {
        "silent",
        "error",
        "warn",
        "info",
        "debug",
        "trace",
    };
    printf("[RW] (%8s) %s: %s\n", LOG_LEVELS[level], target, message);
}

void on_stream_start(
    rainway::OutboundStream stream,
    std::string media_path)
{
    bool stopped = false;
    stream.SetCloseHandler([&stopped]() { stopped = true; });

    WI_VERIFY_SUCCEEDED(CoInitializeEx(nullptr, COINIT_DISABLE_OLE1DDE));

    auto device = dx::create_device();
    auto result = mf::open_media(device, media_path.c_str());
    mf::debug_media_format(result.source_reader, result.resampler);

    auto media = Media {
        result.resampler,
        result.source_reader,
        result.device_manager,
    };

    auto prev = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> audio = {};
    winrt::com_ptr<ID3D11Texture2D> texture = dx::create_texture(device, 1920, 1080, DXGI_FORMAT_B8G8R8A8_UNORM);

    LONGLONG deadline = 0;

    while (!stopped)
    {
        // This deadline is when we need to get the next samples from the media
        // Since the media is at 30 FPS, we often dont have anything new to submit. This
        // deadline allows us to not spin the thread, and allow other threads to be scheduled
        // whilst we are not doing anything. It also significantly reduces contention on
        // the texture keyed mutexes.
        if (deadline != 0)
        {
            using namespace std::chrono_literals;
            auto wake_point = std::chrono::time_point<std::chrono::high_resolution_clock> {100ns * deadline};
            std::this_thread::sleep_until(wake_point);
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed_nanos = (now - prev).count();
        prev = now;

        audio.resize(0);

        bool produced_video = false;
        media.frame(elapsed_nanos / 100, audio, texture, produced_video);

        if (produced_video && texture)
        {
            stream.SubmitVideo(rainway::VideoBuffer {
                rainway::internal::RAINWAY_OUTBOUND_STREAM_VIDEO_BUFFER_DIRECT_X,
                rainway::internal::RainwayDirectX_Body {texture.get()}});
        }

        deadline = media.video_timestamp;

        if (audio.size() > 0)
        {
            auto audio_buffer = rainway::AudioBuffer {rainway::internal::RAINWAY_AUDIO_BUFFER_PCM, (int16_t*)audio.data()};
            // Convert from byte length to samples
            // 2 bytes per sample, 1 sample per n channels
            auto sample_count = audio.size() / 2 / 2;

            auto submission = rainway::AudioOptions {
                AUDIO_SAMPLE_RATE,
                (uint16_t)2,
                (uint32_t)sample_count,
                audio_buffer};
            stream.SubmitAudio(submission);
        }

        // Use the closest deadline
        deadline = std::min(deadline, media.audio_timestamp);
    }
}

void on_peer_connected(
    rainway::Connection conn,
    rainway::PeerConnection peer,
    std::string media_path)
{
    peer.SetStateChangeHandler(
        rainway::PeerConnection::StateChangeHandler {
            [=](rainway::PeerConnection::State state) {
                if (state == rainway::PeerConnection::State::RAINWAY_PEER_STATE_CONNECTED)
                    printf("Peer connected: %s\n", peer.externalId.c_str());
                else if (state == rainway::PeerConnection::State::RAINWAY_PEER_STATE_FAILED)
                    printf("Peer failed: %s\n", peer.externalId.c_str());
            }});

    peer.SetOutboundStreamRequestHandler(rainway::PeerConnection::OutboundStreamRequestHandler {
        [=](rainway::OutboundStreamRequest req) {
            rainway::OutboundStreamStartOptions config;
            config.defaultPermissions = rainway::InputLevel::RAINWAY_INPUT_LEVEL_ALL;
            config.type = rainway::StreamType::RAINWAY_STREAM_TYPE_BYOFB;

            printf("Accepting stream request: from %s\n", req.Peer().externalId.c_str());

            req.Accept(
                config,
                rainway::OutboundStreamStartCallback {
                    [=](rainway::OutboundStream stream) {
                        // Spawn thread for this stream
                        auto thread = std::thread {
                            [media_path, stream]() {
                                on_stream_start(stream, media_path);
                            },
                        };

                        thread.detach();
                    },
                    // on failure
                    [](rainway::Error err) {
                        printf("Failed to accept stream: %d", err);
                    }});
        }});
}

int main(int argc, const char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s <api_key> <path to media>\n", argv[0]);
        exit(1);
    }

    const auto api_key = argv[1];
    const auto media_path = std::string(argv[2]);

    auto hr = rainway::Initialize();
    if (hr != rainway::Error::RAINWAY_ERROR_SUCCESS)
    {
        printf("Error. Failed to initialize Rainway: %d\n", hr);
        return 1;
    }

    // install the log handlers
    rainway::SetLogLevel(rainway::LogLevel::RAINWAY_LOG_LEVEL_DEBUG, nullptr);
    rainway::SetLogSink(log_sink);

    rainway::Connection::CreateOptions config;
    config.apiKey = api_key;
    config.externalId = "video-player";

    rainway::Connection::Create(
        config,
        rainway::Connection::CreatedCallback {
            [=](rainway::Connection conn) {
                printf("Connected to rainway network as %llu\n", conn.Id());

                conn.SetPeerConnectionRequestHandler(rainway::Connection::PeerConnectionRequestHandler {
                    [=](rainway::IncomingConnectionRequest req) {
                        printf("Accepting connection: %lld from %s\n", req.id, req.externalId.c_str());

                        req.Accept(
                            rainway::PeerOptions {},
                            rainway::IncomingConnectionRequest::AcceptCallback {
                                // on success
                                [=](rainway::PeerConnection peer) {
                                    on_peer_connected(conn, peer, media_path);
                                },
                                // on failure
                                [](rainway::Error err) {
                                    printf("Failed to accept connection: %d", err);
                                }});
                    }});
            },
            // on failure
            [](rainway::Error err) {
                printf("Failed to connect to rainway: %d", err);
            }});

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10000h);
}
