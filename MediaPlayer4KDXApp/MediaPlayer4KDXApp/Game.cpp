//
// Game.cpp -
//

#include "pch.h"
#include "dxgi1_4.h"
#include "assert.h"

#include "Mfidl.h"
#include "MFSrcProcessor.h"
#include "Mftransform.h"
#include "MFTProcessor.h"
#include "AVDecoder.h"

#include "Game.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <AVRender.h>

using namespace Windows::Foundation::Collections;

using namespace Windows::Graphics::Display;
using namespace Windows::ApplicationModel;
using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Game Object implementation
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Constructor.
Game::Game()
{
    m_LastInputState = { 0 };
    m_pAVDecoder = nullptr;
    m_pNV12VideoConnector = nullptr;
    m_hContentFile = NULL;
}

void Game::LoadInputFile()
{
    m_hContentFile = CreateFile2(MEDIA_CONTENT_FILE_ALT_PATH, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
    if (m_hContentFile == INVALID_HANDLE_VALUE)
    {
        m_hContentFile = CreateFile2(MEDIA_CONTENT_FILE, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
        if (m_hContentFile == INVALID_HANDLE_VALUE)
        {
            DX::TIF(HRESULT_FROM_WIN32(GetLastError()));
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize the Direct3D resources required to run.
void Game::Initialize(Windows::UI::Core::CoreWindow^ window)
{
    m_window = window;

    CreateUIDevice();
    CreateVideoDevice();

    //TODO: find out when should this closed or shutdown
    m_pNV12VideoConnector = new CDataConnector();
    DX::TIF(m_pNV12VideoConnector->Initialize());

    m_pPCMAudioConnector = new CDataConnector();
    DX::TIF(m_pPCMAudioConnector->Initialize());

    CreateResources();

    LoadInputFile();

    
    DX::TIF(MFStartup(MF_VERSION));
    m_pAVDecoder = new CAVDecoder();

    wchar_t mediaPath[1024] = {};
    GetNextMedia(m_hContentFile, mediaPath, _countof(mediaPath));
    m_pAVDecoder->Initialize(m_spD3DVideoDevice.Get(), m_pNV12VideoConnector, m_pPCMAudioConnector, (int)wcslen(mediaPath), mediaPath);
    m_pAVDecoder->Start();
    m_nPlayback++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Executes basic game loop.
void Game::Tick()
{

    Update(0,0);

    Render();
}

bool IsPressing(GamepadReading* reading, GamepadButtons buttons)
{
    return reading && ((reading->Buttons & buttons) == buttons);
}

bool IsPressed(GamepadReading* reading, GamepadReading* previousReading, GamepadButtons buttons)
{
    return IsPressing(reading, buttons) && !IsPressing(previousReading, buttons);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updates the world
void Game::Update(float /* totalTime */, float /* elapsedTime */)
{
    std::shared_ptr<GamepadReading> reading = nullptr;
    IVectorView<Gamepad^>^ gamepads = Gamepad::Gamepads;

    if (gamepads->Size)
    {
        reading = std::make_shared<GamepadReading>(gamepads->GetAt(0)->GetCurrentReading());

        if (IsPressed(reading.get(), m_LastInputState.get(), GamepadButtons::X))
        {
            wchar_t str[4096] = L"";

            //Skip video
            m_pAVDecoder->Stop();
            delete m_pAVDecoder;
            m_pAVDecoder = nullptr;
            m_pAVDecoder = new CAVDecoder();

            wchar_t mediaPath[1024] = {};
            GetNextMedia(m_hContentFile, mediaPath, _countof(mediaPath));
            m_pAVDecoder->Initialize(m_spD3DVideoDevice.Get(), m_pNV12VideoConnector, m_pPCMAudioConnector, (int)wcslen(mediaPath), mediaPath);
            m_pAVDecoder->Start();
            m_nPlayback++;
            swprintf_s(str, 4096, L"Media Playback count %d \n", m_nPlayback);
            OutputDebugString(str);

        }

        if (IsPressed(reading.get(), m_LastInputState.get(), GamepadButtons::Y))
        {
            //Change Gamma
            m_vidRenderer.ToggleHDRState();
        }

        m_LastInputState = reading;
    }

    if (m_pAVDecoder && m_pAVDecoder->IsEOF())
    {
        wchar_t str[4096] = L"";

        m_pAVDecoder->Stop();
        delete m_pAVDecoder;
        m_pAVDecoder = nullptr;
        m_pAVDecoder = new CAVDecoder();

        wchar_t mediaPath[1024] = {};
        GetNextMedia(m_hContentFile, mediaPath, _countof(mediaPath));
        m_pAVDecoder->Initialize(m_spD3DVideoDevice.Get(), m_pNV12VideoConnector, m_pPCMAudioConnector, (int)wcslen(mediaPath), mediaPath);
        m_pAVDecoder->Start();
        m_nPlayback++;
        swprintf_s(str, 4096, L"Media Playback count %d \n", m_nPlayback);
        OutputDebugString(str);
    }




}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draws the scene
void Game::Render()
{
    Clear();
    Present();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper method to clear the backbuffers
void Game::Clear()
{
    // Clear the views
    static float flAlpha = 0.00f;
    static float flDelta = 0.01f;
#if 0
    // Simulate a UI animation by just changing alpha channel
    flAlpha += flDelta;
    if (flAlpha > 1.00f)
    {
        flAlpha = 1.00f;;
        flDelta = -flDelta;
    }
    else if (flAlpha < 0.00f)
    {
        flAlpha = 0.00f;;
        flDelta = -flDelta;
    }
#endif
    //const float clearColor[] = { 0.00f, 0.00f, 0.00f, flAlpha };
    //m_spImmCtx->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    //m_spImmCtx->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
    //m_spImmCtx->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Presents the backbuffer contents to the screen
void Game::Present()
{
    // The first argument instructs DXGI to block until VSync, putting the application
    // to sleep until the next VSync. This ensures we don't waste any cycles rendering
    // frames that will never be displayed to the screen.
    HRESULT hr = m_spUISwapChain->Present(1, 0);

    
    // If the device was reset we must completely reinitialize the renderer.
    /*if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        Initialize(m_window.Get());
    }
    else
    {
        DX::ThrowIfFailed(hr);
    }*/

    /*DXGI_FRAME_STATISTICS frameStat = {};
    if (S_OK == m_spUISwapChain->GetFrameStatistics(&frameStat))
    {
        UINT dwIssuedPresentCount = 0;
        if (S_OK == m_spUISwapChain->GetLastPresentCount(&dwIssuedPresentCount))
        {
            // If previous frame's latency has reached max latency already, throttle by waiting for one vblank interval to avoid lock contention
            if ((dwIssuedPresentCount - frameStat.PresentCount) >= sc_dwMaxSwapChainLatencyInFrame)
            {
                m_spDxgiOutput->WaitForVBlank();

                // Get statistics again to find out new latency                                      
                if (S_OK != m_spUISwapChain->GetFrameStatistics(&frameStat))
                {
                    frameStat.PresentCount++;
                }
            }

            m_videoFrameStat.dxgiFrameStat = frameStat;
            m_videoFrameStat.dwIssuedPresentCount = dwIssuedPresentCount;
        }
    }*/

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These are the resources that depend on the device.
void Game::CreateUIDevice()
{
    // This flag adds support for surfaces with a different color channel ordering than the API default.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    
#ifdef _DEBUG
    //   creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_10_0
    };

    // Create the DX11 API device object, and get a corresponding context.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

    DX::TIF(
        D3D11CreateDevice(
            nullptr,                    // specify null to use the default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                    // leave as nullptr unless software device
            creationFlags,              // optionally set debug and Direct2D compatibility flags
            featureLevels,              // list of feature levels this app can support
            ARRAYSIZE(featureLevels),   // number of entries in above list
            D3D11_SDK_VERSION,          // always set this to D3D11_SDK_VERSION
            &device,                    // returns the Direct3D device created
            &m_featureLevel,            // returns feature level of device created
            &context                    // returns the device immediate context
            )
        );

    // Get the DirectX11.1 device by QI off the DirectX11 one.
    DX::TIF(device.As(&m_spD3DDevice));

    // And get the corresponding device context in the same way.
    DX::TIF(context.As(&m_spImmCtx));
}

void Game::CreateVideoDevice()
{
    // This flag adds support for surfaces with a different color channel ordering than the API default.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

#ifdef _DEBUG
    //   creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_10_0
    };

    // Create the DX11 API device object, and get a corresponding context.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

    DX::TIF(
        D3D11CreateDevice(
            nullptr,                    // specify null to use the default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                    // leave as nullptr unless software device
            creationFlags,              // optionally set debug and Direct2D compatibility flags
            featureLevels,              // list of feature levels this app can support
            ARRAYSIZE(featureLevels),   // number of entries in above list
            D3D11_SDK_VERSION,          // always set this to D3D11_SDK_VERSION
            &device,                    // returns the Direct3D device created
            &m_featureLevel,            // returns feature level of device created
            &context                    // returns the device immediate context
        )
    );

    // Get the DirectX11.1 device by QI off the DirectX11 one.
    DX::TIF(device.As(&m_spD3DVideoDevice));

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateResources()
{
    // Store the window bounds so the next time we get a SizeChanged event we can
    // avoid rebuilding everything if the size is identical.
    m_windowBounds = m_window.Get()->Bounds;

    // If the swap chain already exists, resize it, otherwise create one.
    if (m_spUISwapChain != nullptr)
    {
        DX::TIF(m_spUISwapChain->ResizeBuffers(2, 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
    }
    else
    {
        InitializeUISwapChain();
        InitializeUIComposition();
        InitializeVideoVisual();
    }

    // Obtain the backbuffer for this window which will be the final 3D rendertarget.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    DX::TIF(m_spUISwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer));

    // Create a view interface on the rendertarget to use on bind.
    DX::TIF(m_spD3DDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTargetView));

    // Cache the rendertarget dimensions in our helper class for convenient use.
    D3D11_TEXTURE2D_DESC backBufferDesc = { 0 };
    backBuffer->GetDesc(&backBufferDesc);

    // Allocate a 2-D surface as the depth/stencil buffer and
    // create a DepthStencil view on this surface to use on bind.
    CD3D11_TEXTURE2D_DESC depthStencilDesc(DXGI_FORMAT_D32_FLOAT, backBufferDesc.Width, backBufferDesc.Height, 1, 1, D3D11_BIND_DEPTH_STENCIL);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencil;
    DX::TIF(m_spD3DDevice->CreateTexture2D(&depthStencilDesc, nullptr, &depthStencil));

    CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
    DX::TIF(m_spD3DDevice->CreateDepthStencilView(depthStencil.Get(), &depthStencilViewDesc, &m_depthStencilView));

    // Create a viewport descriptor of the full window size.
    CD3D11_VIEWPORT viewPort(0.0f, 0.0f, static_cast<float>(backBufferDesc.Width), static_cast<float>(backBufferDesc.Height));

    // Set the current viewport using the descriptor.
    m_spImmCtx->RSSetViewports(1, &viewPort);

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Game::InitializeUISwapChain()
{
    // First, retrieve the underlying DXGI Device from the D3D Device
    Microsoft::WRL::ComPtr<IDXGIDevice1>  dxgiDevice;
    DX::TIF(m_spD3DDevice.As(&dxgiDevice));

    // Identify the physical adapter (GPU or card) this device is running on.
    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    DX::TIF(dxgiDevice->GetAdapter(&dxgiAdapter));

    DX::TIF(dxgiAdapter->EnumOutputs(0, &m_spDxgiOutput));

    UINT nModes = 0;
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, NULL));

    DXGI_MODE_DESC *pDisplayModes = new DXGI_MODE_DESC[nModes];
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, pDisplayModes));

    // And obtain the factory object that created it.
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    DX::TIF(dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory));

    // Create a descriptor for the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
    swapChainDesc.Width = pDisplayModes[0].Width; //UI_SWAPCHAIN_WIDTH;
    swapChainDesc.Height = pDisplayModes[0].Height; //UI_SWAPCHAIN_HEIGHT;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 3;
    swapChainDesc.Stereo = false;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swapChainDesc.Flags = 0;

    delete[] pDisplayModes;

    // Create 
    ComPtr<IDXGISwapChain1> spUISwapChain;
    DX::TIF(dxgiFactory->CreateSwapChainForComposition(m_spD3DDevice.Get(), &swapChainDesc, nullptr, &spUISwapChain));
    DX::TIF(spUISwapChain.As(&m_spUISwapChain));
        
    //dxgiDevice->SetMaximumFrameLatency(sc_dwMaxSwapChainLatencyInFrame);
    //m_spUISwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Game::InitializeUIComposition()
{
    //
    // Set up Windows.UI.Composition Compositor, root ContainerVisual, and associate with the
    // CoreWindow.
    //

    m_compositor = ref new Compositor();

    m_UIroot = m_compositor->CreateContainerVisual();

    // Display information can only be retrieved and accessed on the UI thread. 
    // Inverse default implicit scaling if any on root visual.
    // If reacting to resolution changes is necessary it is possible to listen for a resolution scale changed
    // notification, but the accompanying logic might end up complicated which is not really required for Xbox.
    DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();
    float ScaleFactor = static_cast<float>(currentDisplayInformation->ResolutionScale) / SCALE_100_PERCENT;
    
    float3 ScaleValue = { 1.0f/ScaleFactor, 1.0f/ScaleFactor, 1.0f };
    m_UIroot->Scale = ScaleValue;
    
    m_UIcompositionTarget = m_compositor->CreateTargetForCurrentView();
    m_UIcompositionTarget->Root = m_UIroot;

    //
    // Create a simple UI scene.
    //
    
    auto spriteVisual = m_compositor->CreateSpriteVisual();
    m_UIroot->Children->InsertAtTop(spriteVisual);
    
    //
    // Create a CompositionSurface wrapper for our swapchain and then set as content
    // on our SpriteVisual. To do this we need to drop down into standard C++
    //

    ComPtr<ABI::Windows::UI::Composition::ICompositor> compositorInterface;
    ComPtr<ABI::Windows::UI::Composition::ICompositorInterop> compositorNative;
    ComPtr<ABI::Windows::UI::Composition::ICompositionSurface> compositionSurface;
    ComPtr<ABI::Windows::UI::Composition::ICompositionSurfaceBrush> surfaceBrush;
    ComPtr<ABI::Windows::UI::Composition::ICompositionBrush> brush;
    ComPtr<ABI::Windows::UI::Composition::ISpriteVisual> spriteVisualNative;
    ComPtr<ABI::Windows::UI::Composition::IVisual> baseVisualNative;

    ComPtr<IInspectable> compositorUnderlyingType = reinterpret_cast<IInspectable*>(m_compositor);
    ComPtr<IInspectable> visualUnderlyingType = reinterpret_cast<IInspectable*>(spriteVisual);
    DX::TIF(compositorUnderlyingType.As(&compositorInterface));
    DX::TIF(compositorUnderlyingType.As(&compositorNative));
    DX::TIF(visualUnderlyingType.As(&spriteVisualNative));
    DX::TIF(visualUnderlyingType.As(&baseVisualNative));

    DX::TIF(compositorNative->CreateCompositionSurfaceForSwapChain(m_spUISwapChain.Get(), compositionSurface.GetAddressOf()));
        
    DX::TIF(compositorInterface->CreateSurfaceBrushWithSurface(compositionSurface.Get(), surfaceBrush.GetAddressOf()));
    
    DX::TIF(surfaceBrush.As(&brush));
    
    spriteVisualNative->put_Brush(brush.Get());

    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    m_spUISwapChain->GetDesc1(&scd);

    UINT nModes = 0;
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, NULL));

    DXGI_MODE_DESC *pDisplayModes = new DXGI_MODE_DESC[nModes];
    DX::TIF(m_spDxgiOutput->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &nModes, pDisplayModes));

    // Properties directly on the visual
    Vector3 VisualOffset = { 0.0f, 0.0f, 0.0f };
    Vector2 VisualSize = { float(scd.Width), float(scd.Height) };
    // Set VisualScale for fullscreen
    Vector3 VisualScale = { static_cast<float>(pDisplayModes[0].Width) / static_cast<float>(scd.Width), static_cast<float>(pDisplayModes[0].Height) / static_cast<float>(scd.Height), 1.0f };

    delete[] pDisplayModes;

    baseVisualNative->put_Size(VisualSize);
    baseVisualNative->put_Offset(VisualOffset);
    baseVisualNative->put_Scale(VisualScale);

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Game::InitializeVideoVisual()
{
    auto spriteVisual = m_compositor->CreateSpriteVisual();
    m_UIroot->Children->InsertAtBottom(spriteVisual);
    
    ComPtr<ABI::Windows::UI::Composition::ISpriteVisual> spriteVisualNative;
    ComPtr<ABI::Windows::UI::Composition::IVisual> baseVisualNative;
    ComPtr<IInspectable> visualUnderlyingType = reinterpret_cast<IInspectable*>(spriteVisual);
    DX::TIF(visualUnderlyingType.As(&spriteVisualNative));
    DX::TIF(visualUnderlyingType.As(&baseVisualNative));
    
    ComPtr<IInspectable> compositorUnderlyingType = reinterpret_cast<IInspectable*>(m_compositor);
    ComPtr<ABI::Windows::UI::Composition::ICompositor> compositorInterface;
    ComPtr<ABI::Windows::UI::Composition::ICompositorInterop> compositorNative;
    DX::TIF(compositorUnderlyingType.As(&compositorInterface));
    DX::TIF(compositorUnderlyingType.As(&compositorNative));
        
    m_vidRenderer.Initialize(m_spD3DVideoDevice.Get(), m_pNV12VideoConnector, m_pPCMAudioConnector, spriteVisualNative.Get(), baseVisualNative.Get(), compositorInterface.Get(), compositorNative.Get());
}


void Game::GetNextMedia(HANDLE hFile, wchar_t * pMediaPath, int maxStrLen)
{
    int pathLen = 0;

    // line starts with # is comment; move to next line
    while (pMediaPath[0] == 0xFEFF || pMediaPath[0] == L'#' || pathLen == 0)
    {
        pathLen = GetFileLine(hFile, pMediaPath, maxStrLen);
        if (pathLen == 0)
        {
            //EOF; start from begining
            DWORD dwRet = SetFilePointer(hFile, NULL, NULL, FILE_BEGIN);
            if (dwRet == INVALID_SET_FILE_POINTER)
            {
                DX::TIF(HRESULT_FROM_WIN32(GetLastError()));
            }
        }
    }
}
int Game::GetFileLine(HANDLE hFile, wchar_t * pMediaPath, int maxStrLen)
{
    wchar_t c;
    DWORD dwReadBytes = 0;
    int nChar = 0;
    UINT linePos = 0;

    ReadFile(hFile, &c, 2, &dwReadBytes, NULL);
    while (dwReadBytes > 0)
    {
        nChar++;
        if (c == L'\n')
        {
            // one line completed 
            pMediaPath[linePos] = '\0'; 
            linePos++;
            break;
        }
        else if (c == L'\r')
        {
            //Ignore this
        }
        else
        {
            pMediaPath[linePos] = c;
            linePos++;
        }

        ReadFile(hFile, &c, 2, &dwReadBytes, NULL);
    }
    pMediaPath[linePos] = '\0';
    return nChar;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

