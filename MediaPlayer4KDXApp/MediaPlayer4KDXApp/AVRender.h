//
// AVRender.h
//

#pragma once

#include "mfapi.h"

#include <Windows.UI.Composition.h>
#include <windows.media.protection.playready.h>
//#include <vector>
#include <dxgi1_5.h>

#include <Audioclient.h>

#define NOMINMAX
#include <WindowsNumerics.h>
#include <Windows.UI.Composition.Interop.h>
//#include <Windows.Foundation.Numerics.h>

#define FRAMES_IN_STATISTICS 8

using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Core;
using namespace Windows::Gaming::Input;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation::Numerics;
using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct VideoFrameStatistics
{
    UINT presentCount;
    double videoQPCTime;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AVRender //: public SampleSink
{
private:
    HANDLE m_hVideoRenderThread;
    HANDLE m_hAudioRenderThread;
    // This guards both swap chain and sample queue
    bool m_fRunning;

    HANDLE m_hFirstVideoSample;
    HANDLE m_hFirstAudioSample;

    ComPtr<ID3D11Device1> m_spD3DDevice;
    ComPtr<ID3D11DeviceContext1> m_spImmCtx;
    ComPtr<IDXGISwapChain3> m_spSwapChain;
    ComPtr<IDXGIOutput> m_spDxgiOutput;
    ComPtr<IDXGIFactory2> m_spDxgiFactory;
    ComPtr<ID3D11Texture2D> m_spSoftwareCodecRenderTexture;
    VideoFrameStatistics m_videoFrameStat[FRAMES_IN_STATISTICS];
    UINT m_lastStatPos;

    ComPtr<IMFMediaType> m_spPendingMediaType;

    const static UINT sc_dwSwapChainBackBuffers = 2;

    // members for video processor for video
    ComPtr<ID3D11VideoDevice1> m_spVideoDevice;
    ComPtr<ID3D11VideoProcessorEnumerator> m_spD3D11VideoProcEnum;
    ComPtr<ID3D11VideoProcessor> m_spD3D11VideoProc;
    ComPtr<ID3D11VideoContext2> m_spD3D11VideoContext;

    // DCOMP interfaces
    ComPtr<ABI::Windows::UI::Composition::ICompositor> m_compositorInterface;
    ComPtr<ABI::Windows::UI::Composition::ICompositorInterop> m_compositorNative;
    ComPtr<ABI::Windows::UI::Composition::ISpriteVisual>  m_spVideospriteVisualNative;
    ComPtr<ABI::Windows::UI::Composition::IVisual>  m_spBaseVisualNative;
    ComPtr<ABI::Windows::UI::Composition::ICompositionSurface> m_compositionSurface;
    ComPtr<ABI::Windows::UI::Composition::ICompositionSurfaceBrush> m_surfaceBrush;
    ComPtr<ABI::Windows::UI::Composition::ICompositionBrush> m_brush;

    DXGI_COLOR_SPACE_TYPE m_swapchainColorSpace;

    //Audio interfaces
    ComPtr<IAudioClient> m_spAudioClient;
    ComPtr<IAudioRenderClient> m_spAudioRenderClient;
    ComPtr<IAudioClock> m_spAudioClock;

    //Audio data
    UINT64 m_audioFrequesncy;

    CRITICAL_SECTION m_audioDataSync;
    UINT64 m_hnsAudioQPCTime;
    double m_audioStreamTime;
    
    LARGE_INTEGER m_QPCFrequency;
    double m_videoFrameRate;
    double m_exactFrameRate;
    double m_deltaFrameTime;
    UINT m_syncPresentCount;

    HANDLE m_hAudioData;
    WAVEFORMATEXTENSIBLE m_audioFormat;


private:
    static DWORD WINAPI RenderVideoThreadProc(LPVOID pParam);
    static DWORD WINAPI RenderAudioThreadProc(LPVOID pParam);
    void VideoRenderLoop();
    void AudioRenderLoop();
    void Present(bool fRepeat, UINT syncInterval);

    //LONGLONG GetVBlankInterval();
    //LONGLONG GetVideoTimeInternal(LONGLONG llSystemTime);

    // Video SwapChain
    void UpdateVideoSwapChainVisual(IDXGISwapChain1* pVideoSwapChain);
    void ReleaseVideoSwapChainVisual();
    void ConfigureColorSpaceDisplay(IMFMediaType *pVideoMediaType);
    HRESULT ConfigureSwapChain();
    HRESULT ConfigureAudio(IMFMediaType *pAudioType);
    void GetWaveFormat(IMFMediaType *pAudioType, WAVEFORMATEXTENSIBLE *pAudioFormat);

    ComPtr<ID3D11Texture2D> m_pSWDecOutTexture;
    HRESULT CopyToTexture(IMFMediaBuffer *pMediaBuffer);
    HRESULT CreateSWDecoderOutputTexture(IMFMediaType *spMediaType);

    HRESULT ComputeAudioSyncTime(LONGLONG audioSampleTime, UINT64 nAudioFrame);
    HRESULT ComputeVideoWait(DXGI_FRAME_STATISTICS *pFrameStat, INT32 * nWaitFrame, LONGLONG sampleTime, LONGLONG nextSampleTime);

    HRESULT DumpVideoFrame(IMFSample* pVideoSample);

    UINT32 m_frameRate;
    UINT32 m_colorSpace;
    UINT32 m_height;
    UINT32 m_width;
    UINT32 m_HDR;

    BOOL m_bForceSDR;
    DXGI_COLOR_SPACE_TYPE m_VPOutColorSpace;
    DXGI_COLOR_SPACE_TYPE m_SCInColorSpace;

    CDataConnector *m_pNV12VideoConnector;
    CDataConnector *m_pPCMAudioConnector;

    LONGLONG m_hnsLastVideoTime;
    LONGLONG m_hnsLastAudioTime;



public:
    AVRender();
    ~AVRender();

    HRESULT Initialize(ID3D11Device1 *pD3d11Device1, CDataConnector *pNV12VideoConnector, CDataConnector *pPCMAudioConnector, ABI::Windows::UI::Composition::ISpriteVisual* pVideoSpriteVisual, ABI::Windows::UI::Composition::IVisual* pBaseVisual,
        ABI::Windows::UI::Composition::ICompositor* pCompositorInterface, ABI::Windows::UI::Composition::ICompositorInterop* pCompositorNative);

    HRESULT Shutdown();

    void DrawVideoSample(IMFSample *pVideoSample);

    void Reset();
    virtual void Flush();
    void ToggleHDRState();
};
