#include "pch.h"
#include <DataConnector.h>
#include <AVRender.h>
#include <mmdeviceapi.h>

#include <Audiosessiontypes.h>


using namespace Windows::Storage::Streams;
using namespace Windows::Graphics::Display::Core;
using namespace Windows::Graphics::Display;

//--------------------------------------------------------------------------------------  
// Name: ActivationCompletionDelegate  
// Desc: Used with ActivateAudioInterfaceAsync() to activate IAudioClient  
//--------------------------------------------------------------------------------------  
class ActivationCompletionDelegate : public RuntimeClass < RuntimeClassFlags < ClassicCom >, FtmBase, IActivateAudioInterfaceCompletionHandler >
{
private:
    HANDLE                                 m_hCompletionEvent;
    IActivateAudioInterfaceAsyncOperation* m_spOperation;

public:
    ActivationCompletionDelegate(HANDLE hCompletedEvent) : m_hCompletionEvent(hCompletedEvent) {}
    ~ActivationCompletionDelegate() {}

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
    {
        if (NULL == operation)
        {
            return E_POINTER;
        }

        m_spOperation = operation;
        m_spOperation->AddRef();

        SetEvent(m_hCompletionEvent);
        return S_OK;
    }

    STDMETHODIMP GetOperationInterface(IActivateAudioInterfaceAsyncOperation** ppOperation)
    {
        if (NULL == ppOperation)
        {
            return E_POINTER;
        }

        if (m_spOperation == NULL)
        {
            return E_FAIL;
        }
        else
        {
            *ppOperation = m_spOperation;
        }

        return S_OK;
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void DbgPrint(char* pLogStr, ...)
{
    char str[4096];

    va_list ArgList;
    va_start(ArgList, pLogStr);

    static LARGE_INTEGER liFreq = {};
    if (0 == liFreq.QuadPart)
    {
        QueryPerformanceFrequency(&liFreq);
    }

    LARGE_INTEGER liNow = {};
    QueryPerformanceCounter(&liNow);
    LONGLONG llNowMS = (liNow.QuadPart * 1000) / liFreq.QuadPart;

    char* pBuf = str;

    int iLen = sprintf_s(pBuf, 4096, "%I64d ", llNowMS);

    pBuf = pBuf + iLen;
    vsprintf_s(pBuf, 4096 - iLen, pLogStr, ArgList);

    OutputDebugStringA(str);

    va_end(ArgList);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VideoRenderer Class Object implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AVRender::AVRender()
{
    m_hVideoRenderThread = NULL;
    m_hAudioRenderThread = NULL;
    m_fRunning = false;

    m_hFirstVideoSample = NULL;
    m_hFirstAudioSample = NULL;

    memset(&m_videoFrameStat, 0, sizeof(m_videoFrameStat));
    m_lastStatPos = 0;
    m_pNV12VideoConnector = nullptr;

    m_swapchainColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709; //Default to BT 709 color space for HD contents


    m_frameRate = 0;
    m_colorSpace = 0;
    m_height = 0;
    m_width = 0;
    m_HDR = 0;

    m_hnsLastVideoTime = 0;
    m_hnsLastAudioTime = 0;

    m_hAudioData = NULL;

    m_bForceSDR = FALSE;
    m_hnsAudioQPCTime = 0;
    m_audioStreamTime = 0;

    m_videoFrameRate = 0;
    m_exactFrameRate = 60.0f; //TODO: get actual framerate
    m_deltaFrameTime = 0.0f;
    m_syncPresentCount = 0;

    memset(&m_audioFormat, 0, sizeof(m_audioFormat));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AVRender::~AVRender()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT AVRender::Initialize(ID3D11Device1 *pD3d11Device1, CDataConnector *pNV12VideoConnector, CDataConnector *pPCMAudioConnector, ABI::Windows::UI::Composition::ISpriteVisual* pVideoSpriteVisual, ABI::Windows::UI::Composition::IVisual* pBaseVisual,
    ABI::Windows::UI::Composition::ICompositor* pCompositorInterface, ABI::Windows::UI::Composition::ICompositorInterop* pCompositorNative)
{
    m_fRunning = true;
    //m_fVideoPrerollEnd = false;

    m_spVideospriteVisualNative = pVideoSpriteVisual;
    m_spBaseVisualNative = pBaseVisual;
    m_compositorInterface = pCompositorInterface;
    m_compositorNative = pCompositorNative;

    m_spD3DDevice = pD3d11Device1;
    m_pNV12VideoConnector = pNV12VideoConnector;
    m_pPCMAudioConnector = pPCMAudioConnector;

    // And get the corresponding device context in the same way.
    m_spD3DDevice->GetImmediateContext1(&m_spImmCtx);

    // First, retrieve the underlying DXGI Device from the D3D Device
    Microsoft::WRL::ComPtr<IDXGIDevice1>  spDxgiDevice;
    DX::TIF(m_spD3DDevice.As(&spDxgiDevice));

    spDxgiDevice->SetMaximumFrameLatency(sc_dwSwapChainBackBuffers);

    // Identify the physical adapter (GPU or card) this device is running on.
    Microsoft::WRL::ComPtr<IDXGIAdapter> spDxgiAdapter;
    DX::TIF(spDxgiDevice->GetAdapter(&spDxgiAdapter));

    DX::TIF(spDxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &m_spDxgiFactory));

    DX::TIF(spDxgiAdapter->EnumOutputs(0, &m_spDxgiOutput));

    DX::TIF(m_spD3DDevice.As(&m_spVideoDevice));
    DX::TIF(m_spImmCtx.As(&m_spD3D11VideoContext));
    

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC vpcdesc = {};
    vpcdesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    vpcdesc.InputFrameRate.Numerator = 60;
    vpcdesc.InputFrameRate.Denominator = 1;
    vpcdesc.InputWidth = 1920;
    vpcdesc.InputHeight = 1080;
    vpcdesc.OutputFrameRate.Numerator = 60;
    vpcdesc.OutputFrameRate.Denominator = 1;
    vpcdesc.OutputWidth = 3840;
    vpcdesc.OutputHeight = 2160;
    vpcdesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    DX::TIF(m_spVideoDevice->CreateVideoProcessorEnumerator(&vpcdesc, &m_spD3D11VideoProcEnum));
    DX::TIF(m_spVideoDevice->CreateVideoProcessor(m_spD3D11VideoProcEnum.Get(), 0, &m_spD3D11VideoProc));

    m_spD3D11VideoContext->VideoProcessorSetStreamFrameFormat(m_spD3D11VideoProc.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

    //m_nProcessedFrame = 0;
    ConfigureSwapChain();
    m_hVideoRenderThread = CreateThread(NULL, 0, RenderVideoThreadProc, this, 0, NULL);
    m_hAudioRenderThread = CreateThread(NULL, 0, RenderAudioThreadProc, this, 0, NULL);

    m_hFirstVideoSample = CreateSemaphore(NULL, 0, 1, NULL);
    DX::TIF((m_hFirstVideoSample == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);

    m_hFirstAudioSample = CreateSemaphore(NULL, 0, 1, NULL);
    DX::TIF((m_hFirstVideoSample == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);

    QueryPerformanceFrequency(&m_QPCFrequency);

    InitializeCriticalSection(&m_audioDataSync);

    return S_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT AVRender::Shutdown()
{
    m_fRunning = false;
    m_spVideospriteVisualNative = nullptr;
    m_spBaseVisualNative = nullptr;
    m_compositorInterface = nullptr;
    m_compositorNative = nullptr;

    WaitForSingleObjectEx(m_hAudioRenderThread, INFINITE, FALSE);
    WaitForSingleObjectEx(m_hVideoRenderThread, INFINITE, FALSE);

    DeleteCriticalSection(&m_audioDataSync);
    
    return S_OK;
}

void AVRender::DrawVideoSample(IMFSample * pCurrentSample)
{
    UINT subRes = 0;

    D3D11_TEXTURE2D_DESC nv12TextureDesc;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputdesc = {};
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
    D3D11_VIDEO_PROCESSOR_STREAM vpStream = {};

    ComPtr<ID3D11Texture2D> pBackTexture;
    
    ComPtr<IMFMediaBuffer> pBuffer;
    ComPtr<IMFDXGIBuffer> pDXGIBuffer;
    ComPtr<ID3D11Texture2D> pNV12Texture;

    ComPtr<ID3D11VideoProcessorInputView> pInputView;
    ComPtr<ID3D11VideoProcessorOutputView> pOutputView;

    DX::TIF(pCurrentSample->GetSampleTime(&m_hnsLastVideoTime));

    DX::TIF(m_spSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackTexture)));
    
    DX::TIF(pCurrentSample->GetBufferByIndex(0, &pBuffer));
    if (FAILED(pBuffer.As(&pDXGIBuffer)))
    {
        // Software decoder, copy decoded data to texture
        DX::TIF(CopyToTexture(pBuffer.Get()));
        pNV12Texture = m_pSWDecOutTexture;
        subRes = 0;
    }
    else
    {
        pDXGIBuffer->GetResource(IID_PPV_ARGS(&pNV12Texture));
        pDXGIBuffer->GetSubresourceIndex(&subRes);
    }

    pNV12Texture->GetDesc(&nv12TextureDesc);
    nv12TextureDesc.ArraySize = 1;

    inputdesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputdesc.Texture2D.MipSlice = 0;
    inputdesc.Texture2D.ArraySlice = subRes;

    DX::TIF(m_spVideoDevice->CreateVideoProcessorInputView(pNV12Texture.Get(), m_spD3D11VideoProcEnum.Get(), &inputdesc, &pInputView));

    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;
    DX::TIF(m_spVideoDevice->CreateVideoProcessorOutputView(pBackTexture.Get(), m_spD3D11VideoProcEnum.Get(), &outputDesc, &pOutputView));

    vpStream.Enable = TRUE;
    vpStream.pInputSurface = pInputView.Get();
    DX::TIF(m_spD3D11VideoContext->VideoProcessorBlt(m_spD3D11VideoProc.Get(), pOutputView.Get(), 0, 1, &vpStream));
}

DWORD WINAPI AVRender::RenderAudioThreadProc(LPVOID pParam)
{
    AVRender* pRenderer = static_cast<AVRender*>(pParam);

    pRenderer->AudioRenderLoop();

    return 0;

}

void AVRender::ToggleHDRState()
{
    m_bForceSDR = !m_bForceSDR;
    if (m_bForceSDR)
    {
        m_spD3D11VideoContext->VideoProcessorSetOutputColorSpace1(m_spD3D11VideoProc.Get(), DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709);
        m_spSwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709);
    }
    else
    {
        m_spD3D11VideoContext->VideoProcessorSetOutputColorSpace1(m_spD3D11VideoProc.Get(), m_VPOutColorSpace);
        m_spSwapChain->SetColorSpace1(m_SCInColorSpace);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DWORD WINAPI AVRender::RenderVideoThreadProc(LPVOID pParam)
{
    AVRender* pRenderer = static_cast<AVRender*>(pParam);

    pRenderer->VideoRenderLoop();

    return 0;
}

LONGLONG videoSampleTime;
LONGLONG audioSampleTime;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::VideoRenderLoop()
{
    HRESULT hr = S_OK;
    UINT32 nSample = 0;
    wchar_t str[1024] = L"";

    ComPtr<IMFSample> pNextSample;
    ComPtr<IMFSample> pLastSample;
    ComPtr<IMFMediaType> spPendingMediaType;

    while (m_fRunning)
    {
        LONGLONG nextSampleTime = 0;
        UINT nDroppedFrame = 0;
        DXGI_FRAME_STATISTICS frameStat = {};

        //wait for initial data
        while (nSample == 0 && m_fRunning)
        {
            ComPtr<IUnknown> spVideoData;
            eDataType dataType;
            hr = m_pNV12VideoConnector->ReadData(&dataType, &spVideoData, 1000);
            if (SUCCEEDED(hr) && dataType == DATA_MEDIA_TYPE)
            {
                ComPtr<IMFMediaType> spMediaType;
                DX::TIF(spVideoData.As(&spMediaType));

                ConfigureColorSpaceDisplay(spMediaType.Get());
                CreateSWDecoderOutputTexture(spMediaType.Get());
            }
            else if (SUCCEEDED(hr) && dataType == DATA_SAMPLE)
            {
                DX::TIF(spVideoData.As(&pNextSample));
                DX::TIF(pNextSample->GetSampleTime(&nextSampleTime));

                //Signal video sample receieved
                ReleaseSemaphore(m_hFirstVideoSample, 1, NULL);

                //wait for auido sample if present
                DWORD waitObj = WaitForSingleObject(m_hFirstAudioSample, 1000);
                if (waitObj != WAIT_OBJECT_0)
                {
                    //NO audio signal
                }

                nSample++;
            }
        }

        while (m_fRunning)
        {
            ComPtr<IUnknown> spVideoData;
            eDataType dataType;
            hr = m_pNV12VideoConnector->ReadData(&dataType, &spVideoData, INFINITE);

            if (SUCCEEDED(hr) && dataType == DATA_MEDIA_TYPE)
            {
                ComPtr<IMFMediaType> spMediaType;
                DX::TIF(spVideoData.As(&spMediaType));

                // Consecutive media type; clean old one
                spPendingMediaType = nullptr;
                spPendingMediaType = spMediaType;
            }
            else if (SUCCEEDED(hr) && dataType == DATA_SAMPLE)
            {
                ComPtr<IMFSample> pCurrentSample;
                LONGLONG sampleTime = 0;
                INT32 nFrameDealy = 0;

                pLastSample = nullptr;
                pLastSample = pCurrentSample;

                pCurrentSample = pNextSample;
                sampleTime = nextSampleTime;
                pNextSample = nullptr;
                nextSampleTime = 0;

                DX::TIF(spVideoData.As(&pNextSample));
                DX::TIF(pNextSample->GetSampleTime(&nextSampleTime));

                if (!pCurrentSample)
                {
                    if (spPendingMediaType)
                    {
                        ConfigureColorSpaceDisplay(spPendingMediaType.Get());
                        CreateSWDecoderOutputTexture(spPendingMediaType.Get());
                        spPendingMediaType = nullptr;
                    }
                    continue;
                }

                nFrameDealy = 1;
                DX::TIF(ComputeVideoWait(&frameStat, &nFrameDealy, sampleTime, nextSampleTime));
                
                if (nFrameDealy < 0)
                {
                    //Video is lagging; drop frame
                    if (m_lastStatPos > 0)
                    {
                        m_lastStatPos--;
                    }
                    nDroppedFrame++;
                    if (spPendingMediaType)
                    {
                        ConfigureColorSpaceDisplay(spPendingMediaType.Get());
                        CreateSWDecoderOutputTexture(spPendingMediaType.Get());
                        spPendingMediaType = nullptr;
                    }
                    continue;
                }
                else
                {
                    //Present interval can upto 4; hence if more than 4 frame
                    //present multiple time
                    for (int i = 0; i < nFrameDealy; i += 4)
                    {
                        int nFrameToDisplay = min(nFrameDealy - i, 4);
                        DrawVideoSample(pCurrentSample.Get());
                        Present(false, (UINT)nFrameToDisplay);
                    }

                    hr = m_spSwapChain->GetFrameStatistics(&frameStat);
                    if (FAILED(hr))
                    {
                        memset(&frameStat, 0, sizeof(frameStat));
                        hr = S_OK;
                    }
                }

                nSample++;
                if (spPendingMediaType)
                {
                    ConfigureColorSpaceDisplay(spPendingMediaType.Get());
                    CreateSWDecoderOutputTexture(spPendingMediaType.Get());
                    spPendingMediaType = nullptr;
                }
            }
            else if ((FAILED(hr)) || (SUCCEEDED(hr) && dataType == DATA_COMPLETE_ALL_FRAMES))
            {
                m_hnsLastVideoTime = 0x7fffffffffffffff;
                m_deltaFrameTime = 0;
                m_syncPresentCount = 0;
                m_lastStatPos = 0;
                break;
            }
        }
        DrawVideoSample(pNextSample.Get());
        Present(false, 1);
        
        swprintf_s(str, 1024, L"Dropped frame count %d \n", nDroppedFrame);
        OutputDebugString(str);
        m_deltaFrameTime = 0;
        m_syncPresentCount = 0;
        m_lastStatPos = 0;
    }
}

void AVRender::AudioRenderLoop()
{
    HRESULT hr = S_OK;
    DWORD waitResult = 0;

    while (m_fRunning)
    {
        ComPtr<IUnknown> spAudioDataInitial;
        ComPtr<IAudioRenderClient> spRenderClient;
        eDataType dataType;
        UINT32 nAudioFrame = 0;
        UINT32 nAudioFramePadding = 0;
        UINT32 nAudioFrameLeft = 0;
        UINT32 nAudioFrameFromDecoder = 0;
        BYTE *pData;
        UINT64 nTotalAudioFrame = 0;
        
        hr = m_pPCMAudioConnector->ReadData(&dataType, &spAudioDataInitial, 1000);

        if (SUCCEEDED(hr) && dataType == DATA_MEDIA_TYPE)
        {
            
            ComPtr<IMFMediaType> spMediaType;
            DX::TIF(spAudioDataInitial.As(&spMediaType));
            spAudioDataInitial = nullptr;

            ConfigureAudio(spMediaType.Get());
            m_spAudioClient->GetService(IID_PPV_ARGS(&spRenderClient));
            m_spAudioClient->GetBufferSize(&nAudioFrame);

            hr = m_pPCMAudioConnector->ReadData(&dataType, &spAudioDataInitial, 1000);
            if (SUCCEEDED(hr) && dataType == DATA_SAMPLE)
            {
                //play first audio
                ComPtr<IMFSample> pCurrentSample;
                ComPtr<IMFMediaBuffer> spMediaBuffer;
                DWORD audioDataLength = 0;
                DWORD maxAudioDataLength = 0;
                BYTE *pAudioData = nullptr;

                DX::TIF(spAudioDataInitial.As(&pCurrentSample));
                spAudioDataInitial = nullptr;
                DX::TIF(pCurrentSample->GetSampleTime(&m_hnsLastAudioTime));
                //audioSampleTime = m_hnsLastAudioTime;

                ReleaseSemaphore(m_hFirstAudioSample, 1, NULL);

                //wait for auido sample if present
                DWORD waitObj = WaitForSingleObject(m_hFirstVideoSample, 1000);
                if (waitObj != WAIT_OBJECT_0)
                {
                    //NO video stream
                }

                DX::TIF(pCurrentSample->ConvertToContiguousBuffer(&spMediaBuffer));
                
                DX::TIF(m_spAudioClient->GetCurrentPadding(&nAudioFramePadding));
                
                nAudioFrameLeft = nAudioFrame - nAudioFramePadding;
                DX::TIF(spRenderClient->GetBuffer(nAudioFrameLeft, &pData));

                DX::TIF(spMediaBuffer->Lock(&pAudioData, &maxAudioDataLength, &audioDataLength));

                nAudioFrameFromDecoder = audioDataLength /  m_audioFormat.Format.nBlockAlign;
                if (nAudioFrameFromDecoder <= nAudioFrameLeft)
                {
                    CopyMemory(pData, pAudioData, audioDataLength);
                }
                else
                {
                    DX::TIF(E_UNEXPECTED);
                }

                DX::TIF(spMediaBuffer->Unlock());
                nTotalAudioFrame += nAudioFrameFromDecoder;

                DX::TIF(spRenderClient->ReleaseBuffer(nAudioFrameFromDecoder, 0));
            }

        }
        else
        {
            // audio device is not configured yet
            continue;
        }

        //Try to match audio start with vblank for minimum audio video delay
        m_spDxgiOutput->WaitForVBlank();

        DX::TIF(m_spAudioClient->Start());
        while (m_fRunning)
        {
            ComPtr<IUnknown> spAudioData;
            hr = m_pPCMAudioConnector->ReadData(&dataType, &spAudioData, 40);
            if (SUCCEEDED(hr) && dataType == DATA_SAMPLE)
            {
                ComPtr<IMFMediaBuffer> spMediaBuffer;
                ComPtr<IMFSample> pCurrentSample;
                DX::TIF(spAudioData.As(&pCurrentSample));

                DX::TIF(pCurrentSample->GetSampleTime(&m_hnsLastAudioTime));
                DX::TIF(pCurrentSample->ConvertToContiguousBuffer(&spMediaBuffer));

                //play audio
                waitResult = WaitForSingleObject(m_hAudioData, 40);
                if (waitResult != WAIT_OBJECT_0)
                {
                    //TODO: add scilence
                    DX::TIF(E_UNEXPECTED);
                }
                else
                {
                    DWORD audioDataLength = 0;
                    DWORD maxAudioDataLength = 0;
                    BYTE *pAudioData = nullptr;

                    DX::TIF(m_spAudioClient->GetCurrentPadding(&nAudioFramePadding));
                    nAudioFrameLeft = nAudioFrame - nAudioFramePadding;
                    
                    DX::TIF(spMediaBuffer->Lock(&pAudioData, &maxAudioDataLength, &audioDataLength));
                    nAudioFrameFromDecoder = audioDataLength / m_audioFormat.Format.nBlockAlign;

                    while (nAudioFrameFromDecoder > nAudioFrameLeft)
                    {
                        //less space than decoder data. wait again
                        waitResult = WaitForSingleObject(m_hAudioData, 40);
                        DX::TIF(m_spAudioClient->GetCurrentPadding(&nAudioFramePadding));
                        nAudioFrameLeft = nAudioFrame - nAudioFramePadding;
                    }

                    DX::TIF(spRenderClient->GetBuffer(nAudioFrameLeft, &pData));

                    CopyMemory(pData, pAudioData, audioDataLength);

                    DX::TIF(spMediaBuffer->Unlock());

                    DX::TIF(spRenderClient->ReleaseBuffer(nAudioFrameFromDecoder, 0));

                    DX::TIF(ComputeAudioSyncTime(m_hnsLastAudioTime, nTotalAudioFrame));
                    nTotalAudioFrame += nAudioFrameFromDecoder;
                }
            }
            else
            {
                break;
            }
        }

        // Release audio device
        m_spAudioClient->Stop();
        m_spAudioClient = nullptr;

        //playback completed; reset audio time
        EnterCriticalSection(&m_audioDataSync);
        m_hnsAudioQPCTime = 0;
        m_audioStreamTime = 0;
        LeaveCriticalSection(&m_audioDataSync);
    }

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::Flush()
{
    // reset clock
    memset(&m_videoFrameStat, 0, sizeof(m_videoFrameStat));
    m_lastStatPos = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::Reset()
{
    Flush();

    if (m_spImmCtx.Get())
    {
        m_spImmCtx->Flush();
    }

    memset(&m_videoFrameStat, 0, sizeof(m_videoFrameStat));
    m_lastStatPos = 0;

    // clear video display
    ComPtr<ID3D11Texture2D> spBackBuffer;
    ComPtr<ID3D11RenderTargetView>  spRenderTargetView;
    DX::TIF(m_spSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)spBackBuffer.ReleaseAndGetAddressOf()));
    DX::TIF(m_spD3DDevice->CreateRenderTargetView(spBackBuffer.Get(), nullptr, spRenderTargetView.ReleaseAndGetAddressOf()));

    const float clearColor[] = { 1.00f, 0.00f, 0.00f, 0.00f };
    m_spImmCtx->OMSetRenderTargets(1, spRenderTargetView.GetAddressOf(), NULL);
    m_spImmCtx->ClearRenderTargetView(spRenderTargetView.Get(), clearColor);

    m_spSwapChain->Present(0, 0);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
HRESULT AVRender::ConfigureSwapChain()
{
    HRESULT hr = S_OK;

    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    ComPtr<IDXGISwapChain1> spSwapchain;

    // Cretae swap chain
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.Flags = 0;
    scd.BufferCount = 3;
    scd.Format = DXGI_FORMAT_R10G10B10A2_UNORM; //DXGI_FORMAT_B8G8R8A8_UNORM; //DXGI_FORMAT_R10G10B10A2_UNORM;
    scd.Width = 3840; //1920; // 3840;
    scd.Height = 2160; //1080; // 2160;

    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Stereo = FALSE;

    DX::TIF(m_spDxgiFactory->CreateSwapChainForComposition(m_spD3DDevice.Get(), &scd, nullptr, &spSwapchain));

    DX::TIF(spSwapchain.As(&m_spSwapChain));
    UpdateVideoSwapChainVisual(m_spSwapChain.Get());


    UINT nModes = 0;
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, NULL));

    DXGI_MODE_DESC *pDisplayModes = new DXGI_MODE_DESC[nModes];
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, pDisplayModes));

    // Properties directly on the visual
    Vector3 VisualOffset = { 0.0f, 0.0f, 0.0f };
    Vector2 VisualSize = { float(scd.Width), float(scd.Height) };
    // Set VisualScale for fullscreen
    Vector3 VisualScale = { static_cast<float>(pDisplayModes[0].Width) / static_cast<float>(scd.Width), static_cast<float>(pDisplayModes[0].Height) / static_cast<float>(scd.Height), 1.0f };

    m_spBaseVisualNative->put_Size(VisualSize);
    m_spBaseVisualNative->put_Offset(VisualOffset);
    m_spBaseVisualNative->put_Scale(VisualScale);

    delete[] pDisplayModes;
    return (hr);
}

HRESULT AVRender::ConfigureAudio(IMFMediaType *pAudioType)
{
    HRESULT hr = S_OK;
    REFERENCE_TIME hnsRequestedDuration = 800000ll; //hns time 80ms

    ComPtr<IUnknown> unknownInterface;
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    ComPtr<ActivationCompletionDelegate> completionObject;
    ComPtr<IActivateAudioInterfaceCompletionHandler> completionHandler;

    // Query the defauilt render device ID      
    Platform::String^ id = Windows::Media::Devices::MediaDevice::GetDefaultAudioRenderId(Windows::Media::Devices::AudioDeviceRole::Default);

    // Create the completion event for ActivateAudioInterfaceAsync()       
    HANDLE completionEvent = CreateEventEx(NULL, L"", NULL, EVENT_ALL_ACCESS);
    DX::TIF((completionEvent == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);
    completionObject = Make<ActivationCompletionDelegate>(completionEvent);
    DX::TIF(completionObject.As(&completionHandler));

    // Activate the default audio interface      
    DX::TIF(ActivateAudioInterfaceAsync(id->Data(), __uuidof(IAudioClient), NULL, completionHandler.Get(), &operation));

    // Wait for the async operation to complete          
    WaitForSingleObjectEx(completionEvent, INFINITE, FALSE);

    DX::TIF(completionObject->GetOperationInterface(operation.ReleaseAndGetAddressOf()));

    // Verify that the interface was activated      
    HRESULT hrActivated = S_OK;
    DX::TIF(operation->GetActivateResult(&hrActivated, &unknownInterface));
    DX::TIF(hrActivated);

    //clear old ones
    m_spAudioClient = nullptr;
    m_spAudioRenderClient = nullptr;
    m_spAudioClock = nullptr;

    // Return the IAudioClient interface      
    DX::TIF(unknownInterface.As(&m_spAudioClient));
    CloseHandle(completionEvent);

    m_hAudioData = CreateEventEx(NULL, L"", NULL, EVENT_ALL_ACCESS);

    GetWaveFormat(pAudioType, &m_audioFormat);

    DX::TIF(m_spAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hnsRequestedDuration, 0, (WAVEFORMATEX *)&m_audioFormat, NULL));
    DX::TIF(m_spAudioClient->SetEventHandle(m_hAudioData));

    DX::TIF(m_spAudioClient->GetService(_uuidof(IAudioRenderClient), (void**)&m_spAudioRenderClient));
    DX::TIF(m_spAudioClient->GetService(_uuidof(IAudioClock), (void**)&m_spAudioClock));

    DX::TIF(m_spAudioClock->GetFrequency(&m_audioFrequesncy));

    return hr;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::Present(bool fRepeat, UINT syncInterval)
{


    DXGI_PRESENT_PARAMETERS parameters = { 0 };
    // The first argument instructs DXGI to block until VSync, putting the application
    // to sleep until the next VSync. This ensures we don't waste any cycles rendering
    // frames that will never be displayed to the screen.
    (void)m_spSwapChain->Present1(syncInterval, 0, &parameters);
}

typedef struct _MT_CUSTOM_VIDEO_PRIMARIES {
    float fRx;
    float fRy;
    float fGx;
    float fGy;
    float fBx;
    float fBy;
    float fWx;
    float fWy;
} MT_CUSTOM_VIDEO_PRIMARIES;

DEFINE_GUID(MF_MT_CUSTOM_VIDEO_PRIMARIES,
    0x47537213, 0x8cfb, 0x4722, 0xaa, 0x34, 0xfb, 0xc9, 0xe2, 0x4d, 0x77, 0xb8);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::ReleaseVideoSwapChainVisual()
{
    m_compositionSurface = nullptr;
    m_surfaceBrush = nullptr;
    m_brush = nullptr;
}
//#pragma optimize("",off)
void AVRender::ConfigureColorSpaceDisplay(IMFMediaType * pVideoMediaType)
{
    UINT32 dwVideoPrimaries;
    UINT32 dwVideoTransferFuntion;
    UINT32 dwNumerator = 0, dwDenominator = 0;
    float fltFramerate = 60.0f;
    UINT32 nFrameRate = 60;
    MT_CUSTOM_VIDEO_PRIMARIES primaries;
    Windows::Graphics::Display::Core::HdmiDisplayHdr2086Metadata hdrMetaData = {};

    dwVideoPrimaries = MFGetAttributeUINT32(pVideoMediaType, MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    dwVideoTransferFuntion = MFGetAttributeUINT32(pVideoMediaType, MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    MFGetAttributeRatio(pVideoMediaType, MF_MT_FRAME_RATE, &dwNumerator, &dwDenominator);

    //dwVideoPrimaries = MFVideoPrimaries_BT2020;
    //dwVideoTransferFuntion = MFVideoTransFunc_2084;

    if (dwDenominator > 0)
    {
        fltFramerate = ((float)dwNumerator) / ((float)dwDenominator);
    }
    nFrameRate = ((UINT)((fltFramerate) + 0.5));
    m_videoFrameRate = fltFramerate;


    if (SUCCEEDED(pVideoMediaType->GetBlob(MF_MT_CUSTOM_VIDEO_PRIMARIES, (UINT8*)&primaries, sizeof(primaries), NULL)))
    {
        hdrMetaData.RedPrimaryX = (UINT16)(primaries.fRx * 50000.0f);
        hdrMetaData.RedPrimaryY = (UINT16)(primaries.fRy * 50000.0f);
        hdrMetaData.GreenPrimaryX = (UINT16)(primaries.fGx * 50000.0f);
        hdrMetaData.GreenPrimaryY = (UINT16)(primaries.fGy * 50000.0f);
        hdrMetaData.BluePrimaryX = (UINT16)(primaries.fBx * 50000.0f);
        hdrMetaData.BluePrimaryY = (UINT16)(primaries.fBy * 50000.0f);
        hdrMetaData.WhitePointX = (UINT16)(primaries.fWx * 50000.0f);
        hdrMetaData.WhitePointY = (UINT16)(primaries.fWy * 50000.0f);
    }

    UINT32 max_mastering_luminance = 0;
    if (SUCCEEDED(pVideoMediaType->GetUINT32(MF_MT_MAX_MASTERING_LUMINANCE, &max_mastering_luminance)))
    {
        hdrMetaData.MaxMasteringLuminance = max_mastering_luminance;
    }

    UINT32 min_mastering_luminance = 0;
    if (SUCCEEDED(pVideoMediaType->GetUINT32(MF_MT_MIN_MASTERING_LUMINANCE, &min_mastering_luminance)))
    {
        hdrMetaData.MinMasteringLuminance = min_mastering_luminance;
    }

    UINT32 max_content_luminance = 0;
    if (SUCCEEDED(pVideoMediaType->GetUINT32(MF_MT_MAX_LUMINANCE_LEVEL, &max_content_luminance)))
    {
        hdrMetaData.MaxContentLightLevel = (UINT16)max_content_luminance;
    }

    UINT32 max_frame_avg_luminance = 0;
    if (SUCCEEDED(pVideoMediaType->GetUINT32(MF_MT_MAX_FRAME_AVERAGE_LUMINANCE_LEVEL, &max_frame_avg_luminance)))
    {
        hdrMetaData.MaxFrameAverageLightLevel = (UINT16)max_frame_avg_luminance;
    }

    //if (nFrameRate == m_frameRate && dwVideoPrimaries == m_colorSpace && m_HDR == dwVideoTransferFuntion)
    //{
        //TODO: only look for metadata
        //return;
    //}

    if (dwVideoPrimaries == MFVideoPrimaries_BT2020)
    {
        if (dwVideoTransferFuntion == MFVideoTransFunc_2084)
        {
            m_spD3D11VideoContext->VideoProcessorSetStreamColorSpace1(m_spD3D11VideoProc.Get(), 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020);
        }
        else
        {
            m_spD3D11VideoContext->VideoProcessorSetStreamColorSpace1(m_spD3D11VideoProc.Get(), 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020);
        }
    }
    else
    {
        m_spD3D11VideoContext->VideoProcessorSetStreamColorSpace1(m_spD3D11VideoProc.Get(), 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    }

    HdmiDisplayInformation^ hdmiInfo = HdmiDisplayInformation::GetForCurrentView();
    //auto config = DisplayConfiguration::GetForCurrentView();
    auto modes = hdmiInfo->GetSupportedDisplayModes();
    HdmiDisplayMode^ maxMode = modes->GetAt(0);
    
    UINT32 selectedFrameRate = 0;
    UINT32 altFrameRate = ((UINT)((maxMode->RefreshRate) + 0.5));

    UINT32 selectedColorSpace = 0;
    UINT32 altColorSpace = MFVideoPrimaries_BT709;

    UINT32 selectedHDR = 0;
    UINT32 altHDR = MFVideoTransFunc_709;

    UINT32 selectedResWidth = 0;
    UINT32 selectedResHeight = 0;

    UINT32 maxBitsPerPixel = 24;

    UINT32 modeIndex = 0;

    //Find Matching frame rate first
    for (DWORD i = 0; i < modes->Size; i++)
    {
        HdmiDisplayMode^ mode = modes->GetAt(i);
        UINT32 refreshRate = ((UINT)((mode->RefreshRate) + 0.5));
        if (refreshRate == nFrameRate)
        {
            selectedFrameRate = refreshRate;
            break;
        }

        if (refreshRate >= nFrameRate && refreshRate <= altFrameRate)
        {
            altFrameRate = refreshRate;
        }
    }

    if (selectedFrameRate == 0)
    {
        //Didn't find matching frame rate; go with frame rate just bigger than selected
        selectedFrameRate = altFrameRate;
    }
    else
    {
        altFrameRate = selectedFrameRate;
    }

    //Find matching mode with same color space and HDR
    for (DWORD i = 0; i < modes->Size; i++)
    {
        HdmiDisplayMode^ mode = modes->GetAt(i);
        UINT32 refreshRate = ((UINT)((mode->RefreshRate) + 0.5));
        UINT32 colorspace = MFVideoPrimaries_BT709; // 0->709, 1->2020
        UINT32 hdr = MFVideoTransFunc_709;

        if (refreshRate == selectedFrameRate)
        {
            colorspace = (mode->ColorSpace == HdmiDisplayColorSpace::BT2020) ? MFVideoPrimaries_BT2020 : MFVideoPrimaries_BT709;
            hdr = mode->IsSmpte2084Supported ? MFVideoTransFunc_2084 : MFVideoTransFunc_709;

            if (colorspace == dwVideoPrimaries && hdr == dwVideoTransferFuntion)
            {
                selectedColorSpace = colorspace;
                selectedHDR = hdr;
            }

            if (hdr >= dwVideoTransferFuntion || colorspace >= dwVideoTransferFuntion)
            {
                altColorSpace = colorspace;
                altHDR = hdr;
            }
        }
    }

    if (selectedColorSpace == 0)
    {
        selectedColorSpace = altColorSpace;
        selectedHDR = altHDR;
    }

    //Find matching resolution
    for (DWORD i = 0; i < modes->Size; i++)
    {
        HdmiDisplayMode^ mode = modes->GetAt(i);
        UINT32 width = mode->ResolutionWidthInRawPixels;
        UINT32 height = mode->ResolutionHeightInRawPixels;
        UINT32 refreshRate = ((UINT)((mode->RefreshRate) + 0.5));
        UINT32 colorspace = MFVideoPrimaries_BT709; // 0->709, 1->2020
        UINT32 hdr = MFVideoTransFunc_709;
        UINT32 bitsPerPixel = mode->BitsPerPixel;

        if (refreshRate == selectedFrameRate)
        {
            colorspace = (mode->ColorSpace == HdmiDisplayColorSpace::BT2020) ? MFVideoPrimaries_BT2020 : MFVideoPrimaries_BT709;
            hdr = mode->IsSmpte2084Supported ? MFVideoTransFunc_2084 : MFVideoTransFunc_709;

            if (selectedColorSpace == colorspace && selectedHDR == hdr)
            {
                if (width > selectedResWidth)
                {
                    selectedResWidth = width;
                    selectedResHeight = height;
                    maxBitsPerPixel = bitsPerPixel;
                    modeIndex = i;
                }
                else if (width == selectedResWidth && bitsPerPixel >= maxBitsPerPixel)
                {
                    maxBitsPerPixel = bitsPerPixel;
                    modeIndex = i;
                }
            }
        }
    }

    if (selectedFrameRate != m_frameRate || m_colorSpace != selectedColorSpace || m_height != selectedResHeight || m_width != selectedResWidth || m_HDR != selectedHDR)
    {
        //Change TV mode only if needed

        HdmiDisplayMode^ matchedMode = modes->GetAt(modeIndex);
        HdmiDisplayHdrOption hdrOption = (selectedHDR == MFVideoTransFunc_2084) ? HdmiDisplayHdrOption::Eotf2084 : HdmiDisplayHdrOption::EotfSdr;
        hdmiInfo->RequestSetCurrentDisplayModeAsync(matchedMode, hdrOption, hdrMetaData);

        m_exactFrameRate = matchedMode->RefreshRate;
    }


    if (selectedColorSpace == MFVideoPrimaries_BT2020)
    {
        if (selectedHDR == MFVideoTransFunc_2084)
        {
            m_VPOutColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
            m_SCInColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
        }
        else
        {
            m_VPOutColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
            m_SCInColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
        }
    }
    else
    {
        m_VPOutColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
        m_SCInColorSpace = DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;
    }

    m_spD3D11VideoContext->VideoProcessorSetOutputColorSpace1(m_spD3D11VideoProc.Get(), m_VPOutColorSpace);
    m_spSwapChain->SetColorSpace1(m_SCInColorSpace);

    m_frameRate = selectedFrameRate;
    m_colorSpace = selectedColorSpace;
    m_height = selectedResHeight;
    m_width = selectedResWidth;
    m_HDR = selectedHDR;

    MFVideoArea videoArea = {};
    RECT rcSrc = {};
    UINT32 uiPanScanEnabled = 0;
    HRESULT hr = pVideoMediaType->GetUINT32(MF_MT_PAN_SCAN_ENABLED, &uiPanScanEnabled);
    if ((hr == S_OK) && uiPanScanEnabled)
    {
        hr = pVideoMediaType->GetBlob(MF_MT_PAN_SCAN_APERTURE,  (UINT8*)&videoArea, sizeof(MFVideoArea), NULL);
    }

    if (hr != S_OK)
    {
        hr = pVideoMediaType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&videoArea, sizeof(MFVideoArea), NULL);
    }

    if (S_OK == hr)
    {
        rcSrc.left = videoArea.OffsetX.value;
        rcSrc.right = videoArea.OffsetX.value + videoArea.Area.cx;
        rcSrc.top = videoArea.OffsetY.value;
        rcSrc.bottom = videoArea.OffsetY.value + videoArea.Area.cy;
    }
    else
    {
        UINT32 dwInputWidth = 0;
        UINT32 dwInputHeight = 0;
        rcSrc.left = 0;
        rcSrc.top = 0;
        MFGetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, &dwInputWidth, &dwInputHeight);
        rcSrc.right = rcSrc.left + dwInputWidth;
        rcSrc.bottom = rcSrc.top + dwInputHeight;
    }

    m_spD3D11VideoContext->VideoProcessorSetStreamSourceRect(m_spD3D11VideoProc.Get(), 0, TRUE, &rcSrc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AVRender::UpdateVideoSwapChainVisual(IDXGISwapChain1* pVideoSwapChain)
{
    //
    // Create a CompositionSurface wrapper for our swapchain and then set as content
    // on our SpriteVisual. To do this we need to drop down into standard C++
    //

    DX::TIF(m_compositorNative->CreateCompositionSurfaceForSwapChain(pVideoSwapChain, &m_compositionSurface));

    DX::TIF(m_compositorInterface->CreateSurfaceBrushWithSurface(m_compositionSurface.Get(), &m_surfaceBrush));

    DX::TIF(m_surfaceBrush.As(&m_brush));

    m_spVideospriteVisualNative->put_Brush(m_brush.Get());
}

void AVRender::GetWaveFormat(IMFMediaType * pAudioType, WAVEFORMATEXTENSIBLE * pAudioFormat)
{
    //HRESULT hr = S_OK;

    GUID majorType;
    GUID subType;
    UINT32 data;

    DX::TIF(pAudioType->GetGUID(MF_MT_MAJOR_TYPE, &majorType));
    DX::TIF(pAudioType->GetGUID(MF_MT_SUBTYPE, &subType));
    pAudioFormat->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;

    if (subType == MFAudioFormat_PCM)
    {
        pAudioFormat->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }
    else if (subType == MFAudioFormat_Float)
    {
        pAudioFormat->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else
    {
        //TODO: error
        DX::TIF(E_UNEXPECTED);
    }

    if (subType == MFAudioFormat_PCM || subType == MFAudioFormat_Float)
    {
        DX::TIF(pAudioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &data));
        pAudioFormat->Format.nChannels = (WORD)data;

        DX::TIF(pAudioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &data));
        pAudioFormat->Format.nSamplesPerSec = (DWORD)data;

        DX::TIF(pAudioType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &data));
        pAudioFormat->Format.nAvgBytesPerSec = (DWORD)data;

        DX::TIF(pAudioType->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &data));
        pAudioFormat->Format.nBlockAlign = (WORD)data;

        DX::TIF(pAudioType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &data));
        pAudioFormat->Format.wBitsPerSample = (WORD)data;

        pAudioFormat->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

        pAudioFormat->Samples.wValidBitsPerSample = pAudioFormat->Format.wBitsPerSample;

        pAudioType->GetUINT32(MF_MT_AUDIO_CHANNEL_MASK, &data);
        pAudioFormat->dwChannelMask = (DWORD)data;
    }

}

#define MAX_TIME_SAMPLE 4096
typedef struct
{
    LONGLONG audioPos;
    LONGLONG audioFreq;
    LONGLONG audQPCPos;

    LONGLONG sampleTime;
    LONGLONG audioFrame;

    LONGLONG hsnAudQPC;
    double hnsStream;
} AudioTime;
AudioTime audioTime[MAX_TIME_SAMPLE];
int nAudTime = 0;

HRESULT AVRender::CopyToTexture(IMFMediaBuffer *pMediaBuffer)
{
    HRESULT hr = S_OK;
    BYTE * pbBuffer = NULL;
    DWORD cbMaxBuffer = 0;
    DWORD cbCurrentBuffer = 0;

    D3D11_TEXTURE2D_DESC desc = { 0 };
    m_pSWDecOutTexture->GetDesc(&desc);

    D3D11_MAPPED_SUBRESOURCE mapresource = {};
    DX::TIF(pMediaBuffer->Lock(&pbBuffer, &cbMaxBuffer, &cbCurrentBuffer));
    DX::TIF(m_spImmCtx->Map(m_pSWDecOutTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapresource));

    UINT cbRowLength;
    if (desc.Format == DXGI_FORMAT_NV12)
    {
        cbRowLength = desc.Width;
    }
    else if (desc.Format == DXGI_FORMAT_P010)
    {
        cbRowLength = desc.Width * 2;
    }
    else
    {
        DX::TIF(E_UNEXPECTED);
    }

    if (cbCurrentBuffer < cbRowLength * desc.Height * 3 / 2)
    {
        //Unexpected, is MFT missing format change
        DX::TIF(E_UNEXPECTED);
    }

    for (UINT32 i = 0; i < desc.Height * 3 / 2; i++)
    {
        memcpy((BYTE *)mapresource.pData + mapresource.RowPitch * i, pbBuffer + i * cbRowLength, cbRowLength);
    }
    

    m_spImmCtx->Unmap(m_pSWDecOutTexture.Get(), 0);
    pMediaBuffer->Unlock();
    return hr;
}

HRESULT AVRender::CreateSWDecoderOutputTexture(IMFMediaType * spMediaType)
{
    HRESULT hr = S_OK;
    D3D11_TEXTURE2D_DESC desc = { 0 };

    UINT32 nInputWidth = 0;
    UINT32 nInputHeight = 0;
    GUID subType = {};
    DXGI_FORMAT videoFormat;

    MFGetAttributeSize(spMediaType, MF_MT_FRAME_SIZE, &nInputWidth, &nInputHeight);
    DX::TIF(spMediaType->GetGUID(MF_MT_SUBTYPE, &subType));

    if (subType == MFVideoFormat_P010)
    {
        videoFormat = DXGI_FORMAT_P010;
    }
    else if (subType == MFVideoFormat_NV12)
    {
        videoFormat = DXGI_FORMAT_NV12;
    }
    else
    {
        DX::TIF(E_UNEXPECTED);
    }

    if (m_pSWDecOutTexture)
    {
        m_pSWDecOutTexture->GetDesc(&desc);
    }

    if (desc.Width != nInputWidth || desc.Height != nInputHeight || desc.Format != videoFormat)
    {
        D3D11_TEXTURE2D_DESC newVideoDesc = { 0 };

        m_pSWDecOutTexture = nullptr;

        newVideoDesc.Width = nInputWidth;
        newVideoDesc.Height = nInputHeight;
        newVideoDesc.MipLevels = 1;
        newVideoDesc.ArraySize = 1;
        newVideoDesc.Format = videoFormat;
        newVideoDesc.SampleDesc.Count = 1;
        newVideoDesc.Usage = D3D11_USAGE_DYNAMIC;
        newVideoDesc.BindFlags = D3D11_BIND_DECODER;
        newVideoDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        DX::TIF(m_spD3DDevice->CreateTexture2D(&newVideoDesc, NULL, &m_pSWDecOutTexture));
    }

    return hr;
}

//TODO: Take account for gap in audio stream
HRESULT AVRender::ComputeAudioSyncTime(LONGLONG audioSampleTime, UINT64 nAudioFrame)
{
    HRESULT hr = S_OK;

    UINT64 audioPosition = 0;
    UINT64 audioQPCPosition = 0;
    
    DX::TIF(m_spAudioClock->GetPosition(&audioPosition, &audioQPCPosition));

    double audioFramePos = (double)audioPosition / ((double)m_audioFrequesncy) * ((double)m_audioFormat.Format.nSamplesPerSec);

    //Convert audio position to sampleTime
    double syncAudioTime = (double)audioSampleTime  - 10000000.0 * ((double)nAudioFrame - audioFramePos) / ((double)m_audioFormat.Format.nSamplesPerSec);
    
    EnterCriticalSection(&m_audioDataSync);
    m_hnsAudioQPCTime = audioQPCPosition;
    m_audioStreamTime = syncAudioTime;
    LeaveCriticalSection(&m_audioDataSync);

    if (nAudTime < MAX_TIME_SAMPLE)
    {
        audioTime[nAudTime].audioPos = audioPosition;
        audioTime[nAudTime].audioFreq = m_audioFrequesncy;
        audioTime[nAudTime].audQPCPos = audioQPCPosition;

        audioTime[nAudTime].sampleTime = audioSampleTime;
        audioTime[nAudTime].audioFrame = nAudioFrame;

        audioTime[nAudTime].hsnAudQPC = audioQPCPosition;
        audioTime[nAudTime].hnsStream = m_audioStreamTime;
        nAudTime++;
    }
    

    return hr;
}

typedef struct
{
    LONGLONG audioQPCPos;
    double   audioSyncTime;
    LONGLONG sampleTime;
    double videoQPCTime;
    LONGLONG presentCount;
    double deltaVideoFrame;
    LONGLONG qpcFreq;
    double frameRate;
    double oldVideoTime;
    int waitFrameCount;
} VideoTime;
VideoTime videoTime[MAX_TIME_SAMPLE];
int nVidTime = 0;

DXGI_FRAME_STATISTICS frameStat[MAX_TIME_SAMPLE];
int nFrameStat = 0;

HRESULT AVRender::ComputeVideoWait(DXGI_FRAME_STATISTICS *pFrameStat, INT32 * nWaitFrame, LONGLONG sampleTime, LONGLONG nextSampleTime)
{
    HRESULT hr = S_OK;

    UINT lastPresentCount;
    double deltaVideoFrame = 0;

    *nWaitFrame = 0;
    
    DX::TIF(m_spSwapChain->GetLastPresentCount(&lastPresentCount));

    EnterCriticalSection(&m_audioDataSync);
    UINT64 hnsAudioQPCPosition = m_hnsAudioQPCTime;
    double syncAudioTime = m_audioStreamTime;
    LeaveCriticalSection(&m_audioDataSync);

    if (nVidTime < MAX_TIME_SAMPLE)
    {
        videoTime[nVidTime].sampleTime = sampleTime;
        videoTime[nVidTime].qpcFreq = m_QPCFrequency.QuadPart;
        videoTime[nVidTime].frameRate = m_exactFrameRate;
    }

    if (syncAudioTime <= 0.01)
    {
        //Either no audio or very early skip
        goto done;
    }

    //Covert video time to QPC time when needs to be dispalyed
    double videoQPCTime = ((double)m_QPCFrequency.QuadPart) * ((double)hnsAudioQPCPosition + (double)sampleTime - syncAudioTime) / 10000000.0f;

    m_videoFrameStat[m_lastStatPos].presentCount = lastPresentCount+1;
    m_videoFrameStat[m_lastStatPos].videoQPCTime = videoQPCTime;
    m_lastStatPos = (m_lastStatPos + 1) & (FRAMES_IN_STATISTICS - 1);

    if (nVidTime < MAX_TIME_SAMPLE)
    {
        videoTime[nVidTime].audioQPCPos = hnsAudioQPCPosition;
        videoTime[nVidTime].audioSyncTime = syncAudioTime;
        
        videoTime[nVidTime].videoQPCTime = videoQPCTime;
        videoTime[nVidTime].deltaVideoFrame = 0;
        videoTime[nVidTime].presentCount = 0;
    }

    if (pFrameStat->PresentCount == 0)
    {
        // No initial number, skip
        hr = S_OK;
        goto done;
    }

    if (nFrameStat < MAX_TIME_SAMPLE)
    {
        frameStat[nFrameStat] = *pFrameStat;
        nFrameStat++;
    }

    for (UINT i = 0; i < m_lastStatPos; i++)
    {

        if ((pFrameStat->PresentCount > m_syncPresentCount) && (m_videoFrameStat[i].presentCount == pFrameStat->PresentCount))
        {
            double qpcRefreshTime = (double)(pFrameStat->PresentRefreshCount - pFrameStat->SyncRefreshCount) * ((double)m_QPCFrequency.QuadPart) / m_exactFrameRate + (double)pFrameStat->SyncQPCTime.QuadPart;
            double deltaVideoPresentTime = qpcRefreshTime - m_videoFrameStat[i].videoQPCTime;
            
            deltaVideoFrame = deltaVideoPresentTime * m_exactFrameRate / ((double)m_QPCFrequency.QuadPart) ;

            //TEST:
            videoTime[nVidTime].deltaVideoFrame = deltaVideoFrame;
            videoTime[nVidTime].presentCount = pFrameStat->PresentCount;
            videoTime[nVidTime].oldVideoTime = m_videoFrameStat[i].videoQPCTime;
            
            //invalidate all present stats, so that it does not overcompensate
            m_lastStatPos = 0;
            break;
        }
    }

    if (deltaVideoFrame > 0.5f || deltaVideoFrame < -0.5f)
    {
        //Big difference between Audio and video time.
        //Wait until video is synced before next avsync 
        DX::TIF(m_spSwapChain->GetLastPresentCount(&m_syncPresentCount));
        m_syncPresentCount++;
    }
    
    //TODO: if deltaVideoFrame bigger set m_syncPresentCount;

done:
    double videoOnScreenFrames = (double)(nextSampleTime - sampleTime) * m_exactFrameRate / 10000000.0f - deltaVideoFrame;

    //Rounding
    if (videoOnScreenFrames + m_deltaFrameTime > 0)
    {
        *nWaitFrame = (INT32)(videoOnScreenFrames + m_deltaFrameTime + 0.5f);
    }
    else
    {
        *nWaitFrame = (INT32)(videoOnScreenFrames + m_deltaFrameTime - 0.5f);
    }

    if (nVidTime < MAX_TIME_SAMPLE)
    {
        videoTime[nVidTime].waitFrameCount = *nWaitFrame;
        nVidTime++;
    }
    
    m_deltaFrameTime = videoOnScreenFrames - (double)*nWaitFrame;
    
    return hr;
}

HRESULT AVRender::DumpVideoFrame(IMFSample* pVideoSample)
{
    HRESULT hr = S_OK;
    D3D11_TEXTURE2D_DESC nv12TextureDesc;
    ComPtr<ID3D11Texture2D> pNV12Img;
    D3D11_MAPPED_SUBRESOURCE subresouce;
    ComPtr<IMFMediaBuffer> pBuffer;

    ComPtr<ID3D11Texture2D> pNV12Texture;
    ComPtr<IMFDXGIBuffer> pDXGIBuffer;

    HANDLE hImageFile = CreateFile2(L"d:\\image.nv12", GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);

    DX::TIF(pVideoSample->GetBufferByIndex(0, &pBuffer));
    if (FAILED(pBuffer.As(&pDXGIBuffer)))
    {
        // Software decoder, copy decoded data to texture
        DX::TIF(CopyToTexture(pBuffer.Get()));
        pNV12Texture = m_pSWDecOutTexture;
    }
    else
    {
        pDXGIBuffer->GetResource(IID_PPV_ARGS(&pNV12Texture));
    }

    pNV12Texture->GetDesc(&nv12TextureDesc);
    nv12TextureDesc.Usage = D3D11_USAGE_STAGING;
    nv12TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
    nv12TextureDesc.BindFlags = 0;

    DX::TIF(m_spD3DDevice->CreateTexture2D(&nv12TextureDesc, nullptr, &pNV12Img));
    m_spImmCtx->CopyResource(pNV12Img.Get(), pNV12Texture.Get());
    m_spImmCtx->Flush();

    DX::TIF(m_spImmCtx->Map(pNV12Img.Get(), 0, D3D11_MAP_READ_WRITE, 0, &subresouce));

    if (subresouce.RowPitch == nv12TextureDesc.Width)
    {
        WriteFile(hImageFile, subresouce.pData, nv12TextureDesc.Width*nv12TextureDesc.Height*3/2, nullptr, nullptr);
    }
    else
    {
        for (UINT32 i = 0; i<nv12TextureDesc.Height*1.5; i++)
        {
            WriteFile(hImageFile, (char *)subresouce.pData + i*subresouce.RowPitch, nv12TextureDesc.Width, nullptr, nullptr);
        }
    }
    m_spImmCtx->Unmap(pNV12Img.Get(), 0);
    CloseHandle(hImageFile);

    return hr;
}
