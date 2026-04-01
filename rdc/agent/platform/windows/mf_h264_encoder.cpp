/**
 * @file mf_h264_encoder.cpp
 * @brief 实现 agent/platform/windows/mf_h264_encoder 相关的类型、函数与流程。
 */

#include "mf_h264_encoder.hpp"

#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <propvarutil.h>

#include <combaseapi.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace rdc::agent::platform::windows {

namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] std::runtime_error MakeError(const std::string& message, const HRESULT hr) {
    return std::runtime_error(message + " (hr=0x" + std::to_string(static_cast<unsigned long>(hr)) + ")");
/**
 * @brief 执行 CheckHr 相关处理。
 * @param hr hr。
 * @param message 待处理的消息对象。
 */
}

void CheckHr(const HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw MakeError(message, hr);
    }
}

/**
 * @brief 创建样本从字节。
 * @param bytes 输入字节缓冲区。
 * @param sample_time_hns sampletimehns。
 * @param sample_duration_hns 样本时长，单位为 100ns。
 * @return 返回对象指针或句柄。
 */
ComPtr<IMFSample> CreateSampleFromBytes(const std::vector<std::uint8_t>& bytes,
                                        const std::int64_t sample_time_hns,
                                        const std::int64_t sample_duration_hns) {
    ComPtr<IMFMediaBuffer> buffer;
    CheckHr(MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()), &buffer), "MFCreateMemoryBuffer failed");

    std::uint8_t* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    CheckHr(buffer->Lock(&data, &max_length, &current_length), "IMFMediaBuffer::Lock failed");
    (void)max_length;
    (void)current_length;
    std::memcpy(data, bytes.data(), bytes.size());
    CheckHr(buffer->Unlock(), "IMFMediaBuffer::Unlock failed");
    CheckHr(buffer->SetCurrentLength(static_cast<DWORD>(bytes.size())), "IMFMediaBuffer::SetCurrentLength failed");

    ComPtr<IMFSample> sample;
    CheckHr(MFCreateSample(&sample), "MFCreateSample failed");
    CheckHr(sample->AddBuffer(buffer.Get()), "IMFSample::AddBuffer failed");
    CheckHr(sample->SetSampleTime(sample_time_hns), "IMFSample::SetSampleTime failed");
    CheckHr(sample->SetSampleDuration(sample_duration_hns), "IMFSample::SetSampleDuration failed");
    return sample;
}

/**
 * @brief 复制样本字节。
 * @param sample 样本对象。
 * @return 返回结果集合。
 */
std::vector<std::uint8_t> CopySampleBytes(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> contiguous_buffer;
    CheckHr(sample->ConvertToContiguousBuffer(&contiguous_buffer), "IMFSample::ConvertToContiguousBuffer failed");

    std::uint8_t* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    CheckHr(contiguous_buffer->Lock(&data, &max_length, &current_length), "IMFMediaBuffer::Lock failed");
    (void)max_length;
    std::vector<std::uint8_t> bytes(data, data + current_length);
    CheckHr(contiguous_buffer->Unlock(), "IMFMediaBuffer::Unlock failed");
    return bytes;
}

/**
 * @brief 创建输出样本。
 * @param output_size outputsize。
 * @return 返回对象指针或句柄。
 */
ComPtr<IMFSample> CreateOutputSample(const DWORD output_size) {
    ComPtr<IMFMediaBuffer> output_buffer;
    CheckHr(MFCreateMemoryBuffer(output_size, &output_buffer), "MFCreateMemoryBuffer(output) failed");

    ComPtr<IMFSample> output_sample;
    CheckHr(MFCreateSample(&output_sample), "MFCreateSample(output) failed");
    CheckHr(output_sample->AddBuffer(output_buffer.Get()), "IMFSample::AddBuffer(output) failed");
    return output_sample;
}

/**
 * @brief 构造 MfH264Encoder 对象。
 */
}  // namespace

MfH264Encoder::MfH264Encoder(H264EncoderConfig config)
    : config_(config),
      frame_duration_hns_(ComputeFrameDurationHns(config.fps_num, config.fps_den)) {
    Initialize();
}

/**
 * @brief 析构 MfH264Encoder 对象并释放相关资源。
 */
MfH264Encoder::~MfH264Encoder() {
    if (transform_ != nullptr) {
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    }

    if (mf_started_) {
        MFShutdown();
    }

    if (com_initialized_) {
        CoUninitialize();
    }
}

/**
 * @brief 编码相关流程。
 * @param frame 视频帧对象。
 * @return 返回结果集合。
 */
std::vector<encoder::EncodedVideoFrame> MfH264Encoder::Encode(const encoder::Nv12VideoFrame& frame) {
    if (frame.width != config_.width || frame.height != config_.height) {
        throw std::runtime_error("NV12 frame dimensions do not match encoder configuration");
    }

    const auto sample = CreateSampleFromBytes(frame.bytes, next_sample_time_hns_, frame_duration_hns_);
    next_sample_time_hns_ += frame_duration_hns_;

    HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        auto frames = PullAvailableOutput();
        hr = transform_->ProcessInput(0, sample.Get(), 0);
        if (FAILED(hr)) {
            throw MakeError("IMFTransform::ProcessInput failed after draining output", hr);
        }
        auto more_frames = PullAvailableOutput();
        frames.insert(frames.end(),
                      std::make_move_iterator(more_frames.begin()),
                      std::make_move_iterator(more_frames.end()));
        return frames;
    }

    CheckHr(hr, "IMFTransform::ProcessInput failed");
    return PullAvailableOutput();
}

/**
 * @brief 输出相关流程。
 * @return 返回结果集合。
 */
std::vector<encoder::EncodedVideoFrame> MfH264Encoder::Drain() {
    CheckHr(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0),
            "MFT_MESSAGE_NOTIFY_END_OF_STREAM failed");
    CheckHr(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0), "MFT_MESSAGE_COMMAND_DRAIN failed");
    return PullAvailableOutput();
}

/**
 * @brief 初始化相关流程。
 */
void MfH264Encoder::Initialize() {
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (com_hr == S_OK || com_hr == S_FALSE) {
        com_initialized_ = true;
    } else if (com_hr != RPC_E_CHANGED_MODE) {
        throw MakeError("CoInitializeEx failed", com_hr);
    }

    CheckHr(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET), "MFStartup failed");
    mf_started_ = true;

    transform_ = CreateEncoderTransform();
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(transform_->GetAttributes(&attributes)) && attributes != nullptr) {
        attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    }
    ConfigureCodecApi();
    SetMediaTypes();
    StartStreaming();
}

/**
 * @brief 配置CodecApi。
 */
void MfH264Encoder::ConfigureCodecApi() {
    codec_api_.Reset();
    const HRESULT query_hr = transform_.As(&codec_api_);
    if (FAILED(query_hr) || codec_api_ == nullptr) {
        return;
    }

    VARIANT value;
    VariantInit(&value);

    value.vt = VT_UI4;
    value.ulVal = eAVEncCommonRateControlMode_CBR;
    codec_api_->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);

    value.ulVal = config_.gop_size;
    codec_api_->SetValue(&CODECAPI_AVEncMPVGOPSize, &value);

    VariantClear(&value);
}

void MfH264Encoder::RequestKeyframe() {
    if (codec_api_ == nullptr) {
        return;
    }

    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = 1;
    codec_api_->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value);
    VariantClear(&value);
}

/**
 * @brief 设置MediaTypes。
 */
void MfH264Encoder::SetMediaTypes() {
    ComPtr<IMFMediaType> output_type;
    CheckHr(MFCreateMediaType(&output_type), "MFCreateMediaType(output) failed");
    CheckHr(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set output major type failed");
    CheckHr(output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Set output subtype failed");
    CheckHr(output_type->SetUINT32(MF_MT_AVG_BITRATE, config_.bitrate), "Set output bitrate failed");
    CheckHr(output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set output interlace mode failed");
    CheckHr(MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height), "Set output frame size failed");
    CheckHr(MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, config_.fps_num, config_.fps_den), "Set output frame rate failed");
    CheckHr(MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set output pixel aspect ratio failed");
    CheckHr(transform_->SetOutputType(0, output_type.Get(), 0), "IMFTransform::SetOutputType failed");

    ComPtr<IMFMediaType> input_type;
    CheckHr(MFCreateMediaType(&input_type), "MFCreateMediaType(input) failed");
    CheckHr(input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set input major type failed");
    CheckHr(input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "Set input subtype failed");
    CheckHr(input_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set input interlace mode failed");
    CheckHr(MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height), "Set input frame size failed");
    CheckHr(MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, config_.fps_num, config_.fps_den), "Set input frame rate failed");
    CheckHr(MFSetAttributeRatio(input_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set input pixel aspect ratio failed");
    CheckHr(transform_->SetInputType(0, input_type.Get(), 0), "IMFTransform::SetInputType failed");
}

/**
 * @brief 启动Streaming。
 */
void MfH264Encoder::StartStreaming() {
    if (stream_started_) {
        return;
    }

    const HRESULT flush_hr = transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    if (FAILED(flush_hr) && flush_hr != E_NOTIMPL && flush_hr != E_FAIL) {
        throw MakeError("MFT_MESSAGE_COMMAND_FLUSH failed", flush_hr);
    }
    CheckHr(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
            "MFT_MESSAGE_NOTIFY_BEGIN_STREAMING failed");
    CheckHr(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
            "MFT_MESSAGE_NOTIFY_START_OF_STREAM failed");
    stream_started_ = true;
}

/**
 * @brief 创建编码器Transform。
 * @return 返回对象指针或句柄。
 */
ComPtr<IMFTransform> MfH264Encoder::CreateEncoderTransform() const {
    MFT_REGISTER_TYPE_INFO input_type_info{};
    input_type_info.guidMajorType = MFMediaType_Video;
    input_type_info.guidSubtype = MFVideoFormat_NV12;

    MFT_REGISTER_TYPE_INFO output_type_info{};
    output_type_info.guidMajorType = MFMediaType_Video;
    output_type_info.guidSubtype = MFVideoFormat_H264;

    IMFActivate** activates = nullptr;
    UINT32 activate_count = 0;
    HRESULT enum_hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                                MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                                &input_type_info,
                                &output_type_info,
                                &activates,
                                &activate_count);

    if (FAILED(enum_hr) || activate_count == 0 || activates == nullptr) {
        if (activates != nullptr) {
            CoTaskMemFree(activates);
            activates = nullptr;
        }

        activate_count = 0;
        enum_hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                            &input_type_info,
                            &output_type_info,
                            &activates,
                            &activate_count);
    }

    if (FAILED(enum_hr) || activate_count == 0 || activates == nullptr) {
        if (activates != nullptr) {
            CoTaskMemFree(activates);
            activates = nullptr;
        }

        activate_count = 0;
        CheckHr(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                          MFT_ENUM_FLAG_ALL,
                          &input_type_info,
                          &output_type_info,
                          &activates,
                          &activate_count),
                "MFTEnumEx(video encoder) failed");
    }

    if (activate_count == 0 || activates == nullptr) {
        throw std::runtime_error("No Media Foundation H.264 encoder MFT available");
    }

    ComPtr<IMFTransform> transform;
    HRESULT activate_hr = E_FAIL;
    for (UINT32 i = 0; i < activate_count; ++i) {
        if (activates[i] == nullptr) {
            continue;
        }

        activate_hr = activates[i]->ActivateObject(IID_PPV_ARGS(&transform));
        activates[i]->Release();
        if (SUCCEEDED(activate_hr) && transform != nullptr) {
            break;
        }
    }

    CoTaskMemFree(activates);

    if (FAILED(activate_hr) || transform == nullptr) {
        throw MakeError("Failed to activate Media Foundation H.264 encoder", activate_hr);
    }

    return transform;
}

/**
 * @brief 执行 PullAvailable输出 相关处理。
 * @return 返回结果集合。
 */
std::vector<encoder::EncodedVideoFrame> MfH264Encoder::PullAvailableOutput() {
    std::vector<encoder::EncodedVideoFrame> frames;

    while (true) {
        MFT_OUTPUT_STREAM_INFO output_stream_info{};
        CheckHr(transform_->GetOutputStreamInfo(0, &output_stream_info), "GetOutputStreamInfo failed");

        ComPtr<IMFSample> output_sample;
        const bool transform_provides_samples =
            (output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0 ||
            (output_stream_info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) != 0;

        MFT_OUTPUT_DATA_BUFFER output_data{};
        output_data.dwStreamID = 0;
        DWORD status = 0;
        HRESULT hr = S_OK;

        if (!transform_provides_samples) {
            output_sample = CreateOutputSample(output_stream_info.cbSize);
            output_data.pSample = output_sample.Get();
            hr = transform_->ProcessOutput(0, 1, &output_data, &status);
        } else {
            output_data.pSample = nullptr;
            hr = transform_->ProcessOutput(0, 1, &output_data, &status);
            if (hr == E_UNEXPECTED && output_stream_info.cbSize > 0) {
                output_sample = CreateOutputSample(output_stream_info.cbSize);
                output_data.pSample = output_sample.Get();
                output_data.pEvents = nullptr;
                status = 0;
                hr = transform_->ProcessOutput(0, 1, &output_data, &status);
            }
        }

        (void)status;

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break;
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            throw std::runtime_error("Media Foundation H.264 encoder requested unexpected stream change");
        }

        CheckHr(hr, "IMFTransform::ProcessOutput failed");

        IMFSample* produced_sample = output_data.pSample;
        if (produced_sample == nullptr) {
            produced_sample = output_sample.Get();
        }

        if (produced_sample != nullptr) {
            encoder::EncodedVideoFrame frame;
            frame.bytes = CopySampleBytes(produced_sample);
            frame.sample_time_hns = 0;
            frame.sample_duration_hns = 0;
            produced_sample->GetSampleTime(&frame.sample_time_hns);
            produced_sample->GetSampleDuration(&frame.sample_duration_hns);

            UINT32 clean_point = FALSE;
            if (SUCCEEDED(produced_sample->GetUINT32(MFSampleExtension_CleanPoint, &clean_point))) {
                frame.is_key_frame = clean_point != FALSE;
            }

            frames.push_back(std::move(frame));
        }

        if (output_data.pEvents != nullptr) {
            output_data.pEvents->Release();
        }
    }

    return frames;
}

/**
 * @brief 计算帧DurationHns。
 * @param fps_num 帧率分子。
 * @param fps_den 帧率分母。
 * @return 返回计算得到的数值结果。
 */
std::int64_t MfH264Encoder::ComputeFrameDurationHns(const std::uint32_t fps_num, const std::uint32_t fps_den) {
    if (fps_num == 0 || fps_den == 0) {
        throw std::runtime_error("Invalid H.264 encoder frame rate");
    }

    return (10'000'000LL * fps_den) / fps_num;
}

}  // namespace rdc::agent::platform::windows
