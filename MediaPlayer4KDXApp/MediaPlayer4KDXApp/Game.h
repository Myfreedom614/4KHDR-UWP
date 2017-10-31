//
// Game.h
//

#pragma once


#include <agile.h>

#include <Windows.UI.Composition.h>
#include <windows.media.protection.playready.h>
//#include <vector>
#include <dxgi1_5.h>

#define NOMINMAX
#include <AVRender.h>

#define MEDIA_CONTENT_FILE L"content.txt"
#define MEDIA_CONTENT_FILE_ALT_PATH L"d:\\content.txt"

//
// Namespace aliases throughout project
//

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
//using namespace std;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A basic game implementation that creates a D3D11 device and
// provides a game loop
ref class Game sealed
{
public:

    Game();

    // Initialization and management
    void Initialize(Windows::UI::Core::CoreWindow^ window);

    // Basic game loop
    void Tick();
    void Update(float totalTime, float elapsedTime);
    void Render();

    // Rendering helpers
    void Clear();
    void Present();
    
private:

    void CreateUIDevice();
    void CreateVideoDevice();
    void CreateResources();
    void LoadInputFile();
    void InitializeUISwapChain();
    void InitializeUIComposition();
    void InitializeVideoVisual();

    //void PrepareTest();

    // Core Application state
    Platform::Agile<CoreWindow>  m_window;
    Windows::Foundation::Rect    m_windowBounds;

    // Direct3D Objects
    D3D_FEATURE_LEVEL               m_featureLevel;
    ComPtr<ID3D11Device1>           m_spD3DDevice;
    ComPtr<ID3D11DeviceContext1>    m_spImmCtx;

    // Rendering resources
    ComPtr<IDXGISwapChain3>         m_spUISwapChain;

    ComPtr<ID3D11RenderTargetView>  m_renderTargetView;
    ComPtr<ID3D11DepthStencilView>  m_depthStencilView;
    ComPtr<ID3D11Texture2D>         m_depthStencil;
    ComPtr<IDXGIOutput>             m_spDxgiOutput;
    
    // Windows.UI.Composition
    Compositor ^                    m_compositor;
    ContainerVisual ^               m_UIroot;
    CompositionTarget ^             m_UIcompositionTarget;


    ComPtr<ID3D11Device1>           m_spD3DVideoDevice;
    AVRender m_vidRenderer;
    int m_nPlayback;
    CAVDecoder *m_pAVDecoder;
    CDataConnector *m_pNV12VideoConnector;
    CDataConnector *m_pPCMAudioConnector;
    //bool m_fPipelineSetup;

    std::shared_ptr<GamepadReading> m_LastInputState;
    //vector<wstring> m_vecContent;

    HANDLE m_hContentFile;
    void GetNextMedia(HANDLE hFile, wchar_t *pMediaPath, int maxStrLen);
    int GetFileLine(HANDLE hFile, wchar_t *pMediaPath, int maxStrLen);
    
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
