#include "SimpleAdaptiveStreamer.h"
#include <iostream>
#include <ppl.h>
#include <ppltasks.h>

SimpleAdaptiveStreamer::SimpleAdaptiveStreamer()
{
}

SimpleAdaptiveStreamer::~SimpleAdaptiveStreamer()
{
    // TODO: proper cleanup
}

HRESULT SimpleAdaptiveStreamer::Initialize()
{
    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::CreateAudioGraph()
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
    auto quantumStarted = Callback<ITypedEventHandler<
        ABI::Windows::Media::Audio::AudioGraph*, IInspectable*>>(this, &SimpleAdaptiveStreamer::OnAudioGraphQuantumStarted);
    IFR(spAudioGraph->add_QuantumStarted(quantumStarted.Get(), &quantumStartedToken));
    
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

HRESULT SimpleAdaptiveStreamer::CreateAudioGraphNodes(_In_ IMediaSource2* pSource)
{
    // Create the audio input node
    ComPtr<IMediaSourceAudioInputNode> spInputNode;
    ComPtr<IAudioGraph3> spAudioGraph3;
    IFR(m_audioGraph.As(&spAudioGraph3));
    ComPtr<ICreateMediaSourceAudioInputNodeResult> spResult;
    IFR(CreateInputNode(spAudioGraph3.Get(), pSource, &spInputNode, &spResult));
    MediaSourceAudioInputNodeCreationStatus spStatus;
    IFR(spResult->get_Status(&spStatus));

    ComPtr<IAudioDeviceOutputNode> spOutputNode;
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

HRESULT SimpleAdaptiveStreamer::LoadContent(const std::wstring& sURL)
{
    // 1) Create a IAdaptiveMediaSource
    IFR(CreateMediaSource(sURL.c_str(), &m_spAdaptiveMediaSource));

    // 2) Create a IMediaSource2 from the adaptive media source
    ComPtr<IMediaSourceStatics> spMediaSourceStatics;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Core_MediaSource).Get(),
        &spMediaSourceStatics));
    ComPtr<IMediaSource2> spMediaSource2;
    spMediaSourceStatics->CreateFromAdaptiveMediaSource(m_spAdaptiveMediaSource.Get(), &spMediaSource2);

    // 3) Create a IMediaPlaybackItem
    IFR(CreateMediaPlaybackItem(spMediaSource2.Get(), m_spPlaybackItem.ReleaseAndGetAddressOf()));

    // 4) Create a IMediaPlaybackSource
    ComPtr<IMediaPlaybackSource> spMediaPlaybackSource;
    IFR(m_spPlaybackItem.As(&spMediaPlaybackSource));

    // 5) Create the MediaPlayer
    IFR(ABI::Windows::Foundation::ActivateInstance(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Playback_MediaPlayer).Get(),
        &m_mediaPlayer));
    m_mediaPlayer->put_AutoPlay(false);

    // !!! WARNING
    IFR(CreateAudioGraph());
#if true
    // This gives this in OnFailed callback: HR: 0xc00d36da - Some component is already listening to events on this event generator.
    IFR(CreateAudioGraphNodes(spMediaSource2.Get()));
#else
    // This gives this in OnFailed callback: HR: 0xc00d36b2 - The request is invalid in the current state. 
    ComPtr<IMediaSource2> spMediaSource2_other;
    spMediaSourceStatics->CreateFromAdaptiveMediaSource(m_spAdaptiveMediaSource.Get(), &spMediaSource2_other);
    IFR(CreateAudioGraphNodes(spMediaSource2_other.Get()));
#endif

    // 6) Update the source of the MediaPlayer
    ComPtr<IMediaPlayerSource2> spPlayerAsMediaPlayerSource;
    IFR(m_mediaPlayer.As(&spPlayerAsMediaPlayerSource));
    IFR(spPlayerAsMediaPlayerSource->put_Source(spMediaPlaybackSource.Get()));

    // 7) Register callbacks and enable video frame server mode
    EventRegistrationToken failedEventToken;
    auto mediaFailed = Callback<ITypedEventHandler<ABI::Windows::Media::Playback::MediaPlayer*, ABI::Windows::Media::Playback::MediaPlayerFailedEventArgs*>>(this, &SimpleAdaptiveStreamer::OnFailed);
    IFR(m_mediaPlayer->add_MediaFailed(mediaFailed.Get(), &failedEventToken));

    // frameserver mode is on the IMediaPlayer5 interface
    ComPtr<IMediaPlayer5> spMediaPlayer5;
    IFR(m_mediaPlayer.As(&spMediaPlayer5));

    // set frameserver mode
    IFR(spMediaPlayer5->put_IsVideoFrameServerEnabled(true));

    EventRegistrationToken videoFrameAvailableToken;
    auto videoFrameAvailableCallback = Callback<ITypedEventHandler<ABI::Windows::Media::Playback::MediaPlayer*, IInspectable*>>(this, &SimpleAdaptiveStreamer::OnVideoFrameAvailable);
    IFR(spMediaPlayer5->add_VideoFrameAvailable(videoFrameAvailableCallback.Get(), &videoFrameAvailableToken));

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::Play()
{
    IFR(m_mediaPlayer->Play());
    IFR(m_audioGraph->Start());
    return S_OK;
}


HRESULT SimpleAdaptiveStreamer::Pause()
{
    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::Stop()
{
    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::OnFailed(IMediaPlayer* sender, IMediaPlayerFailedEventArgs* args)
{
    std::cout << "Failed!" << std::endl;

    HRESULT hr = S_OK;

    IFR(args->get_ExtendedErrorCode(&hr));

    SafeString errorMessage;
    IFR(args->get_ErrorMessage(errorMessage.GetAddressOf()));

    LOG_RESULT_MSG(hr, errorMessage.c_str());

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::OnVideoFrameAvailable(IMediaPlayer* sender, IInspectable* arg)
{
    std::cout << "Video frame received!" << std::endl;

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::OnAudioGraphQuantumStarted(_In_ IAudioGraph* sender, _In_ IInspectable* args)
{
    std::cout << "Audio graph quantum started" << std::endl;
    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::CreateAudioGraphFromSettings(_Outptr_opt_ IAudioGraph** pp,
    _In_ IAudioGraphSettings* pSettings,
    _Outptr_opt_ ICreateAudioGraphResult** ppResult
)
{
    ComPtr<IAudioGraphStatics> spStatics;
    Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Audio_AudioGraph).Get(),
        &spStatics
    );

    ComPtr<IAsyncOperation<CreateAudioGraphResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<ICreateAudioGraphResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateAudioGraphResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Media::Audio::CreateAudioGraphResult*>* pOp,
            AsyncStatus status
            ) -> HRESULT
        {
            if (status == AsyncStatus::Completed)
            {
                hrStatus = pOp->GetResults(&spResult);
            }
            else
            {
                hrStatus = E_FAIL;
            }

            SetEvent(operationCompleted.Get());
            return S_OK;
        });

    concurrency::create_task([&]()
        {
            spStatics->CreateAsync(
                pSettings,
                &spCreateOperation
            );
            if (spCreateOperation)
            {
                spCreateOperation->put_Completed(callback.Get());
            }
            else
            {
                SetEvent(operationCompleted.Get());
            }
        });


    DWORD result = WaitForSingleObject(operationCompleted.Get(), INFINITE);

    AudioGraphCreationStatus creationStatus = AudioGraphCreationStatus::AudioGraphCreationStatus_UnknownFailure;
    if (spResult)
        spResult->get_Status(&creationStatus);

    if (creationStatus == AudioGraphCreationStatus::AudioGraphCreationStatus_Success)
    {
        ComPtr<IAudioGraph> spGraph;
        spResult->get_Graph(&spGraph);

        if (pp != nullptr)
        {
            *pp = spGraph.Detach();
        }
    }

    if (ppResult != nullptr)
    {
        *ppResult = spResult.Detach();
    }

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::CreateAudioGraphSettings(_COM_Outptr_ IAudioGraphSettings** pp)
{
    if (pp != nullptr)
    {
        *pp = nullptr;
    }

    ComPtr<IAudioGraphSettingsFactory> spFactory;
    IFR(Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Audio_AudioGraphSettings).Get(),
        &spFactory
    ));

    ComPtr<IAudioGraphSettings> sp;
    IFR(spFactory->Create(ABI::Windows::Media::Render::AudioRenderCategory::AudioRenderCategory_Media, &sp));

    *pp = sp.Detach();

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::CreateInputNode(_In_ IAudioGraph3* pAudioGraph, _In_ IMediaSource2* pSource, _COM_Outptr_ IMediaSourceAudioInputNode** pp, _Outptr_opt_ ICreateMediaSourceAudioInputNodeResult** ppResult)
{
    ComPtr<IAsyncOperation<CreateMediaSourceAudioInputNodeResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<ICreateMediaSourceAudioInputNodeResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateMediaSourceAudioInputNodeResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Media::Audio::CreateMediaSourceAudioInputNodeResult*>* pOp,
            AsyncStatus status
            ) -> HRESULT
        {
            if (status == AsyncStatus::Completed)
            {
                hrStatus = pOp->GetResults(&spResult);
            }
            else
            {
                hrStatus = E_FAIL;
            }

            SetEvent(operationCompleted.Get());
            return S_OK;
        });

    concurrency::create_task([&]()
        {
            pAudioGraph->CreateMediaSourceAudioInputNodeAsync(pSource, &spCreateOperation);
            if (spCreateOperation)
            {
                spCreateOperation->put_Completed(callback.Get());
            }
            else
            {
                SetEvent(operationCompleted.Get());
            }
        });


    DWORD result = WaitForSingleObject(operationCompleted.Get(), INFINITE);

    MediaSourceAudioInputNodeCreationStatus creationStatus = MediaSourceAudioInputNodeCreationStatus::MediaSourceAudioInputNodeCreationStatus_UnknownFailure;
    if (spResult)
        spResult->get_Status(&creationStatus);

    if (creationStatus == MediaSourceAudioInputNodeCreationStatus::MediaSourceAudioInputNodeCreationStatus_Success)
    {
        ComPtr<IMediaSourceAudioInputNode> spNode;
        spResult->get_Node(&spNode);

        if (pp != nullptr)
        {
            *pp = spNode.Detach();
        }
    }
    else
    {
        Log(Log_Level_Error, L"Audio input node creation failed.");
        return E_FAIL;
    }

    if (ppResult != nullptr)
    {
        *ppResult = spResult.Detach();
    }

    return S_OK;
}

HRESULT SimpleAdaptiveStreamer::CreateMediaSource(
    LPCWSTR pszUrl,
    IAdaptiveMediaSource** ppMediaSource)
{
    NULL_CHK(pszUrl);
    NULL_CHK(ppMediaSource);

    *ppMediaSource = nullptr;

    // convert the uri
    ComPtr<IUriRuntimeClassFactory> spUriFactory;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
        &spUriFactory));

    ComPtr<IUriRuntimeClass> spUri;
    IFR(spUriFactory->CreateUri(
        Wrappers::HStringReference(pszUrl).Get(),
        &spUri));

    // create a media source
    ComPtr<IMediaSourceStatics> spMediaSourceStatics;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Core_MediaSource).Get(),
        &spMediaSourceStatics));

    ComPtr<IMediaSource2> spMediaSource2;

    ComPtr<IAdaptiveMediaSource> adaptiveSource;
    CreateAdaptiveMediaSourceFromUri(pszUrl, adaptiveSource.GetAddressOf(), nullptr);

    *ppMediaSource = adaptiveSource.Detach();

    return S_OK;
}


HRESULT SimpleAdaptiveStreamer::CreateMediaPlaybackItem(
    _In_ IMediaSource2* pMediaSource,
    _COM_Outptr_ IMediaPlaybackItem** ppMediaPlaybackItem)
{
    // create playbackitem from source
    ComPtr<IMediaPlaybackItemFactory> spItemFactory;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Playback_MediaPlaybackItem).Get(),
        &spItemFactory));

    ComPtr<IMediaPlaybackItem> spItem;
    IFR(spItemFactory->Create(pMediaSource, &spItem));

    *ppMediaPlaybackItem = spItem.Detach();

    return S_OK;
}

void SimpleAdaptiveStreamer::CreateAdaptiveMediaSourceFromUri(
    _In_ PCWSTR szManifestUri,
    _Outptr_opt_ IAdaptiveMediaSource** ppAdaptiveMediaSource,
    _Outptr_opt_ IAdaptiveMediaSourceCreationResult** ppCreationResult
)
{
    HRESULT hr = S_OK;

    if (ppAdaptiveMediaSource != nullptr)
    {
        *ppAdaptiveMediaSource = nullptr;
    }

    if (ppCreationResult != nullptr)
    {
        *ppCreationResult = nullptr;
    }

    ComPtr<IUriRuntimeClassFactory> spUriFactory;
    ABI::Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
        &spUriFactory);

    ComPtr<IUriRuntimeClass> spUri;
    spUriFactory->CreateUri(
        HStringReference(szManifestUri).Get(),
        &spUri
    );

    ComPtr<IAdaptiveMediaSourceStatics> spMediaSourceStatics;
    Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_Streaming_Adaptive_AdaptiveMediaSource).Get(),
        &spMediaSourceStatics
    );

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceCreationResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<IAdaptiveMediaSourceCreationResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<AdaptiveMediaSourceCreationResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceCreationResult*>* pOp,
            AsyncStatus status
            ) -> HRESULT
        {
            if (status == AsyncStatus::Completed)
            {
                hrStatus = pOp->GetResults(&spResult);
            }
            else
            {
                hrStatus = E_FAIL;
            }

            SetEvent(operationCompleted.Get());
            return S_OK;
        });

    concurrency::create_task([&]()
        {
            spMediaSourceStatics->CreateFromUriAsync(
                spUri.Get(),
                &spCreateOperation
            );
            if (spCreateOperation)
            {
                spCreateOperation->put_Completed(callback.Get());
            }
            else
            {
                SetEvent(operationCompleted.Get());
            }
        });


    DWORD result = WaitForSingleObject(operationCompleted.Get(), INFINITE);

    AdaptiveMediaSourceCreationStatus creationStatus = AdaptiveMediaSourceCreationStatus_UnknownFailure;
    if (spResult)
        spResult->get_Status(&creationStatus);

    if (creationStatus == AdaptiveMediaSourceCreationStatus_Success)
    {
        ComPtr<IAdaptiveMediaSource> spMediaSource;
        spResult->get_MediaSource(&spMediaSource);

        if (ppAdaptiveMediaSource != nullptr)
        {
            *ppAdaptiveMediaSource = spMediaSource.Detach();
        }
    }

    if (ppCreationResult != nullptr)
    {
        *ppCreationResult = spResult.Detach();
    }
}

HRESULT SimpleAdaptiveStreamer::CreateOutputNode(_In_ IAudioGraph* pAudioGraph, _COM_Outptr_ IAudioDeviceOutputNode** pp)
{
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    HRESULT hrStatus = S_OK;
    ComPtr<ICreateAudioDeviceOutputNodeResult> spResult;
    ComPtr<IAsyncOperation<CreateAudioDeviceOutputNodeResult*>> spCreateOperation;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateAudioDeviceOutputNodeResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            IAsyncOperation<CreateAudioDeviceOutputNodeResult*>* pOp,
            AsyncStatus status
            ) -> HRESULT
        {
            if (status == AsyncStatus::Completed)
            {
                hrStatus = pOp->GetResults(&spResult);
            }
            else
            {
                hrStatus = E_FAIL;
            }

            SetEvent(operationCompleted.Get());
            return S_OK;
        });

    concurrency::create_task([&]()
        {
            pAudioGraph->CreateDeviceOutputNodeAsync(&spCreateOperation);
            if (spCreateOperation)
            {
                spCreateOperation->put_Completed(callback.Get());
            }
            else
            {
                SetEvent(operationCompleted.Get());
            }
        });


    DWORD result = WaitForSingleObject(operationCompleted.Get(), INFINITE);
    if (result != S_OK)
    {
        Log(Log_Level_Error, L"Async audio output node creation failed.");
        return E_FAIL;
    }

    AudioDeviceNodeCreationStatus creationStatus = AudioDeviceNodeCreationStatus::AudioDeviceNodeCreationStatus_UnknownFailure;
    if (spResult)
        spResult->get_Status(&creationStatus);

    if (creationStatus == AudioFileNodeCreationStatus::AudioFileNodeCreationStatus_Success)
    {
        ComPtr<IAudioDeviceOutputNode> spNode;
        spResult->get_DeviceOutputNode(&spNode);

        if (pp != nullptr)
        {
            *pp = spNode.Detach();
        }
    }
    else
    {
        Log(Log_Level_Error, L"Audio output node creation failed.");
        return E_FAIL;
    }

    return S_OK;
}