#include "AdaptiveStreamer.h"

#include <iostream>

#include "MediaHelpers.h"

using namespace Windows::Foundation;
using namespace Microsoft::WRL;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace ABI::Windows::Media;
using namespace Core;
using namespace Playback;
using namespace Audio;
using namespace Effects;
using namespace ABI::Windows::Storage::Streams;

bool AdaptiveStreamer::m_deviceNotReady = true;

AdaptiveStreamer::AdaptiveStreamer() :
    m_d3dDevice(nullptr)
    , m_mediaDevice(nullptr)
    , m_bIgnoreEvents(false)
    , m_primarySharedHandle(INVALID_HANDLE_VALUE)
    , m_readyForFrames(false)
    , m_createTextures(false)
{
}

AdaptiveStreamer::~AdaptiveStreamer()
{
#ifdef USE_AUDIOGRAPH
    m_audioGraph->Stop();
#endif
}

HRESULT AdaptiveStreamer::Initialize()
{
    CreateMediaPlayer();
    return S_OK;
}

HRESULT AdaptiveStreamer::LoadContent(const std::wstring& sURL)
{
    Log(Log_Level_Info, L"AdaptiveStreamer::LoadContent()");

    if (m_mediaPlayer.Get() == nullptr)
    {
        return E_UNEXPECTED;
    }

    // Check if MediaPlayer now has a source (Stop was not called). 
    // If so, call stop. It will recreate and reinitialize MediaPlayer (m_mediaPlayer) 
    ComPtr<IMediaPlayerSource2> spPlayerAsMediaPlayerSource;
    ComPtr<IMediaPlaybackSource> spCurrentSource;
    IFR(m_mediaPlayer.As(&spPlayerAsMediaPlayerSource));
    spPlayerAsMediaPlayerSource->get_Source(&spCurrentSource);

    if (spCurrentSource.Get())
    {
        IFR(Stop());
    }

    m_subtitleTracks.clear();

    // create the media source for content (fromUri)
    ComPtr<IMediaSource2> spMediaSource2;
    IFR(CreateMediaSource(sURL.c_str(), &spMediaSource2));
    ComPtr<IMediaSource4> spMediaSource4;
    spMediaSource2.As(&spMediaSource4);
    if (spMediaSource4.Get() != nullptr)
    {
        assert(m_spAdaptiveMediaSource.Get() == nullptr);
        spMediaSource4->get_AdaptiveMediaSource(m_spAdaptiveMediaSource.ReleaseAndGetAddressOf());
    }

#ifdef USE_AUDIOGRAPH
    #ifdef ONE_SINGLE_MEDIASOURCE
        CreateAudioGraphNodes(spMediaSource2.Get());
    #else
        // Mute the Media Player as the sound will be played via the audio graph
        m_mediaPlayer.Get()->put_Volume(0.0);
        // create the media source for the audiograph
        ComPtr<IMediaSource2> spMediaSourceForAudioGraph;
        IFR(CreateMediaSource(sURL.c_str(), &spMediaSourceForAudioGraph));
        CreateAudioGraphNodes(spMediaSourceForAudioGraph.Get());
    #endif
#endif

    IFR(CreateMediaPlaybackItem(spMediaSource2.Get(), m_spPlaybackItem.ReleaseAndGetAddressOf()));
        
    ComPtr<IMediaPlaybackSource> spMediaPlaybackSource;
    IFR(m_spPlaybackItem.As(&spMediaPlaybackSource));

    IFR(spPlayerAsMediaPlayerSource->put_Source(spMediaPlaybackSource.Get()));

    return S_OK;
}

HRESULT AdaptiveStreamer::Play()
{
    Log(Log_Level_Info, L"AdaptiveStreamer::Play()");

    if (nullptr != m_mediaPlayer)
    {
        IFR(m_mediaPlayer->Play());
#ifdef USE_AUDIOGRAPH
        IFR(PlayAudioGraph());
#endif
        return S_OK;
    }
    return E_ILLEGAL_METHOD_CALL;
}

HRESULT AdaptiveStreamer::PlayAudioGraph()
{
    ComPtr<IAudioNode> spNode;
    m_audioInNode.As(&spNode);
    spNode->Start();
    m_audioOutNode.As(&spNode);
    spNode->Start();
    m_audioGraph->Start();
    return S_OK;
}

HRESULT AdaptiveStreamer::Pause()
{
    Log(Log_Level_Info, L"AdaptiveStreamer::Pause()");

    if (nullptr != m_mediaPlayer)
    {
        IFR(m_mediaPlayer->Pause());
        return S_OK;
    }
    return E_ILLEGAL_METHOD_CALL;
}

HRESULT AdaptiveStreamer::Stop()
{
    Log(Log_Level_Info, L"AdaptiveStreamer::Stop()");

    bool fireStateChange = false;
    m_bIgnoreEvents = true;

    if (nullptr != m_mediaPlayer)
    {
        fireStateChange = true;

        ComPtr<IMediaPlayerSource2> spMediaPlayerSource;
        m_mediaPlayer.As(&spMediaPlayerSource);

        if (spMediaPlayerSource != nullptr)
        {
            spMediaPlayerSource->put_Source(nullptr);
        }

        if (m_spAdaptiveMediaSource.Get() != nullptr)
        {
            m_spAdaptiveMediaSource.Reset();
            m_spAdaptiveMediaSource = nullptr;
        }

        if (m_spPlaybackItem != nullptr)
        {
            m_spPlaybackItem.Reset();
            m_spPlaybackItem = nullptr;
        }
    }

    m_subtitleTracks.clear();

    ReleaseMediaPlayer();

    HRESULT hr = CreateMediaPlayer();

    if (fireStateChange)
    {
        PLAYBACK_STATE playbackState;
        ZeroMemory(&playbackState, sizeof(playbackState));
        playbackState.type = StateType::StateType_None;
        playbackState.state = PlaybackState::PlaybackState_None;
    }

    m_bIgnoreEvents = false;

    return hr;
}

HRESULT AdaptiveStreamer::CreateMediaPlayer()
{
    // create media player
    ComPtr<IMediaPlayer> spMediaPlayer;
    IFR(ActivateInstance(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Playback_MediaPlayer).Get(),
        &spMediaPlayer));

    spMediaPlayer->put_AutoPlay(false);

    // setup callbacks
    EventRegistrationToken failedEventToken;
    auto mediaFailed = Microsoft::WRL::Callback<IFailedEventHandler>(this, &AdaptiveStreamer::OnFailed);
    IFR(spMediaPlayer->add_MediaFailed(mediaFailed.Get(), &failedEventToken));

    // frameserver mode is on the IMediaPlayer5 interface
    ComPtr<IMediaPlayer5> spMediaPlayer5;
    IFR(spMediaPlayer.As(&spMediaPlayer5));

    // set frameserver mode
    IFR(spMediaPlayer5->put_IsVideoFrameServerEnabled(true));

    // register for frame available callback
    EventRegistrationToken videoFrameAvailableToken;
    auto videoFrameAvailableCallback = Microsoft::WRL::Callback<IMediaPlayerEventHandler>(this, &AdaptiveStreamer::OnVideoFrameAvailable);
    IFR(spMediaPlayer5->add_VideoFrameAvailable(videoFrameAvailableCallback.Get(), &videoFrameAvailableToken));

    // store the player and token 
    m_mediaPlayer = nullptr;

    m_mediaPlayer.Attach(spMediaPlayer.Detach());
    m_failedEventToken = failedEventToken;
    m_videoFrameAvailableToken = videoFrameAvailableToken;

    ComPtr<IMediaPlayer3> spMediaPlayer3;
    IFR(m_mediaPlayer.As(&spMediaPlayer3));
    m_mediaPlayer3.Attach(spMediaPlayer3.Detach());

    m_mediaPlayer5.Attach(spMediaPlayer5.Detach());

    ComPtr<IMediaPlaybackSession> spSession;
    IFR(m_mediaPlayer3->get_PlaybackSession(&spSession));
    m_mediaPlaybackSession.Attach(spSession.Detach());

    IFR(AddStateChanged());

#ifdef USE_AUDIOGRAPH
    IFR(CreateAudioGraph());
#endif
    
    return S_OK;
}

HRESULT AdaptiveStreamer::CreateAudioGraph()
{
    // Create the audio graph
    ComPtr<IAudioGraphSettings> spAudioGraphSettings;
    IFR(CreateAudioGraphSettings(&spAudioGraphSettings));

    ComPtr<IAudioGraph> spAudioGraph;
    IFR(CreateAudioGraphFromSettings(&spAudioGraph, spAudioGraphSettings.Get(), nullptr));

    INT32 quantumSize;
    IFR(spAudioGraph->get_SamplesPerQuantum(&quantumSize));

    // Add event handler to the audio graph
    EventRegistrationToken quantumStartedToken;
    auto quantumStarted = Microsoft::WRL::Callback<IQuantumStartedEventHandler>(this, &AdaptiveStreamer::OnAudioGraphQuantumStarted);
    IFR(spAudioGraph->add_QuantumStarted(quantumStarted.Get(), &quantumStartedToken));
    m_quantumStartedEventToken = quantumStartedToken;
    
    // Get the audio encoding specs
    ComPtr<MediaProperties::IAudioEncodingProperties> spEncodingProperties;
    IFR(spAudioGraph->get_EncodingProperties(&spEncodingProperties));
    IFR(spEncodingProperties->get_Bitrate(&m_audioBitrate));
    IFR(spEncodingProperties->get_BitsPerSample(&m_audioBitsPerSamples));
    IFR(spEncodingProperties->get_ChannelCount(&m_audioChannelCount));
    IFR(spEncodingProperties->get_SampleRate(&m_audioSamplingRate));

    m_audioGraph.Attach(spAudioGraph.Detach());

    return S_OK;
}

HRESULT AdaptiveStreamer::ReleaseAudioGraph()
{
    if (m_audioGraph)
    {
        m_audioGraph->remove_QuantumStarted(m_quantumStartedEventToken);
        m_audioGraph.Reset();
        m_audioGraph = nullptr;
    }

    return S_OK;
}

HRESULT AdaptiveStreamer::OnFailed(IMediaPlayer* sender, IMediaPlayerFailedEventArgs* args)
{
    HRESULT hr = S_OK;

    if (m_bIgnoreEvents)
        return S_OK;

    IFR(args->get_ExtendedErrorCode(&hr));

    SafeString errorMessage;
    IFR(args->get_ErrorMessage(errorMessage.GetAddressOf()));

    LOG_RESULT_MSG(hr, errorMessage.c_str());

    PLAYBACK_STATE playbackState;
    ZeroMemory(&playbackState, sizeof(playbackState));
    playbackState.type = StateType::StateType_Failed;
    playbackState.state = PlaybackState::PlaybackState_None;
    playbackState.hresult = hr;

    return S_OK;
}

HRESULT AdaptiveStreamer::OnVideoFrameAvailable(IMediaPlayer* sender, IInspectable* arg)
{
    std::cout << "Video frame received!" << std::endl;

    if (!m_readyForFrames || m_deviceNotReady)
        return S_OK;

    if (nullptr != m_primaryMediaSurface && m_mediaPlayer5)
    {
        m_mediaPlayer5->CopyFrameToVideoSurface(m_primaryMediaSurface.Get());
    }

    return S_OK;
}

HRESULT AdaptiveStreamer::AddStateChanged()
{
    if (m_mediaPlaybackSession)
    {
        EventRegistrationToken stateChangedToken;
        auto stateChanged = Microsoft::WRL::Callback<IMediaPlaybackSessionEventHandler>(
            this, &AdaptiveStreamer::OnStateChanged);
        IFR(m_mediaPlaybackSession->add_PlaybackStateChanged(stateChanged.Get(), &stateChangedToken));
        m_stateChangedEventToken = stateChangedToken;

        EventRegistrationToken sizeChangedToken;
        auto sizeChanged = Microsoft::WRL::Callback<IMediaPlaybackSessionEventHandler>(
            this, &AdaptiveStreamer::OnSizeChanged);
        IFR(m_mediaPlaybackSession->add_NaturalVideoSizeChanged(sizeChanged.Get(), &sizeChangedToken));
        m_sizeChangedEventToken = sizeChangedToken;

        EventRegistrationToken durationChangedToken;
        auto durationChanged = Microsoft::WRL::Callback<IMediaPlaybackSessionEventHandler>(
            this, &AdaptiveStreamer::OnStateChanged);
        IFR(m_mediaPlaybackSession->add_NaturalDurationChanged(durationChanged.Get(), &durationChangedToken));
        m_durationChangedEventToken = durationChangedToken;
    }

    return S_OK;
}

HRESULT AdaptiveStreamer::OnStateChanged(IMediaPlaybackSession* sender, IInspectable* args)
{
    if (m_bIgnoreEvents)
        return S_OK;

    auto session = m_mediaPlaybackSession;
    if (session == nullptr)
        session = sender;

    MediaPlaybackState state;
    IFR(session->get_PlaybackState(&state));

    PLAYBACK_STATE playbackState;
    ZeroMemory(&playbackState, sizeof(playbackState));
    playbackState.type = StateType::StateType_StateChanged;
    playbackState.state = static_cast<PlaybackState>(state);

    if (state != MediaPlaybackState_None &&
        state != MediaPlaybackState_Opening)
    {
        // width & height of video
        UINT32 width = 0;
        UINT32 height = 0;
        if (SUCCEEDED(session->get_NaturalVideoWidth(&width)) &&
            SUCCEEDED(session->get_NaturalVideoHeight(&height)))
        {
            StereoscopicVideoRenderMode renderMode = StereoscopicVideoRenderMode_Mono;
            m_mediaPlayer3->get_StereoscopicVideoRenderMode(&renderMode);

            //  if rendering is stereoscopic, we force over-under layout and the frame texture height is 2x "natural" height 
            if (renderMode == StereoscopicVideoRenderMode_Stereo)
            {
                height *= 2;
            }

            playbackState.description.width = width;
            playbackState.description.height = height;

            boolean canSeek = false;
            session->get_CanSeek(&canSeek);

            ABI::Windows::Foundation::TimeSpan duration;
            session->get_NaturalDuration(&duration);

            playbackState.description.canSeek = canSeek;
            playbackState.description.duration = duration.Duration;
            playbackState.description.isStereoscopic =
                (renderMode == StereoscopicVideoRenderMode_Stereo) ? 1 : 0;
        }
    }

    return S_OK;
}

HRESULT AdaptiveStreamer::OnSizeChanged(IMediaPlaybackSession*, IInspectable*)
{
    UINT32 width = 0;
    UINT32 height = 0;

    m_mediaPlaybackSession->get_NaturalVideoWidth(&width);
    m_mediaPlaybackSession->get_NaturalVideoHeight(&height);

    if (width && height)
    {
        ReleaseTextures();

        // Do not call CreatePlaybackTexures() here, it causes threading issues on Unity's D3D11 device
        // Instead, set m_createTextures to true, so next time we receive a rendering event (GL.IssuePluginEvent), we create textures 
        m_createTextures = true;
    }

    return S_OK;
}

void AdaptiveStreamer::ReleaseTextures()
{
    Log(Log_Level_Info, L"AdaptiveStreamer::ReleaseTextures()");

    m_readyForFrames = false;

    // primary texture
    if (m_primarySharedHandle != INVALID_HANDLE_VALUE)
    {
        m_primarySharedHandle = INVALID_HANDLE_VALUE;
    }

    m_primaryMediaSurface.Reset();
    m_primaryMediaSurface = nullptr;

    m_primaryMediaTexture.Reset();
    m_primaryMediaTexture = nullptr;

    m_primaryTextureSRV.Reset();
    m_primaryTextureSRV = nullptr;

    m_primaryTexture.Reset();
    m_primaryTexture = nullptr;
}

HRESULT AdaptiveStreamer::CreateAudioGraphNodes(_In_ IMediaSource2* pSource)
{
#ifdef WAV_FILE_INPUT_NODE
    ComPtr<IAudioFileInputNode> spInputNode;
    ComPtr<IAudioGraph> spAudioGraph;
    IFR(m_audioGraph.As(&spAudioGraph));
    IFR(CreateInputNode(spAudioGraph.Get(), L"C:\\Windows\\Media\\Ring05.wav", &spInputNode, nullptr)); // WAV file is in the code's folder
#else
    // Create the audio input node
    ComPtr<IMediaSourceAudioInputNode> spInputNode;
    ComPtr<IAudioGraph3> spAudioGraph3;
    IFR(m_audioGraph.As(&spAudioGraph3));
    ComPtr<ICreateMediaSourceAudioInputNodeResult> spResult;
    IFR(CreateInputNode(spAudioGraph3.Get(), pSource, &spInputNode, &spResult));
    MediaSourceAudioInputNodeCreationStatus spStatus;
    IFR(spResult->get_Status(&spStatus));
#endif

#ifdef AUDIOGRAPH_SOUND_CARD_OUTPUT
    ComPtr<IAudioDeviceOutputNode> spOutputNode;
#else
    ComPtr<IAudioFrameOutputNode> spOutputNode;
#endif
    IFR(CreateOutputNode(m_audioGraph.Get(), &spOutputNode));
    ComPtr<IAudioNode> spAudioNodeOut;
    IFR(spOutputNode.As(&spAudioNodeOut));

    // Link the input and output nodes
    ComPtr<IAudioInputNode> spAudioInputNode;
    IFR(spInputNode.As(&spAudioInputNode));
    IFR(spAudioInputNode->AddOutgoingConnection(spAudioNodeOut.Get()));

    m_audioInNode.Attach(spInputNode.Detach());
    m_audioOutNode.Attach(spOutputNode.Detach());

    return S_OK;
}

void AdaptiveStreamer::ReleaseMediaPlayer()
{
    Log(Log_Level_Info, L"AdaptiveStreamer::ReleaseMediaPlayer()");

    m_subtitleTracks.clear();

    RemoveStateChanged();

    m_mediaPlaybackSession.Reset();
    m_mediaPlaybackSession = nullptr;

    if (nullptr != m_mediaPlayer)
    {
        LOG_RESULT(m_mediaPlayer->remove_MediaFailed(m_failedEventToken));

        // stop playback
        ComPtr<IMediaPlayerSource2> spMediaPlayerSource;
        m_mediaPlayer.As(&spMediaPlayerSource);
        if (spMediaPlayerSource != nullptr)
            spMediaPlayerSource->put_Source(nullptr);

        if (m_spAdaptiveMediaSource.Get() != nullptr)
        {
            m_spAdaptiveMediaSource.Reset();
            m_spAdaptiveMediaSource = nullptr;
        }

        if (m_audioInNode)
        {
            m_audioInNode.Reset();
            m_audioInNode = nullptr;
        }

        if (m_audioOutNode)
        {
            m_audioOutNode.Reset();
            m_audioOutNode = nullptr;
        }

#ifdef USE_AUDIOGRAPH
        ReleaseAudioGraph();
#endif

        if (m_mediaPlayer5)
        {
            LOG_RESULT(m_mediaPlayer5->remove_VideoFrameAvailable(m_videoFrameAvailableToken));

            m_mediaPlayer5.Reset();
            m_mediaPlayer5 = nullptr;
        }

        if (m_mediaPlayer3)
        {
            m_mediaPlayer3.Reset();
            m_mediaPlayer3 = nullptr;
        }

        if (m_spPlaybackItem != nullptr)
        {
            m_spPlaybackItem.Reset();
            m_spPlaybackItem = nullptr;
        }

        m_mediaPlayer.Reset();
        m_mediaPlayer = nullptr;
    }
}

void AdaptiveStreamer::RemoveStateChanged()
{
    // remove playback session callbacks
    if (nullptr != m_mediaPlaybackSession)
    {
        LOG_RESULT(m_mediaPlaybackSession->remove_PlaybackStateChanged(m_stateChangedEventToken));
        LOG_RESULT(m_mediaPlaybackSession->remove_NaturalVideoSizeChanged(m_sizeChangedEventToken));
        LOG_RESULT(m_mediaPlaybackSession->remove_NaturalDurationChanged(m_durationChangedEventToken));
    }
}

HRESULT AdaptiveStreamer::CreatePlaybackTextures()
{
    m_readyForFrames = false;

    ReleaseTextures();

    UINT32 width = 0;
    UINT32 height = 0;

    m_mediaPlaybackSession->get_NaturalVideoWidth(&width);
    m_mediaPlaybackSession->get_NaturalVideoHeight(&height);

    if (!width || !height)
        return E_UNEXPECTED;

    if (!m_d3dDevice || !m_mediaPlayer3 || !m_mediaPlaybackSession)
    {
        return E_ILLEGAL_METHOD_CALL;
    }

    // create the video texture description based on texture format
    ZeroMemory(&m_textureDesc, sizeof(m_textureDesc));
    m_textureDesc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_B8G8R8A8_UNORM, width, height);
    m_textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    m_textureDesc.MipLevels = 1;
    m_textureDesc.ArraySize = 1;
    m_textureDesc.SampleDesc = { 1, 0 };
    m_textureDesc.CPUAccessFlags = 0;
    m_textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    m_textureDesc.Usage = D3D11_USAGE_DEFAULT;

    // create staging texture on unity device
    ComPtr<ID3D11Texture2D> spTexture;
    IFR(m_d3dDevice->CreateTexture2D(&m_textureDesc, nullptr, spTexture.ReleaseAndGetAddressOf()));

    auto srvDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(spTexture.Get(), D3D11_SRV_DIMENSION_TEXTURE2D);
    ComPtr<ID3D11ShaderResourceView> spSRV;

    IFR(m_d3dDevice->CreateShaderResourceView(spTexture.Get(), &srvDesc, spSRV.ReleaseAndGetAddressOf()));

    // create a shared texture from the unity texture
    ComPtr<IDXGIResource1> spDXGIResource;
    IFR(spTexture.As(&spDXGIResource));

    auto sharedHandle = INVALID_HANDLE_VALUE;
    ComPtr<ID3D11Texture2D> spMediaTexture;
    ComPtr<IDirect3DSurface> spMediaSurface;

    HRESULT hr = spDXGIResource->GetSharedHandle(&sharedHandle);

    if (SUCCEEDED(hr))
    {
        ComPtr<ID3D11Device1> spMediaDevice;
        hr = m_mediaDevice.As(&spMediaDevice);
        if (SUCCEEDED(hr))
        {
            hr = spMediaDevice->OpenSharedResource(sharedHandle, IID_PPV_ARGS(&spMediaTexture));

            if (SUCCEEDED(hr))
            {
                hr = GetSurfaceFromTexture(spMediaTexture.Get(), &spMediaSurface);
            }
        }
    }

    IFR(hr);

    m_primaryTexture.Attach(spTexture.Detach());
    m_primaryTextureSRV.Attach(spSRV.Detach());

    m_primarySharedHandle = sharedHandle;
    m_primaryMediaTexture.Attach(spMediaTexture.Detach());
    m_primaryMediaSurface.Attach(spMediaSurface.Detach());

    PLAYBACK_STATE playbackState;
    ZeroMemory(&playbackState, sizeof(playbackState));
    playbackState.type = StateType::StateType_NewFrameTexture;
    playbackState.state = PlaybackState::PlaybackState_NA;

    playbackState.description.width = width;
    playbackState.description.height = height;

    boolean canSeek = false;
    m_mediaPlaybackSession->get_CanSeek(&canSeek);

    ABI::Windows::Foundation::TimeSpan duration;
    m_mediaPlaybackSession->get_NaturalDuration(&duration);

    playbackState.description.canSeek = canSeek;
    playbackState.description.duration = duration.Duration;
    playbackState.description.isStereoscopic = 0;
    
    m_readyForFrames = true;

    return S_OK;
}

HRESULT AdaptiveStreamer::OnAudioGraphQuantumStarted(_In_ IAudioGraph* sender, _In_ IInspectable* args)
{
    std::cout << "Audio graph quantum started" << std::endl;
    return S_OK;
}