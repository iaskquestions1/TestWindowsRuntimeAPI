#pragma once
#include "pch.h"
#include <string>

using namespace Windows::Foundation;
using namespace Microsoft::WRL;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace ABI::Windows::Media;
using namespace Core;
using namespace Playback;
using namespace Audio;
using namespace Effects;
using namespace ABI::Windows::Storage::Streams;
using namespace ABI::Windows::Media::Streaming::Adaptive;
using namespace ABI::Windows::Foundation;
using namespace Microsoft::WRL::Wrappers;

class SimpleAdaptiveStreamer
{
public:
    SimpleAdaptiveStreamer();
    ~SimpleAdaptiveStreamer();
	HRESULT Initialize();
	HRESULT LoadContent(const std::wstring& sURL);
	HRESULT Play();
    HRESULT Pause();
    HRESULT Stop();
    
private:
    // Callbacks - IMediaPlayer2
    HRESULT OnFailed(_In_ IMediaPlayer* sender, _In_ IMediaPlayerFailedEventArgs* args);
    // Callbacks - IMediaPlayer5 - frameserver
    HRESULT OnVideoFrameAvailable(_In_ IMediaPlayer* sender, _In_ IInspectable* args);

    HRESULT OnAudioGraphQuantumStarted(_In_ IAudioGraph* sender, _In_ IInspectable* args);

    // For adaptive streaming
    ComPtr<IAdaptiveMediaSource> m_spAdaptiveMediaSource;
    ComPtr<IMediaPlayer> m_mediaPlayer;
    ComPtr<IMediaPlaybackItem> m_spPlaybackItem;

    // For the Audio Graph
    HRESULT CreateAudioGraphNodes(_In_ IMediaSource2* pSource);
    HRESULT CreateAudioGraph();
    ComPtr<IAudioGraphSettings> m_audioGraphSettings;
    ComPtr<IAudioGraph> m_audioGraph;
    ComPtr<IMediaSourceAudioInputNode> m_audioInNode;
    ComPtr<IAudioDeviceOutputNode> m_audioOutNode;
    UINT32 m_audioBitrate; // bps
    UINT32 m_audioBitsPerSamples; // 32 bits
    UINT32 m_audioChannelCount; // stereo or other
    UINT32 m_audioSamplingRate; // typically 44.1kHz or 48kHz

    HRESULT CreateMediaPlaybackItem(
        _In_ ABI::Windows::Media::Core::IMediaSource2* pMediaSource,
        _COM_Outptr_ ABI::Windows::Media::Playback::IMediaPlaybackItem** ppMediaPlaybackItem);
    
    void CreateAdaptiveMediaSourceFromUri(
        _In_ PCWSTR szManifestUri,
        _Outptr_opt_ IAdaptiveMediaSource** ppAdaptiveMediaSource,
        _Outptr_opt_ IAdaptiveMediaSourceCreationResult** ppCreationResult);

    HRESULT CreateMediaSource(
        _In_ LPCWSTR pszUrl,
        _COM_Outptr_ ABI::Windows::Media::Streaming::Adaptive::IAdaptiveMediaSource** ppMediaSource);

    HRESULT CreateAudioGraphSettings(
        _COM_Outptr_ ABI::Windows::Media::Audio::IAudioGraphSettings** ppAudioGraphSettings);

    HRESULT CreateAudioGraphFromSettings(
        _COM_Outptr_ ABI::Windows::Media::Audio::IAudioGraph** ppAudioGraph,
        _In_ ABI::Windows::Media::Audio::IAudioGraphSettings* pSettings,
        _Outptr_opt_ ABI::Windows::Media::Audio::ICreateAudioGraphResult** ppResult);

    HRESULT CreateInputNode(_In_ ABI::Windows::Media::Audio::IAudioGraph3* pAudioGraph,
        _In_ ABI::Windows::Media::Core::IMediaSource2* pSource,
        _COM_Outptr_ ABI::Windows::Media::Audio::IMediaSourceAudioInputNode** pp,
        _Outptr_opt_ ABI::Windows::Media::Audio::ICreateMediaSourceAudioInputNodeResult** ppResult);

    HRESULT CreateOutputNode(_In_ ABI::Windows::Media::Audio::IAudioGraph* pAudioGraph,
        _COM_Outptr_ ABI::Windows::Media::Audio::IAudioDeviceOutputNode** pp);
};

