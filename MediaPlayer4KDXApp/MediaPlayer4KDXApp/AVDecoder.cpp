
#include "pch.h"
#include "Mfidl.h"
#include "MFSrcProcessor.h"
#include "Mftransform.h"
#include "MFTProcessor.h"
#include "AVDecoder.h"
#include "MFError.h"
#include "Mfapi.h"
//#include "wmcodecdsp.h"

#include "initguid.h" 


//TODO: remove them later
DEFINE_GUID(CLSID_CMSH264DecoderMFT, 0x62CE7E72, 0x4C71, 0x4d20, 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D);
DEFINE_GUID(CLSID_CH265DecoderTransform, 0x420a51a3, 0xd605, 0x430c, 0xb4, 0xfc, 0x45, 0x27, 0x4f, 0xa6, 0xc5, 0x62);
DEFINE_GUID(CLSID_MLPSPDIFMFT, 0xcf5eeedf, 0x0e92, 0x4b3b, 0xa1, 0x61, 0xbd, 0x0f, 0xfe, 0x54, 0x5e, 0x4b);

DEFINE_GUID(MEDIASUBTYPE_DOLBY_TRUEHD, 0xeb27cec4, 0x163e, 0x4ca3, 0x8b, 0x74, 0x8e, 0x25, 0xf9, 0x1b, 0x51, 0x7e);


// {4c547c24-af9a-4f38-96ad-978773cf53xe7}  MF_MT_WORSTCASE_ALLOCATION_DIMENSIONS   {UINT32 (BOOL)}
DEFINE_GUID(MF_MT_ALLOCATE_WORST_RESOLUTION, 0x4c547c24, 0xaf9a, 0x4f38, 0x96, 0xad, 0x97, 0x87, 0x73, 0xcf, 0x53, 0xe7);

// {67BE144C-88B7-4CA9-9628-C808D5262217}   MF_MT_MAX_DPB_SURFACE_COUNT             {UINT32}
DEFINE_GUID(MF_MT_MAX_DPB_SURFACE_COUNT, 0x67be144c, 0x88b7, 0x4ca9, 0x96, 0x28, 0xc8, 0x8, 0xd5, 0x26, 0x22, 0x17);


CAVDecoder::CAVDecoder()
{
    m_bRunning = false;
    m_bIsEOF = false;
    m_hAVDecodeControlThread = nullptr;

    m_pSrcProcessor = nullptr;

    // video connector
    m_pCompVideoConnector = nullptr;
    m_pNV12VideoConnector = nullptr;

    //Audio connectors
    m_pCompAudioConnector = nullptr;
    m_pPCMAudioConnector = nullptr;

    memset(m_mediaPath, 0, sizeof(m_mediaPath));
}

CAVDecoder::~CAVDecoder()
{
}

HRESULT CAVDecoder::Initialize(ID3D11Device1 *pD3d11Device, CDataConnector *pNV12VideoConnector, CDataConnector *pPCMAudioConnector, int fileNameLength, wchar_t *fileName)
{
    HRESULT hr = S_OK;
    
    m_pD3DDevice = pD3d11Device;
    m_pNV12VideoConnector = pNV12VideoConnector;
    m_pPCMAudioConnector = pPCMAudioConnector;
    if (fileName)
    {
        wcscpy_s(m_mediaPath, MEDIA_PATH_LEN, fileName);
    }
    

    return hr;
}

HRESULT CAVDecoder::Start()
{
    HRESULT hr = S_OK;

    m_bRunning = true;

    m_hAVDecodeControlThread = CreateThread(NULL, 0, CAVDecoder::StaticAVDecoderControlThread, this, 0, NULL);
    DX::TIF((m_hAVDecodeControlThread == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);
    
    return hr;
}

HRESULT CAVDecoder::Stop()
{
    HRESULT hr = S_OK;

    m_bRunning = false;
    if (m_pSrcProcessor)
    {
        m_pSrcProcessor->Stop();
    }

    WaitForSingleObject(m_hAVDecodeControlThread, INFINITE);
    CloseHandle(m_hAVDecodeControlThread);
    return hr;
}

DWORD WINAPI CAVDecoder::StaticAVDecoderControlThread(_In_ LPVOID lpParam)
{
    HRESULT hr = S_OK;
    CAVDecoder *pDec = (CAVDecoder *)lpParam;

    hr = pDec->AVDecoderControlThread();

    return hr;
}

HRESULT WINAPI CAVDecoder::AVDecoderControlThread()
{
    HRESULT hr = S_OK;

    //Video connectors
    m_pCompVideoConnector = new CDataConnector();
    DX::TIF(m_pCompVideoConnector->Initialize());

    //Audio connectors
    m_pCompAudioConnector = new CDataConnector();
    DX::TIF(m_pCompAudioConnector->Initialize());

    m_pControlDataConnector = new CDataConnector();
    DX::TIF(m_pControlDataConnector->Initialize());

    m_pSrcProcessor = new CMFSrcProcessor();

    if (m_mediaPath[0] != L'\0')
    {
        m_pSrcProcessor->Initilize((int)wcslen(m_mediaPath), m_mediaPath, m_pControlDataConnector, m_pCompAudioConnector, m_pCompVideoConnector);
    }
    else
    {
        DX::TIF(E_FAIL);
    }
    
    m_pSrcProcessor->Start();

    ComPtr<IMFTransform> spVideoMFT;
    ComPtr<IMFTransform> spAudioMFT;

    CSyncMFTProcessor *pVideoSyncMFTProc = NULL;
    CMFTProcessor *pVideoMFTProc = NULL;

    CSyncMFTProcessor *pAudioSyncMFTProc = NULL;
    CMFTProcessor *pAudioMFTProc = NULL;

    while (m_bRunning)
    {
        eDataType dataType;
        ComPtr<IUnknown> spData;

        hr = m_pControlDataConnector->ReadData(&dataType, &spData, 10);
        
        if (SUCCEEDED(hr) && dataType == DATA_MEDIA_TYPE)
        {
            GUID majorType;
            ComPtr<IMFMediaType> spMediaType;

            DX::TIF(spData.As(&spMediaType));
            DX::TIF(spMediaType->GetMajorType(&majorType));
            if (majorType == MFMediaType_Video)
            {
                DX::TIF(CreateVideoDecoder(m_pD3DDevice.Get(), &spVideoMFT, spMediaType.Get()));
                pVideoSyncMFTProc = new CSyncMFTProcessor();
                DX::TIF(pVideoSyncMFTProc->Initialize(spVideoMFT.Get(), m_pCompVideoConnector, m_pNV12VideoConnector));
                pVideoMFTProc = pVideoSyncMFTProc;

                DX::TIF(pVideoMFTProc->StartMFT());
            }
            else if (majorType == MFMediaType_Audio)
            {
                DX::TIF(CreateAudioDecoder(&spAudioMFT, spMediaType.Get(), m_pPCMAudioConnector));

                pAudioSyncMFTProc = new CSyncMFTProcessor();
                DX::TIF(pAudioSyncMFTProc->Initialize(spAudioMFT.Get(), m_pCompAudioConnector, m_pPCMAudioConnector));
                pAudioMFTProc = pAudioSyncMFTProc;

                DX::TIF(pAudioMFTProc->StartMFT());
            }

        }
        hr = m_pSrcProcessor->WaitToFinish(10);
        if (SUCCEEDED(hr))
        {
            break;
        }
    }

    if (SUCCEEDED(hr))
    {
        m_bRunning = false;
    }

    delete m_pSrcProcessor;
    m_pSrcProcessor = nullptr;

    if (m_pCompVideoConnector)
    {
        m_pCompVideoConnector->Shutdown();
    }

    if (m_pCompAudioConnector)
    {
        m_pCompAudioConnector->Shutdown();
    }

    if (m_pControlDataConnector)
    {
        m_pControlDataConnector->Shutdown();
    }
     
    if (pVideoSyncMFTProc)
    {
        pVideoSyncMFTProc->Shutdown();
        delete pVideoSyncMFTProc;
        pVideoMFTProc = NULL;
    }

    if (pAudioSyncMFTProc)
    {
        pAudioSyncMFTProc->Shutdown();
        delete pAudioSyncMFTProc;
        pAudioSyncMFTProc = NULL;
    }

    if (m_pCompVideoConnector)
    {
        delete m_pCompVideoConnector;
    }

    if (m_pCompAudioConnector)
    {
        delete m_pCompAudioConnector;
    }

    if (m_pControlDataConnector)
    {
        delete m_pControlDataConnector;
    }

    //After all of it;
    m_bIsEOF = true;
    return hr;
}


HRESULT  CAVDecoder::CreateVideoDecoder(ID3D11Device *pD3dDevice, IMFTransform **ppDecoder, IMFMediaType *inputType)
{
    HRESULT hr = S_OK;

    GUID guidVideoDecoder = CLSID_CMSH264DecoderMFT;
    GUID videoSubtype;
    GUID videoOutputSubtype;
    UINT resetToken = 0;
    UINT32 currentWidht = 0, currentHeight = 0;

    ComPtr<IMFDXGIDeviceManager> pDXGIDevManager;
    ComPtr<ID3D10Multithread> spMultithread;
    ComPtr<IMFTransform> spMFT;
    
    ComPtr<IMFMediaType> pDecoderOutType;

    DX::TIF(inputType->GetGUID(MF_MT_SUBTYPE, &videoSubtype));

    if (videoSubtype == MFVideoFormat_H264
        || videoSubtype == MFVideoFormat_H264_ES)
    {
        guidVideoDecoder = CLSID_CMSH264DecoderMFT;
        videoOutputSubtype = MFVideoFormat_NV12;
    }
    else if (videoSubtype == MFVideoFormat_HEVC
        || videoSubtype == MFVideoFormat_HEVC_ES)
    {
        guidVideoDecoder = CLSID_CH265DecoderTransform;
        videoOutputSubtype = MFVideoFormat_P010;
    }
    else if (videoSubtype == MFVideoFormat_WMV1
        || videoSubtype == MFVideoFormat_WMV2
        || videoSubtype == MFVideoFormat_WMV3
        || videoSubtype == MFVideoFormat_WVC1)
    {
        guidVideoDecoder = CLSID_WMVDecoderMFT;
        videoOutputSubtype = MFVideoFormat_NV12;
    }
    else if (videoSubtype == MFVideoFormat_VP90)
    {
        guidVideoDecoder = CLSID_MSVPxDecoder;
        videoOutputSubtype = MFVideoFormat_NV12;
    }
    //else if (videoSubtype == MEDIASUBTYPE_MP42)
    //{
    //    guidVideoDecoder = CLSID_CMpeg4DecMediaObject;
    //    videoOutputSubtype = MFVideoFormat_NV12;
    //}

    DX::TIF(MFCreateDXGIDeviceManager(&resetToken, &pDXGIDevManager));
    DX::TIF(pDXGIDevManager->ResetDevice(reinterpret_cast<IUnknown *>(pD3dDevice), resetToken));
    DX::TIF(pD3dDevice->QueryInterface(IID_PPV_ARGS(&spMultithread)));
    DX::TIF(spMultithread->SetMultithreadProtected(TRUE));

    DX::TIF(CoCreateInstance(guidVideoDecoder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spMFT)));

    spMFT->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, ULONG_PTR(pDXGIDevManager.Get()));
    ComPtr<IMFAttributes> spAttr;
    spMFT->GetAttributes(&spAttr);
    //spAttr->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, 1);
    //spAttr->SetUINT32(MF_MT_ALLOCATE_WORST_RESOLUTION, 1);
    //TODO: Get acutual max DPB buffer from media metadata
    //spAttr->SetUINT32(MF_MT_MAX_DPB_SURFACE_COUNT, 6);

    DX::TIF(spMFT->SetInputType(0, inputType, 0));

    DX::TIF(spMFT->GetOutputAvailableType(0, 0, &pDecoderOutType));
    DX::TIF(pDecoderOutType->SetGUID(MF_MT_SUBTYPE, videoOutputSubtype));

    DX::TIF(spMFT->SetOutputType(0, pDecoderOutType.Get(), 0));
    m_pNV12VideoConnector->WriteData(DATA_MEDIA_TYPE, pDecoderOutType.Get(), INFINITE);

    hr = MFGetAttributeSize(pDecoderOutType.Get(), MF_MT_FRAME_SIZE, &currentWidht, &currentHeight);

    DX::TIF(spMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    DX::TIF(spMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

    *ppDecoder = spMFT.Detach();
    return hr;
}

HRESULT CAVDecoder::CreateAudioDecoder(IMFTransform ** ppDecoder, IMFMediaType * inputType, CDataConnector *pPCMAudioConnector)
{
    HRESULT hr = S_OK;
    GUID guidAudioDecoder = CLSID_MSAACDecMFT;
    GUID audioSubtype;
    GUID audioOutputSubtype;
    ComPtr<IMFTransform> spMFT;
    ComPtr<IMFMediaType> pDecoderOutType;

    DX::TIF(inputType->GetGUID(MF_MT_SUBTYPE, &audioSubtype));

    if (audioSubtype == MFAudioFormat_AAC
        /*|| audioSubtype == MEDIASUBTYPE_RAW_AAC1*/)
    {
        guidAudioDecoder = CLSID_MSAACDecMFT;
    }
    else if (audioSubtype == MFAudioFormat_Dolby_AC3
        || audioSubtype == MFAudioFormat_Dolby_DDPlus)
    {
        guidAudioDecoder = CLSID_MSDDPlusDecMFT;
    }
    else if (audioSubtype == MFAudioFormat_WMAudioV8
        || audioSubtype == MFAudioFormat_WMAudioV9
        || audioSubtype == MFAudioFormat_WMAudio_Lossless)
    {
        // WMA decode is not valiable for apps now in durango, Add it later on
        guidAudioDecoder = CLSID_WMADecMediaObject;
    }
    else if (audioSubtype == MFAudioFormat_MP3
        || audioSubtype == MFAudioFormat_MPEG)
    {
        guidAudioDecoder = CLSID_MP3DecMediaObject;
    }
    else if (audioSubtype == MEDIASUBTYPE_DOLBY_TRUEHD)
    {
        guidAudioDecoder = CLSID_MLPSPDIFMFT;
    }

    DX::TIF(CoCreateInstance(guidAudioDecoder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spMFT)));

    DX::TIF(spMFT->SetInputType(0, inputType, 0));

    for (int i = 0; hr == S_OK; i++)
    {
        hr = spMFT->GetOutputAvailableType(0, i, &pDecoderOutType);
        if (SUCCEEDED(hr))
        {
            DX::TIF(pDecoderOutType->GetGUID(MF_MT_SUBTYPE, &audioOutputSubtype));
            if (audioOutputSubtype == MFAudioFormat_PCM ||
                audioOutputSubtype == MFAudioFormat_Float)
            {
                break;
            }
            else
            {
                pDecoderOutType = nullptr;
            }
        }
        
    }

    
    if (pDecoderOutType)
    {
        DX::TIF(spMFT->SetOutputType(0, pDecoderOutType.Get(), 0));

        DX::TIF(pPCMAudioConnector->WriteData(DATA_MEDIA_TYPE, pDecoderOutType.Get(), INFINITE));

        DX::TIF(spMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        DX::TIF(spMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

        *ppDecoder = spMFT.Detach();
    }
    else
    {
        //TODO: Better error code
        hr = E_FAIL;
    }

    
    return hr;
}
