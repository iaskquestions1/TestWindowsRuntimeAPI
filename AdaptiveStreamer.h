#pragma once
#include "pch.h"
#include <string>

#define USE_AUDIOGRAPH // comment out to not use the audiograph and just let the media player play the audio
#define AUDIOGRAPH_SOUND_CARD_OUTPUT // output to the soundcard vs to a frame node
#define ONE_SINGLE_MEDIASOURCE // this causes the video callbacks not to be called and OnFailed to report a problem
//#define WAV_FILE_INPUT_NODE // comment out to use the HLS stream audio

enum class StateType : UINT32
{
    StateType_None = 0,
    StateType_Opened,
    StateType_StateChanged,
    StateType_Failed,
    StateType_NewFrameTexture,
    StateType_GraphicsDeviceShutdown,
    StateType_GraphicsDeviceReady
};

enum class PlaybackState : UINT32
{
    PlaybackState_None = 0,
    PlaybackState_Opening,
    PlaybackState_Buffering,
    PlaybackState_Playing,
    PlaybackState_Paused,
    PlaybackState_Ended,
    PlaybackState_NA = 255
};

#pragma pack(push, 8)
using MEDIA_DESCRIPTION = struct _MEDIA_DESCRIPTION
{
    UINT32 width;
    UINT32 height;
    INT64 duration;
    byte canSeek;
    byte isStereoscopic;
};
#pragma pack(pop)

#pragma pack(push, 8)
using PLAYBACK_STATE = struct _PLAYBACK_STATE
{
    StateType type;
    PlaybackState state;
    HRESULT hresult;
    MEDIA_DESCRIPTION description;
};
#pragma pack(pop)

using SUBTITLE_TRACK = struct _SUBTITLE_TRACK
{
    std::wstring id;
    std::wstring title;
    std::wstring language;
};

using IMediaPlayerEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Playback::MediaPlayer*, IInspectable*>;
using IFailedEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Playback::MediaPlayer*, ABI::Windows::Media::Playback::MediaPlayerFailedEventArgs*>;
using IMediaPlaybackSessionEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Playback::MediaPlaybackSession*, IInspectable*>;
using IDownloadRequestedEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Streaming::Adaptive::AdaptiveMediaSource*,
    ABI::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceDownloadRequestedEventArgs*>;
using ITracksChangedEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Playback::MediaPlaybackItem*, ABI::Windows::Foundation::Collections::IVectorChangedEventArgs*>;
using IMediaCueEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Core::TimedMetadataTrack*, ABI::Windows::Media::Core::MediaCueEventArgs*>;
using IQuantumStartedEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Audio::AudioGraph*, IInspectable*>;
using IAudioGraphUnrecoverableErrorEventHandler = ABI::Windows::Foundation::ITypedEventHandler<
    ABI::Windows::Media::Audio::AudioGraph*, ABI::Windows::Media::Audio::AudioGraphUnrecoverableErrorOccurredEventArgs*>;

class AdaptiveStreamer
{
public:
    AdaptiveStreamer();
    ~AdaptiveStreamer();
	HRESULT Initialize();
	HRESULT LoadContent(const std::wstring& sURL);
	HRESULT Play();
    HRESULT Pause();
    HRESULT Stop();

private:
    // Callbacks - IMediaPlayer2
    HRESULT OnFailed(_In_ ABI::Windows::Media::Playback::IMediaPlayer* sender, _In_ ABI::Windows::Media::Playback::IMediaPlayerFailedEventArgs* args);

    // Callbacks - IMediaPlayer5 - frameserver
    HRESULT OnVideoFrameAvailable(_In_ ABI::Windows::Media::Playback::IMediaPlayer* sender, _In_ IInspectable* args);

    // Callbacks - IMediaPlaybackSession
    HRESULT OnStateChanged(_In_ ABI::Windows::Media::Playback::IMediaPlaybackSession* sender, _In_ IInspectable* args);
    HRESULT OnSizeChanged(_In_ ABI::Windows::Media::Playback::IMediaPlaybackSession* sender, _In_ IInspectable* args);

    HRESULT OnAudioGraphQuantumStarted(_In_ ABI::Windows::Media::Audio::IAudioGraph* sender, _In_ IInspectable* args);
    
    HRESULT CreateMediaPlayer();
    void ReleaseMediaPlayer();
    HRESULT AddStateChanged();
    void RemoveStateChanged();
    HRESULT CreateAudioGraph();
    HRESULT ReleaseAudioGraph();
    HRESULT PlayAudioGraph();

    HRESULT CreatePlaybackTextures();
    void ReleaseTextures();
    HRESULT CreateAudioGraphNodes(_In_ ABI::Windows::Media::Core::IMediaSource2* pSource);

	Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
	Microsoft::WRL::ComPtr<ID3D11Device> m_mediaDevice;
	Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_spDeviceManager;
	Microsoft::WRL::ComPtr<ABI::Windows::Media::Playback::IMediaPlayer> m_mediaPlayer;
	Microsoft::WRL::ComPtr<ABI::Windows::Media::Playback::IMediaPlayer3> m_mediaPlayer3;
	Microsoft::WRL::ComPtr<ABI::Windows::Media::Playback::IMediaPlayer5> m_mediaPlayer5;

    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IAudioGraphSettings> m_audioGraphSettings;
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IAudioGraph> m_audioGraph;
#ifdef WAV_FILE_INPUT_NODE
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IAudioFileInputNode> m_audioInNode;
#else
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IMediaSourceAudioInputNode> m_audioInNode;
#endif

#ifdef AUDIOGRAPH_SOUND_CARD_OUTPUT
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IAudioDeviceOutputNode> m_audioOutNode;
#else
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Audio::IAudioFrameOutputNode> m_audioOutNode;
#endif

	Microsoft::WRL::ComPtr<ABI::Windows::Media::Playback::IMediaPlaybackSession> m_mediaPlaybackSession;
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Streaming::Adaptive::IAdaptiveMediaSource> m_spAdaptiveMediaSource;
    Microsoft::WRL::ComPtr<ABI::Windows::Media::Playback::IMediaPlaybackItem> m_spPlaybackItem;

    EventRegistrationToken m_stateChangedEventToken;
    EventRegistrationToken m_sizeChangedEventToken;
    EventRegistrationToken m_durationChangedEventToken;

    CD3D11_TEXTURE2D_DESC m_textureDesc;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_primaryTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_primaryTextureSRV;
    
    HANDLE m_primarySharedHandle;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_primaryMediaTexture;
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface> m_primaryMediaSurface;
    
    EventRegistrationToken m_failedEventToken;
    EventRegistrationToken m_videoFrameAvailableToken;
    EventRegistrationToken m_quantumStartedEventToken;

    bool m_bIgnoreEvents;
    bool m_readyForFrames;
    bool m_createTextures;

    UINT32 m_audioBitrate; // bps
    UINT32 m_audioBitsPerSamples; // 32 bits
    UINT32 m_audioChannelCount; // stereo or other
    UINT32 m_audioSamplingRate; // typically 44.1kHz or 48kHz

    static bool m_deviceNotReady;
    std::vector<SUBTITLE_TRACK> m_subtitleTracks;
};

