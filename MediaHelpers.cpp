//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "pch.h"
#include "MediaHelpers.h"
#include <windows.storage.accesscache.h>

using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace ABI::Windows::Media::Core;
using namespace ABI::Windows::Media::Playback;
using namespace ABI::Windows::Media::Streaming::Adaptive;
using namespace ABI::Windows::Media::Audio;

using namespace Microsoft::WRL;

#if defined(_DEBUG)
// Check for SDK Layer support.
inline bool SdkLayersAvailable()
{
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_NULL, // There is no need to create a real hardware device.
        0,
        D3D11_CREATE_DEVICE_DEBUG, // Check for the SDK layers.
        nullptr, // Any feature level will do.
        0,
        D3D11_SDK_VERSION, // Always set this to D3D11_SDK_VERSION for Windows Store apps.
        nullptr, // No need to keep the D3D device reference.
        nullptr, // No need to know the feature level.
        nullptr // No need to keep the D3D device context reference.
    );

    return SUCCEEDED(hr);
}
#endif

using namespace ABI::Windows::Media::Streaming::Adaptive;
using namespace ABI::Windows::Foundation;
using namespace Wrappers;

#include <ppl.h>
#include <ppltasks.h>

void CreateAdaptiveMediaSourceFromUri(
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

    ComPtr<ICreateAdaptiveMediaSourceOperation> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<IAdaptiveMediaSourceCreationResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<AdaptiveMediaSourceCreationResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
        ICreateAdaptiveMediaSourceOperation* pOp,
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

_Use_decl_annotations_

HRESULT CreateMediaSource(
    LPCWSTR pszUrl,
    IMediaSource2** ppMediaSource)
{
    NULL_CHK(pszUrl);
    NULL_CHK(ppMediaSource);

    *ppMediaSource = nullptr;

    // convert the uri
    ComPtr<IUriRuntimeClassFactory> spUriFactory;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
        &spUriFactory));

    ComPtr<IUriRuntimeClass> spUri;
    IFR(spUriFactory->CreateUri(
        Microsoft::WRL::Wrappers::HStringReference(pszUrl).Get(),
        &spUri));

    // create a media source
    ComPtr<IMediaSourceStatics> spMediaSourceStatics;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Media_Core_MediaSource).Get(),
        &spMediaSourceStatics));

    ComPtr<IMediaSource2> spMediaSource2;

#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	Microsoft::WRL::Wrappers::HString scheme;
	spUri->get_SchemeName(scheme.GetAddressOf());
	if (std::wstring(L"file-access") == scheme.GetRawBuffer(nullptr))
	{
		Microsoft::WRL::Wrappers::HString path;
		spUri->get_Path(path.GetAddressOf());
		const wchar_t* pathPtr = path.GetRawBuffer(nullptr);
		if (pathPtr[0] == L'\\' || pathPtr[0] == L'/')
			pathPtr++;

		ComPtr<ABI::Windows::Storage::AccessCache::IStorageApplicationPermissionsStatics> permStats;
		ComPtr<ABI::Windows::Storage::AccessCache::IStorageItemAccessList> accList;
		IFR(ABI::Windows::Foundation::GetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_AccessCache_StorageApplicationPermissions).Get(),
			&permStats));
		IFR(permStats->get_FutureAccessList(&accList));

		boolean hasItem = false;
		Microsoft::WRL::Wrappers::HStringReference token(pathPtr);
		accList->ContainsItem(token.Get(), &hasItem);
		if (hasItem)
		{
			ComPtr<IAsyncOperation<ABI::Windows::Storage::StorageFile*>> fileOp;
			ComPtr<ABI::Windows::Storage::IStorageFile> file;
			Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));
			HRESULT hrResult = S_OK;

			auto callback = Callback<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<ABI::Windows::Storage::StorageFile*>>(
				[&operationCompleted, &file, &hrResult](
					IAsyncOperation<ABI::Windows::Storage::StorageFile*>* pOper, 
					AsyncStatus status
					) -> HRESULT
			{
				if (status == AsyncStatus::Completed)
				{
					ABI::Windows::Storage::IStorageFile* pFile = nullptr;
					hrResult = pOper->GetResults(&pFile);
					file.Attach(pFile);
				}
				else
				{
					hrResult = E_FAIL;
				}

				SetEvent(operationCompleted.Get());
				return S_OK;
			});

			concurrency::create_task([&]() {
				hrResult = accList->GetFileAsync(token.Get(), fileOp.GetAddressOf());
				if (fileOp)
				{
					fileOp->put_Completed(callback.Get());
				}
				else
				{
					SetEvent(operationCompleted.Get());
				}
			});

			DWORD result = WaitForSingleObject(operationCompleted.Get(), INFINITE);

			if (file.Get())
			{
				IFR(spMediaSourceStatics->CreateFromStorageFile(file.Get(), &spMediaSource2));
			}
			else
			{
				return hrResult;
			}
		}
		else
		{
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		}
	}
	else
#endif
    {
        ComPtr<IAdaptiveMediaSource> adaptiveSource;
        CreateAdaptiveMediaSourceFromUri(pszUrl, adaptiveSource.GetAddressOf(), nullptr);

        if (adaptiveSource != nullptr)
            spMediaSourceStatics->CreateFromAdaptiveMediaSource(adaptiveSource.Get(), &spMediaSource2);

        if (spMediaSource2.Get() == nullptr)
        {
            IFR(spMediaSourceStatics->CreateFromUri(
                spUri.Get(),
                &spMediaSource2));
        }
    }


    *ppMediaSource = spMediaSource2.Detach();

    return S_OK;
}


HRESULT CreateAdaptiveMediaSource(
    LPCWSTR pszManifestLocation,
    IAdaptiveMediaSourceCompletedCallback* pCallback)
{
    NULL_CHK(pszManifestLocation);
    NULL_CHK(pCallback);

    ComPtr<IAdaptiveMediaSourceCompletedCallback> spCallback(pCallback);

    // convert the uri
    ComPtr<IUriRuntimeClassFactory> spUriFactory;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
        &spUriFactory));

    ComPtr<IUriRuntimeClass> spUri;
    IFR(spUriFactory->CreateUri(
        Wrappers::HStringReference(pszManifestLocation).Get(),
        &spUri));

    // get factory for adaptive source
    ComPtr<IAdaptiveMediaSourceStatics> spAdaptiveSourceStatics;
    IFR(ABI::Windows::Foundation::GetActivationFactory(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Streaming_Adaptive_AdaptiveMediaSource).Get(),
        &spAdaptiveSourceStatics));

    // get the asyncOp for creating the source
    ComPtr<ICreateAdaptiveMediaSourceOperation> asyncOp;
    IFR(spAdaptiveSourceStatics->CreateFromUriAsync(spUri.Get(), &asyncOp));

    // create a completed callback
    auto completedHandler = Microsoft::WRL::Callback<ICreateAdaptiveMediaSourceResultHandler>(
        [spCallback, asyncOp](_In_ ICreateAdaptiveMediaSourceOperation* pOp, _In_ AsyncStatus status) -> HRESULT
        {
            return spCallback->OnAdaptiveMediaSourceCreated(pOp, status);
        });

    IFR(asyncOp->put_Completed(completedHandler.Get()));

    return S_OK;
}

_Use_decl_annotations_

HRESULT CreateMediaPlaybackItem(
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

_Use_decl_annotations_

HRESULT CreatePlaylistSource(
    IMediaSource2* pSource,
    IMediaPlaybackSource** ppMediaPlaybackSource)
{
    NULL_CHK(pSource);
    NULL_CHK(ppMediaPlaybackSource);

    *ppMediaPlaybackSource = nullptr;

    ComPtr<IMediaPlaybackList> spPlaylist;
    IFR(Windows::Foundation::ActivateInstance(
        Wrappers::HStringReference(RuntimeClass_Windows_Media_Playback_MediaPlaybackList).Get(),
        &spPlaylist));

    // get the iterator for playlist
    ComPtr<Collections::IObservableVector<MediaPlaybackItem*>> spItems;
    IFR(spPlaylist->get_Items(&spItems));

    ComPtr<Collections::IVector<MediaPlaybackItem*>> spItemsVector;
    IFR(spItems.As(&spItemsVector));

    // create playbackitem from source
    ComPtr<IMediaPlaybackItem> spItem;
    IFR(CreateMediaPlaybackItem(pSource, &spItem));

    // add to the list
    IFR(spItemsVector->Append(spItem.Get()));

    // convert to playbackSource
    ComPtr<IMediaPlaybackSource> spMediaPlaybackSource;
    IFR(spPlaylist.As(&spMediaPlaybackSource));

    *ppMediaPlaybackSource = spMediaPlaybackSource.Detach();

    return S_OK;
}

_Use_decl_annotations_

HRESULT GetSurfaceFromTexture(
    ID3D11Texture2D* pTexture,
    IDirect3DSurface** ppSurface)
{
    NULL_CHK(pTexture);
    NULL_CHK(ppSurface);

    *ppSurface = nullptr;

    ComPtr<ID3D11Texture2D> spTexture(pTexture);

    ComPtr<IDXGISurface> dxgiSurface;
    IFR(spTexture.As(&dxgiSurface));

    ComPtr<IInspectable> inspectableSurface;
    IFR(CreateDirect3D11SurfaceFromDXGISurface(dxgiSurface.Get(), &inspectableSurface));

    ComPtr<IDirect3DSurface> spSurface;
    IFR(inspectableSurface.As(&spSurface));

    *ppSurface = spSurface.Detach();

    return S_OK;
}

_Use_decl_annotations_

HRESULT GetTextureFromSurface(
    IDirect3DSurface* pSurface,
    ID3D11Texture2D** ppTexture)
{
    NULL_CHK(pSurface);
    NULL_CHK(ppTexture);

    *ppTexture = nullptr;

    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> spDXGIInterfaceAccess;
    IFR(pSurface->QueryInterface(IID_PPV_ARGS(&spDXGIInterfaceAccess)));
    IFR(spDXGIInterfaceAccess->GetInterface(IID_PPV_ARGS(ppTexture)));

    return S_OK;
}

_Use_decl_annotations_

HRESULT CreateMediaDevice(
    IDXGIAdapter* pDXGIAdapter,
    ID3D11Device** ppDevice)
{
    NULL_CHK(ppDevice);

    // Create the Direct3D 11 API device object and a corresponding context.
    D3D_FEATURE_LEVEL featureLevel;

    // This flag adds support for surfaces with a different color channel ordering
    // than the API default. It is required for compatibility with Direct2D.
    UINT creationFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    if (SdkLayersAvailable())
    {
        // If the project is in a debug build, enable debugging via SDK Layers with this flag.
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif
    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> spDevice;
    ComPtr<ID3D11DeviceContext> spContext;

    D3D_DRIVER_TYPE driverType = (nullptr != pDXGIAdapter) ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    // Create a device using the hardware graphics driver if adapter is not supplied
    HRESULT hr = D3D11CreateDevice(
        pDXGIAdapter, // if nullptr will use default adapter.
        driverType,
        nullptr, // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
        creationFlags, // Set debug and Direct2D compatibility flags.
        featureLevels, // List of feature levels this app can support.
        ARRAYSIZE(featureLevels), // Size of the list above.
        D3D11_SDK_VERSION, // Always set this to D3D11_SDK_VERSION for Windows Store apps.
        &spDevice, // Returns the Direct3D device created.
        &featureLevel, // Returns feature level of device created.
        &spContext // Returns the device immediate context.
    );

    if (FAILED(hr))
    {
        // fallback to WARP if we are not specifying an adapter
        if (nullptr == pDXGIAdapter)
        {
            // If the initialization fails, fall back to the WARP device.
            // For more information on WARP, see: 
            // http://go.microsoft.com/fwlink/?LinkId=286690
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP, // Create a WARP device instead of a hardware device.
                nullptr,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &spDevice,
                &featureLevel,
                &spContext);
        }

        IFR(hr);
    }
    else
    {
        // workaround for nvidia GPU's, cast to ID3D11VideoDevice
        ComPtr<ID3D11VideoDevice> videoDevice;
        spDevice.As(&videoDevice);
    }

    // Turn multithreading on 
    ComPtr<ID3D10Multithread> spMultithread;
    if (SUCCEEDED(spContext.As(&spMultithread)))
    {
        spMultithread->SetMultithreadProtected(TRUE);
    }

    *ppDevice = spDevice.Detach();

    return S_OK;
}

HRESULT CreateAudioGraphFromSettings(_Outptr_opt_ IAudioGraph** pp,
    _In_ IAudioGraphSettings* pSettings,
    _Outptr_opt_ ICreateAudioGraphResult** ppResult
)
{
    ComPtr<IAudioGraphStatics> spStatics;
    Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_Audio_AudioGraph).Get(),
        &spStatics
    );

    ComPtr<IAsyncOperation<CreateAudioGraphResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<ICreateAudioGraphResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateAudioGraphResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ICreateAudioGraphOperation* pOp,
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

HRESULT CreateAudioGraphSettings(_COM_Outptr_ IAudioGraphSettings** pp)
{
    if (pp != nullptr)
    {
        *pp = nullptr;
    }

    ComPtr<IAudioGraphSettingsFactory> spFactory;
    IFR(Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Media_Audio_AudioGraphSettings).Get(),
        &spFactory
    ));

    ComPtr<IAudioGraphSettings> sp;
    IFR(spFactory->Create(ABI::Windows::Media::Render::AudioRenderCategory::AudioRenderCategory_Media, &sp));
    
    *pp = sp.Detach();

    return S_OK;
}

HRESULT CreateInputNode(_In_ IAudioGraph3* pAudioGraph, _In_ IMediaSource2* pSource, _COM_Outptr_ IMediaSourceAudioInputNode** pp, _Outptr_opt_ ICreateMediaSourceAudioInputNodeResult** ppResult)
{
    ComPtr<IAsyncOperation<CreateMediaSourceAudioInputNodeResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<ICreateMediaSourceAudioInputNodeResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateMediaSourceAudioInputNodeResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ICreateMediaSourceAudioInputNodeOperation* pOp,
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
        ComPtr<IMediaSourceAudioInputNode> spGraph;
        spResult->get_Node(&spGraph);

        if (pp != nullptr)
        {
            *pp = spGraph.Detach();
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

/// <summary>
/// Create an Audio Graph input node for a local file.
/// </summary>
/// <param name="pAudioGraph"></param>
/// <param name="path"></param>
/// <param name="pp"></param>
/// <param name="ppResult"></param>
/// <returns></returns>
HRESULT CreateInputNode(_In_ IAudioGraph* pAudioGraph,
    _In_ LPCWSTR path,
    _COM_Outptr_ IAudioFileInputNode** pp,
    _Outptr_opt_ ICreateAudioFileInputNodeResult** ppResult
)
{
    // Create the file from the path
    ComPtr<ABI::Windows::Storage::IStorageFileStatics> spStorageStatics;
    Windows::Foundation::GetActivationFactory(
        HStringReference(RuntimeClass_Windows_Storage_StorageFile).Get(),
        &spStorageStatics
    );

    ComPtr<IAsyncOperation<ABI::Windows::Storage::StorageFile*>> spFileStorageOperation;
    Event operationCompletedFileStorage(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    HRESULT hrStatusFileStorage = S_OK;
    ComPtr<ABI::Windows::Storage::IStorageFile> spResultFileStorage;

    auto callbackFileStorage = Callback<IAsyncOperationCompletedHandler<ABI::Windows::Storage::StorageFile*>>(
        [&operationCompletedFileStorage, &hrStatusFileStorage, &spResultFileStorage](
            IOpenStorageFileOperation* pOp,
            AsyncStatus status
            ) -> HRESULT
        {
            if (status == AsyncStatus::Completed)
            {
                hrStatusFileStorage = pOp->GetResults(&spResultFileStorage);
            }
            else
            {
                hrStatusFileStorage = E_FAIL;
            }

            SetEvent(operationCompletedFileStorage.Get());
            return S_OK;
        });

    concurrency::create_task([&]()
        {
            spStorageStatics->GetFileFromPathAsync(HStringReference(path).Get(), &spFileStorageOperation);
            if (spFileStorageOperation)
            {
                spFileStorageOperation->put_Completed(callbackFileStorage.Get());
            }
            else
            {
                SetEvent(operationCompletedFileStorage.Get());
            }
        });

    DWORD resultFileStorage = WaitForSingleObject(operationCompletedFileStorage.Get(), INFINITE);

    // Create the input node
    ComPtr<IAsyncOperation<CreateAudioFileInputNodeResult*>> spCreateOperation;
    Event operationCompleted(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HRESULT hrStatus = S_OK;
    ComPtr<ICreateAudioFileInputNodeResult> spResult;

    auto callback = Callback<IAsyncOperationCompletedHandler<CreateAudioFileInputNodeResult*>>(
        [&operationCompleted, &hrStatus, &spResult](
            ICreateAudioFileInputNodeOperation* pOp,
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
            pAudioGraph->CreateFileInputNodeAsync(spResultFileStorage.Get(), &spCreateOperation);
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

    AudioFileNodeCreationStatus creationStatus = AudioFileNodeCreationStatus::AudioFileNodeCreationStatus_UnknownFailure;
    if (spResult)
        spResult->get_Status(&creationStatus);

    if (creationStatus == AudioFileNodeCreationStatus::AudioFileNodeCreationStatus_Success)
    {
        ComPtr<IAudioFileInputNode> spGraph;
        spResult->get_FileInputNode(&spGraph);

        if (pp != nullptr)
        {
            *pp = spGraph.Detach();
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

/// <summary>
/// Create an Audio Graph output node for processing in code.
/// </summary>
/// <param name="pAudioGraph"></param>
/// <param name="pp"></param>
/// <returns></returns>
HRESULT CreateOutputNode(_In_ IAudioGraph* pAudioGraph, _COM_Outptr_ IAudioFrameOutputNode** pp)
{
    IFR(pAudioGraph->CreateFrameOutputNode(pp));

    return S_OK;
}

/// <summary>
/// Create an Audio Graph output node for a sound card.
/// </summary>
/// <param name="pAudioGraph"></param>
/// <param name="pp"></param>
/// <returns></returns>
HRESULT CreateOutputNode(_In_ IAudioGraph* pAudioGraph, _COM_Outptr_ IAudioDeviceOutputNode** pp)
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
