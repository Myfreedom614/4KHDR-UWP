//#include "MFTProcessor.h"
//#include "MFSrcProcessor.h"

#pragma once

using namespace Microsoft::WRL;
#define MEDIA_PATH_LEN 1024

class CAVDecoder
{
public:
    CAVDecoder();
    ~CAVDecoder();

    HRESULT Initialize(ID3D11Device1 *pD3d11Device, CDataConnector *pNV12VideoConnector, CDataConnector *pPCMAudioConnector, int fileNameLength, wchar_t *fileName);

    HRESULT Start();
    HRESULT Stop();

    bool IsEOF() { return m_bIsEOF; }

private:
    volatile bool m_bRunning;
    volatile bool m_bIsEOF;
    wchar_t m_mediaPath[MEDIA_PATH_LEN];

    HANDLE m_hAVDecodeControlThread;

    CMFSrcProcessor *m_pSrcProcessor;

    //Video connectors
    CDataConnector *m_pCompVideoConnector;
    CDataConnector *m_pNV12VideoConnector;

    //Audio connectors
    CDataConnector *m_pCompAudioConnector;
    CDataConnector *m_pPCMAudioConnector;

    //Decoder control thread data connector
    CDataConnector *m_pControlDataConnector;

    ComPtr<ID3D11Device1> m_pD3DDevice;

    static DWORD WINAPI StaticAVDecoderControlThread(_In_ LPVOID lpParam);
    _Check_return_ HRESULT AVDecoderControlThread();

    _Check_return_ HRESULT CreateVideoDecoder(ID3D11Device *pD3dDevice, IMFTransform **ppDecoder, IMFMediaType *inputType);
    _Check_return_ HRESULT CreateAudioDecoder(IMFTransform **ppDecoder, IMFMediaType *inputType, CDataConnector *pPCMAudioConnector);
};

