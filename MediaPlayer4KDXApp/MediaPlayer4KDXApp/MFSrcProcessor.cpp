#include "pch.h"
#include "Mfapi.h"
#include "mfidl.h"
#include "MFSrcProcessor.h"
#include "mferror.h"
#include <vccorlib.h>

using namespace Platform;
CMFSrcProcessor::CMFSrcProcessor()
{
    m_hSourceThread = nullptr;
    memset(m_hStreamThread, 0, sizeof(m_hStreamThread));
    memset(m_pStreams, 0, sizeof(m_pStreams));
    m_bRunning = false;
    m_bIsEOF = false;

    m_pAudioConnector = nullptr;
    m_pVideoConnector = nullptr;
    m_pControlConnector = nullptr;
    m_bSourceStopped = false;
    m_mediaFile[0] = L'\0';
}

CMFSrcProcessor::~CMFSrcProcessor()
{

}

HRESULT CMFSrcProcessor::Initilize(int nSrcNameLen, wchar_t *pSrcName, CDataConnector *pControlConnector, CDataConnector *pAudioConnector, CDataConnector *pVideoConnector)
{
    HRESULT hr = S_OK;

    if (!pSrcName)
    {
        return E_POINTER;
    }

    if (nSrcNameLen > MAX_FILE_LEN - 1)
    {
        return E_BOUNDS;
    }

    memcpy_s(m_mediaFile, sizeof(m_mediaFile), pSrcName, nSrcNameLen * sizeof(pSrcName[0]));
    m_mediaFile[nSrcNameLen] = L'\0';

    m_pAudioConnector = pAudioConnector;
    m_pVideoConnector = pVideoConnector;
    m_pControlConnector = pControlConnector;
    return hr;
}

HRESULT CMFSrcProcessor::Start()
{
    HRESULT hr = S_OK;

    m_hSourceThread = CreateThread(NULL, 0, CMFSrcProcessor::StaticSourceThread, this, 0, NULL);
    DX::TIF((m_hSourceThread == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);

    return hr;
}

HRESULT CMFSrcProcessor::Stop()
{
    HRESULT hr = S_OK;
    
    m_bSourceStopped = true;
    
    return hr;
}

HRESULT CMFSrcProcessor::WaitToFinish(DWORD timeOut)
{
    HRESULT hr = S_OK;

    if (m_hSourceThread != nullptr)
    {
        DWORD dwWaitRes = WaitForSingleObject(m_hSourceThread, timeOut);
        if (dwWaitRes != WAIT_OBJECT_0)
        {
            hr = E_FAIL;
        }
        else
        {
            CloseHandle(m_hSourceThread);
            m_hSourceThread = nullptr;
        }
    }
    
    
    return hr;
}


DWORD WINAPI CMFSrcProcessor::StaticSourceThread(_In_ LPVOID lpParam)
{
    HRESULT hr = S_OK;
    CMFSrcProcessor *pDec = (CMFSrcProcessor *)lpParam;

    hr = pDec->SourceThread();

    return hr;
}

#define USE_INBOX_MBR
//#define USE_SSPK_SDK
HRESULT CMFSrcProcessor::SourceThread()
{
    HRESULT hr = S_OK;

    ComPtr<IMFSourceResolver> spResolver;
    ComPtr<IUnknown> spUnk;
    ComPtr<IMFMediaSource> m_spSource;

    ComPtr<IMFPresentationDescriptor> spPD;

    DWORD cStream = 0;
    DWORD nCreatedStream = 0;
    MF_OBJECT_TYPE outType;

    bool bFirstAudioStream = true;
    bool bFirstVideoStream = true;


#ifdef USE_INBOX_MBR
    Windows::Media::MediaExtensionManager ^ extensionManager = ref new Windows::Media::MediaExtensionManager();
    StringReference extensionType(TEXT("Windows.Xbox.Media.SmoothStreamingByteStreamHandler"));

    extensionManager->RegisterByteStreamHandler(extensionType, nullptr, StringReference(TEXT("text/xml")));
    extensionManager->RegisterByteStreamHandler(extensionType, nullptr, StringReference(TEXT("application/vnd.ms-sstr+xml")));
    extensionManager->RegisterByteStreamHandler(extensionType, StringReference(TEXT(".m3u8")), nullptr);
    extensionManager->RegisterByteStreamHandler(extensionType, StringReference(TEXT(".m3u")), nullptr);
    extensionManager->RegisterByteStreamHandler(extensionType, nullptr, StringReference(TEXT("application/x-mpegURL")));
    extensionManager->RegisterByteStreamHandler(extensionType, nullptr, StringReference(TEXT("audio/x-mpegURL")));
    extensionManager->RegisterByteStreamHandler(extensionType, nullptr, StringReference(TEXT("application/vnd.apple.mpegurl")));
#elif  defined(USE_SSPK_SDK)
    Windows::Media::MediaExtensionManager ^ extensionManager = ref new Windows::Media::MediaExtensionManager();
    StringReference extensionType(TEXT("Microsoft.Media.AdaptiveStreaming.SmoothByteStreamHandler"));

    extensionManager->RegisterByteStreamHandler(extensionType, StringReference(TEXT(".ism")), StringReference(TEXT("text/xml")));
    extensionManager->RegisterByteStreamHandler(extensionType, StringReference(TEXT(".ism")), StringReference(TEXT("application/vnd.ms-sstr+xml")));

    // Initialize Playready content protection system properties for SS SDK extension Media Source.
    //DX::TIF(InitializePRITAActivationPropertyStore(spPropertyStore.GetAddressOf()));
#endif

    DX::TIF(MFCreateSourceResolver(&spResolver));
    DX::TIF(spResolver->CreateObjectFromURL(m_mediaFile, MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE, nullptr, &outType, &spUnk));
    DX::TIF(spUnk.As(&m_spSource));

    
    DX::TIF(m_spSource->CreatePresentationDescriptor(&spPD));
    DX::TIF(spPD->GetStreamDescriptorCount(&cStream));

    for (DWORD iStream = 0; iStream < cStream; iStream++)
    {
        BOOL fSelected = FALSE;
        GUID guidMajorType;
        ComPtr<IMFStreamDescriptor> spSD;
        ComPtr<IMFMediaTypeHandler> spMediatypeHandler;
        DWORD dwStreamID = 0;

        DX::TIF(spPD->GetStreamDescriptorByIndex(iStream, &fSelected, &spSD));
        DX::TIF(spSD->GetMediaTypeHandler(&spMediatypeHandler));
        DX::TIF(spSD->GetStreamIdentifier(&dwStreamID));
        DX::TIF(spMediatypeHandler->GetMajorType(&guidMajorType));
        DX::TIF(spPD->SelectStream(iStream));
    }
    
    {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0LL;
        DX::TIF(m_spSource->Start(spPD.Get(), NULL, &var));
        PropVariantClear(&var);
    }

    m_bRunning = true;
    // *
    while (!m_bIsEOF )
    {
        MediaEventType met;
        HRESULT hrStatus;

        ComPtr<IMFMediaEvent> spEvent;
        hr = m_spSource->GetEvent(0, &spEvent);

        if (m_bSourceStopped)
        {
            break;
        }

        if (hr == MF_E_NO_EVENTS_AVAILABLE)
        {
            Sleep(1);
            //Nothing in source queue just continue
            continue;
        }
        else if (FAILED(hr))
        {
            //Any other error break out;
            break;
        }

        spEvent->GetStatus(&hrStatus);
        DX::TIF(hrStatus);
        DX::TIF(spEvent->GetType(&met));

        switch (met)
        {
            case MENewStream:
            case MEUpdatedStream:
                {
                    ComPtr<IMFMediaStream> spStream;
                    ComPtr<IMFStreamDescriptor> spSD;
                    ComPtr<IMFMediaTypeHandler> spMediatypeHandler;
                    ComPtr<IMFMediaType> spMediaType;

                    GUID majorType;

                    PROPVARIANT varEvent;

                    DX::TIF(spEvent->GetValue(&varEvent));
                    hr = varEvent.punkVal->QueryInterface(IID_IMFMediaStream, &spStream);
                    PropVariantClear(&varEvent);
                    DX::TIF(hr);

                    DX::TIF(spStream->GetStreamDescriptor(&spSD));
                    DX::TIF(spSD->GetMediaTypeHandler(&spMediatypeHandler));
                    DX::TIF(spMediatypeHandler->GetCurrentMediaType(&spMediaType));

                    DX::TIF(spMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType));

                    if (majorType == MFMediaType_Video)
                    {
                        if (bFirstVideoStream)
                        {
                            ComPtr<IUnknown> spData;
                            DX::TIF(spMediaType.As(&spData));

                            DX::TIF(m_pControlConnector->WriteData(DATA_MEDIA_TYPE, spData.Get(), INFINITE));
                            m_pStreams[nCreatedStream].pDataConnecor = m_pVideoConnector;
                        }
                        bFirstVideoStream = false;
                    }
                    else if (majorType == MFMediaType_Audio)
                    {
                        if (bFirstAudioStream)
                        {
                            ComPtr<IUnknown> spData;
                            DX::TIF(spMediaType.As(&spData));

                            DX::TIF(m_pControlConnector->WriteData(DATA_MEDIA_TYPE, spData.Get(), INFINITE));
                            m_pStreams[nCreatedStream].pDataConnecor = m_pAudioConnector;
                        }
                        bFirstAudioStream = false;
                    }
                         

                    m_pStreams[nCreatedStream].pThis = this;
                    m_pStreams[nCreatedStream].pStream = spStream.Detach();
                    

                    m_hStreamThread[nCreatedStream] = CreateThread(NULL, 0, CMFSrcProcessor::StaticStreamThread, &m_pStreams[nCreatedStream], 0, NULL);
                    DX::TIF((m_hStreamThread[nCreatedStream] == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);

                    nCreatedStream++;
                }
               
                break;
            case MESourceStarted:
            case MESourceSeeked:
                break;

            case MEEndOfPresentation:
                m_bIsEOF = true;
                break;
            case MESourceStopped:
                break;
        }
    }

    DX::TIF(m_spSource->Stop());
    DX::TIF(m_spSource->Shutdown());

    for (DWORD idx = 0; idx < nCreatedStream; idx++)
    {
        //Wait for all stream to complete
        WaitForSingleObject(m_hStreamThread[idx], INFINITE);
        CloseHandle(m_hStreamThread[idx]);
    }
    // * /
    m_bRunning = false;
    return hr;
}


DWORD WINAPI CMFSrcProcessor::StaticStreamThread(_In_ LPVOID lpParam)
{
    HRESULT hr = S_OK;
    StreamData *pData = (StreamData *)lpParam;

    hr = pData->pThis->StreamThread(pData->pStream, pData->pDataConnecor);

    return hr;
}


HRESULT CMFSrcProcessor::StreamThread(IMFMediaStream *pStream, CDataConnector *pDataConnector)
{
    HRESULT hr = S_OK;
    bool isEOS = false;

    ComPtr<IMFMediaStream> spStream;
    
    spStream.Attach(pStream);
    
    {
        ComPtr<IMFStreamDescriptor> spSD;
        ComPtr<IMFMediaTypeHandler> spMediatypeHandler;
        ComPtr<IMFMediaType> spMediaType;

        DX::TIF(spStream->GetStreamDescriptor(&spSD));
        DX::TIF(spSD->GetMediaTypeHandler(&spMediatypeHandler));
        DX::TIF(spMediatypeHandler->GetCurrentMediaType(&spMediaType));

        if (pDataConnector)
        {
            //TODO: send mediatype info through connector 
            //pDataConnector->WriteData(DATA_SAMPLE, )
        }
    }
    
    while (!isEOS)
    {
        HRESULT hrStatus;
        MediaEventType met;
        ComPtr<IMFMediaEvent> spEvent;

        hr = spStream->GetEvent(0, &spEvent);
        if (hr == MF_E_MEDIA_SOURCE_WRONGSTATE || hr == MF_E_SHUTDOWN)
        {
            // source stopped or shutdowned
            // hence exit
            isEOS = true;
            hr = S_OK;
            break;
        }

        if (FAILED(hr))
        {
            hr = S_OK;
            break;
        }
        DX::TIF(hr);

        spEvent->GetStatus(&hrStatus);
        DX::TIF(hrStatus);
        DX::TIF(spEvent->GetType(&met));

        switch (met)
        {
            case MEStreamStarted:
            case MEStreamSeeked:
                DX::TIF(spStream->RequestSample(nullptr));
                break;

            case MEMediaSample:
                {
                    
                    ComPtr<IMFSample> spSample;
                    PROPVARIANT varEvent;
                    DX::TIF(spEvent->GetValue(&varEvent));
                    hr = varEvent.punkVal->QueryInterface(IID_PPV_ARGS(&spSample));
                    PropVariantClear(&varEvent);
                    DX::TIF(hr);

                    
                    hr = spStream->RequestSample(nullptr);
                    
                    if (hr == MF_E_MEDIA_SOURCE_WRONGSTATE || hr == MF_E_SHUTDOWN || hr == MF_E_END_OF_STREAM)
                    {
                        // source stopped or shutdowned
                        // hence exit
                        isEOS = true;
                        hr = S_OK;
                        break;
                    }
                    DX::TIF(hr);

                    if (pDataConnector)
                    {
                        pDataConnector->WriteData(DATA_SAMPLE, spSample.Get(), INFINITE);
                    }
                    
                    
                }
                break;

            case MEEndOfStream:
                pDataConnector->WriteData(DATA_COMPLETE_ALL_FRAMES, spEvent.Get(), INFINITE);
                isEOS = true;
                break;
        }
    }


    return hr;
}