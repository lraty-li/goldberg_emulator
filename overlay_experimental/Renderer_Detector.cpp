#include "Renderer_Detector.h"
#include "Hook_Manager.h"

#ifdef STEAM_WIN32
#include "DX12_Hook.h"
#include "DX11_Hook.h"
#include "DX10_Hook.h"
#include "DX9_Hook.h"
#include "OpenGL_Hook.h"
#include "Windows_Hook.h"
#endif

#include <algorithm>

constexpr int max_hook_retries = 500;

#ifdef STEAM_WIN32
static decltype(&IDXGISwapChain::Present) _IDXGISwapChain_Present = nullptr;
static decltype(&IDirect3DDevice9::Present) _IDirect3DDevice9_Present = nullptr;
static decltype(&IDirect3DDevice9Ex::PresentEx) _IDirect3DDevice9Ex_PresentEx = nullptr;
static decltype(wglMakeCurrent)* _wglMakeCurrent = nullptr;

static constexpr auto windowClassName = "___overlay_window_class___";

void Renderer_Detector::create_hwnd()
{
    if (dummy_hWnd == nullptr)
    {
        HINSTANCE hInst = GetModuleHandle(nullptr);
        if (atom == 0)
        {
            // Register a window class for creating our render window with.
            WNDCLASSEX windowClass = {};

            windowClass.cbSize = sizeof(WNDCLASSEX);
            windowClass.style = CS_HREDRAW | CS_VREDRAW;
            windowClass.lpfnWndProc = DefWindowProc;
            windowClass.cbClsExtra = 0;
            windowClass.cbWndExtra = 0;
            windowClass.hInstance = hInst;
            windowClass.hIcon = NULL;
            windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
            windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            windowClass.lpszMenuName = NULL;
            windowClass.lpszClassName = windowClassName;
            windowClass.hIconSm = NULL;

            atom = ::RegisterClassEx(&windowClass);
        }

        if (atom > 0)
        {
            dummy_hWnd = ::CreateWindowEx(
                NULL,
                windowClassName,
                "",
                WS_OVERLAPPEDWINDOW,
                0,
                0,
                1,
                1,
                NULL,
                NULL,
                hInst,
                nullptr
            );

            assert(dummy_hWnd && "Failed to create window");
        }
    }

}

void Renderer_Detector::destroy_hwnd()
{
    if (dummy_hWnd != nullptr)
    {
        DestroyWindow(dummy_hWnd);
        UnregisterClass(windowClassName, GetModuleHandle(nullptr));
    }
}

HRESULT STDMETHODCALLTYPE Renderer_Detector::MyIDXGISwapChain_Present(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags)
{
    Renderer_Detector& inst = Renderer_Detector::Inst();
    Hook_Manager& hm = Hook_Manager::Inst();

    auto res = (_this->*_IDXGISwapChain_Present)(SyncInterval, Flags);
    if (!inst.stop_retry())
    {
        IUnknown* pDevice = nullptr;
        _this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D10Device**>(&pDevice)));
        if (pDevice)
        {
            DX10_Hook::Inst()->start_hook();
        }
        else
        {
            _this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D11Device**>(&pDevice)));
            if (pDevice)
            {
                DX11_Hook::Inst()->start_hook();
            }
            else
            {
                //_this->GetDevice(IID_PPV_ARGS(reinterpret_cast<ID3D12Device**>(&pDevice)));
                //if (pDevice)
                //{
                //    DX12_Hook::Inst()->start_hook();
                //}
            }
        }
        if (pDevice) pDevice->Release();
    }

    return res;
}

HRESULT STDMETHODCALLTYPE Renderer_Detector::MyPresent(IDirect3DDevice9* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    Renderer_Detector& inst = Renderer_Detector::Inst();
    Hook_Manager& hm = Hook_Manager::Inst();
    if (!inst.stop_retry())
    {
        DX9_Hook::Inst()->start_hook();
    }
    return (_this->*_IDirect3DDevice9_Present)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT STDMETHODCALLTYPE Renderer_Detector::MyPresentEx(IDirect3DDevice9Ex* _this, CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
    Renderer_Detector& inst = Renderer_Detector::Inst();
    Hook_Manager& hm = Hook_Manager::Inst();
    if (!inst.stop_retry())
    {
        DX9_Hook::Inst()->start_hook();
    }
    return (_this->*_IDirect3DDevice9Ex_PresentEx)(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

BOOL WINAPI Renderer_Detector::MywglMakeCurrent(HDC hDC, HGLRC hGLRC)
{
    Renderer_Detector& inst = Renderer_Detector::Inst();
    Hook_Manager& hm = Hook_Manager::Inst();
    if (!inst.stop_retry())
    {
        OpenGL_Hook::Inst()->start_hook();
    }
    return _wglMakeCurrent(hDC, hGLRC);
}

void Renderer_Detector::HookDXGIPresent(IDXGISwapChain* pSwapChain)
{
    if (!_dxgi_hooked)
    {
        _dxgi_hooked = true;
        (void*&)_IDXGISwapChain_Present = (*reinterpret_cast<void***>(pSwapChain))[(int)IDXGISwapChainVTable::Present];

        rendererdetect_hook->BeginHook();

        rendererdetect_hook->HookFuncs(
            std::pair<void**, void*>((PVOID*)& _IDXGISwapChain_Present, &Renderer_Detector::MyIDXGISwapChain_Present)
        );

        rendererdetect_hook->EndHook();
    }
}

void Renderer_Detector::HookDX9Present(IDirect3DDevice9* pDevice, bool ex)
{
    (void*&)_IDirect3DDevice9_Present = (*reinterpret_cast<void***>(pDevice))[(int)IDirect3DDevice9VTable::Present];
    if (ex)
        (void*&)_IDirect3DDevice9Ex_PresentEx = (*reinterpret_cast<void***>(pDevice))[(int)IDirect3DDevice9VTable::PresentEx];

    rendererdetect_hook->BeginHook();

    rendererdetect_hook->HookFuncs(
        std::pair<void**, void*>((PVOID*)& _IDirect3DDevice9_Present, &Renderer_Detector::MyPresent)
    );
    if (ex)
    {
        rendererdetect_hook->HookFuncs(
            std::pair<void**, void*>((PVOID*)& _IDirect3DDevice9Ex_PresentEx, &Renderer_Detector::MyPresentEx)
        );
    }

    rendererdetect_hook->EndHook();
}

void Renderer_Detector::HookwglMakeCurrent(decltype(wglMakeCurrent)* wglMakeCurrent)
{
    _wglMakeCurrent = wglMakeCurrent;

    rendererdetect_hook->BeginHook();

    rendererdetect_hook->HookFuncs(
        std::pair<void**, void*>((PVOID*)& _wglMakeCurrent, &Renderer_Detector::MywglMakeCurrent)
    );

    rendererdetect_hook->EndHook();
}

void Renderer_Detector::hook_dx9()
{
    if (!_dx9_hooked && !_renderer_found)
    {
        create_hwnd();
        if (dummy_hWnd == nullptr)
            return;

        IDirect3D9Ex* pD3D = nullptr;
        IUnknown* pDevice = nullptr;
        HMODULE library = GetModuleHandle(DX9_Hook::DLL_NAME);
        decltype(Direct3DCreate9Ex)* Direct3DCreate9Ex = nullptr;
        if (library != nullptr)
        {
            Direct3DCreate9Ex = (decltype(Direct3DCreate9Ex))GetProcAddress(library, "Direct3DCreate9Ex");
            D3DPRESENT_PARAMETERS params = {};
            params.BackBufferWidth = 1;
            params.BackBufferHeight = 1;
            params.hDeviceWindow = dummy_hWnd;
            params.BackBufferCount = 1;
            params.Windowed = TRUE;
            params.SwapEffect = D3DSWAPEFFECT_DISCARD;

            if (Direct3DCreate9Ex != nullptr)
            {
                Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D);
                pD3D->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, NULL, reinterpret_cast<IDirect3DDevice9Ex * *>(&pDevice));
            }
            else
            {
                decltype(Direct3DCreate9)* Direct3DCreate9 = (decltype(Direct3DCreate9))GetProcAddress(library, "Direct3DCreate9");
                if (Direct3DCreate9 != nullptr)
                {
                    pD3D = reinterpret_cast<IDirect3D9Ex*>(Direct3DCreate9(D3D_SDK_VERSION));
                    pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &params, reinterpret_cast<IDirect3DDevice9 * *>(&pDevice));
                }
            }
        }

        if (pDevice != nullptr)
        {
            PRINT_DEBUG("Hooked D3D9::Present to detect DX Version\n");

            _dx9_hooked = true;
            auto h = DX9_Hook::Inst();
            h->loadFunctions(reinterpret_cast<IDirect3DDevice9*>(pDevice), Direct3DCreate9Ex != nullptr);
            Hook_Manager::Inst().AddHook(h);
            HookDX9Present(reinterpret_cast<IDirect3DDevice9*>(pDevice), Direct3DCreate9Ex != nullptr);
        }
        else
        {
            PRINT_DEBUG("Failed to hook D3D9::Present to detect DX Version\n");
        }

        if (pDevice) pDevice->Release();
        if (pD3D) pD3D->Release();
    }
}

void Renderer_Detector::hook_dx10()
{
    if (!_dxgi_hooked && !_renderer_found)
    {
        create_hwnd();
        if (dummy_hWnd == nullptr)
            return;

        IDXGISwapChain* pSwapChain = nullptr;
        ID3D10Device* pDevice = nullptr;
        HMODULE library = GetModuleHandle(DX10_Hook::DLL_NAME);
        if (library != nullptr)
        {
            decltype(D3D10CreateDeviceAndSwapChain)* D3D10CreateDeviceAndSwapChain =
                (decltype(D3D10CreateDeviceAndSwapChain))GetProcAddress(library, "D3D10CreateDeviceAndSwapChain");
            if (D3D10CreateDeviceAndSwapChain != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};

                SwapChainDesc.BufferCount = 1;
                SwapChainDesc.BufferDesc.Width = 1;
                SwapChainDesc.BufferDesc.Height = 1;
                SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
                SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
                SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                SwapChainDesc.OutputWindow = dummy_hWnd;
                SwapChainDesc.SampleDesc.Count = 1;
                SwapChainDesc.SampleDesc.Quality = 0;
                SwapChainDesc.Windowed = TRUE;

                D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_NULL, NULL, 0, D3D10_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice);
            }
        }
        if (pSwapChain != nullptr)
        {
            PRINT_DEBUG("Hooked IDXGISwapChain::Present to detect DX Version\n");

            _dx10_hooked = true;
            auto h = DX10_Hook::Inst();
            h->loadFunctions(pSwapChain);
            Hook_Manager::Inst().AddHook(h);
            HookDXGIPresent(pSwapChain);
        }
        else
        {
            PRINT_DEBUG("Failed to Hook IDXGISwapChain::Present to detect DX Version\n");
        }
        if (pDevice)pDevice->Release();
        if (pSwapChain)pSwapChain->Release();
    }
}

void Renderer_Detector::hook_dx11()
{
    if (!_dxgi_hooked && !_renderer_found)
    {
        create_hwnd();
        if (dummy_hWnd == nullptr)
            return;

        IDXGISwapChain* pSwapChain = nullptr;
        ID3D11Device* pDevice = nullptr;
        HMODULE library = GetModuleHandle(DX11_Hook::DLL_NAME);
        if (library != nullptr)
        {
            decltype(D3D11CreateDeviceAndSwapChain)* D3D11CreateDeviceAndSwapChain =
                (decltype(D3D11CreateDeviceAndSwapChain))GetProcAddress(library, "D3D11CreateDeviceAndSwapChain");
            if (D3D11CreateDeviceAndSwapChain != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};

                SwapChainDesc.BufferCount = 1;
                SwapChainDesc.BufferDesc.Width = 1;
                SwapChainDesc.BufferDesc.Height = 1;
                SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
                SwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
                SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                SwapChainDesc.OutputWindow = dummy_hWnd;
                SwapChainDesc.SampleDesc.Count = 1;
                SwapChainDesc.SampleDesc.Quality = 0;
                SwapChainDesc.Windowed = TRUE;

                D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_NULL, NULL, 0, NULL, NULL, D3D11_SDK_VERSION, &SwapChainDesc, &pSwapChain, &pDevice, NULL, NULL);
            }
        }
        if (pSwapChain != nullptr)
        {
            PRINT_DEBUG("Hooked IDXGISwapChain::Present to detect DX Version\n");

            _dx11_hooked = true;
            auto h = DX11_Hook::Inst();
            h->loadFunctions(pSwapChain);
            Hook_Manager::Inst().AddHook(h);
            HookDXGIPresent(pSwapChain);
        }
        else
        {
            PRINT_DEBUG("Failed to Hook IDXGISwapChain::Present to detect DX Version\n");
        }

        if (pDevice) pDevice->Release();
        if (pSwapChain) pSwapChain->Release();
    }
}

void Renderer_Detector::hook_dx12()
{
    if (!_dxgi_hooked && !_renderer_found)
    {
        create_hwnd();
        if (dummy_hWnd == nullptr)
            return;

        IDXGIFactory4* pDXGIFactory = nullptr;
        IDXGISwapChain1* pSwapChain = nullptr;
        ID3D12CommandQueue* pCommandQueue = nullptr;
        ID3D12Device* pDevice = nullptr;
        ID3D12CommandAllocator* pCommandAllocator = nullptr;
        ID3D12GraphicsCommandList* pCommandList = nullptr;

        HMODULE library = GetModuleHandle(DX12_Hook::DLL_NAME);
        if (library != nullptr)
        {
            decltype(D3D12CreateDevice)* D3D12CreateDevice =
                (decltype(D3D12CreateDevice))GetProcAddress(library, "D3D12CreateDevice");

            if (D3D12CreateDevice != nullptr)
            {
                D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));

                if (pDevice != nullptr)
                {
                    DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
                    SwapChainDesc.BufferCount = 2;
                    SwapChainDesc.Width = 1;
                    SwapChainDesc.Height = 1;
                    SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    SwapChainDesc.Stereo = FALSE;
                    SwapChainDesc.SampleDesc = { 1, 0 };
                    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    SwapChainDesc.Scaling = DXGI_SCALING_NONE;
                    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                    SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

                    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                    pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));

                    if (pCommandQueue != nullptr)
                    {
                        HMODULE dxgi = GetModuleHandle("dxgi.dll");
                        if (dxgi != nullptr)
                        {
                            decltype(CreateDXGIFactory1)* CreateDXGIFactory1 = (decltype(CreateDXGIFactory1))GetProcAddress(dxgi, "CreateDXGIFactory1");
                            if (CreateDXGIFactory1 != nullptr)
                            {
                                CreateDXGIFactory1(IID_PPV_ARGS(&pDXGIFactory));
                                if (pDXGIFactory != nullptr)
                                {
                                    pDXGIFactory->CreateSwapChainForHwnd(pCommandQueue, dummy_hWnd, &SwapChainDesc, NULL, NULL, &pSwapChain);

                                    pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator));
                                    if (pCommandAllocator != nullptr)
                                    {
                                        pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCommandAllocator, NULL, IID_PPV_ARGS(&pCommandList));
                                    }
                                }
                            }
                        }
                    }
                }//if (pDevice != nullptr)
            }//if (D3D12CreateDevice != nullptr)
        }//if (library != nullptr)
        if (pCommandQueue != nullptr && pCommandList != nullptr && pSwapChain != nullptr)
        {
            PRINT_DEBUG("Hooked IDXGISwapChain::Present to detect DX Version\n");

            _dx12_hooked = true;
            auto h = DX12_Hook::Inst();
            h->loadFunctions(pCommandQueue, pCommandList, pSwapChain);
            Hook_Manager::Inst().AddHook(h);
            HookDXGIPresent(pSwapChain);
        }
        else
        {
            PRINT_DEBUG("Failed to Hook IDXGISwapChain::Present to detect DX Version\n");
        }

        if (pCommandList) pCommandList->Release();
        if (pCommandAllocator) pCommandAllocator->Release();
        if (pSwapChain) pSwapChain->Release();
        if (pDXGIFactory) pDXGIFactory->Release();
        if (pCommandQueue) pCommandQueue->Release();
        if (pDevice) pDevice->Release();
    }
}

void Renderer_Detector::hook_opengl()
{
    if (!_ogl_hooked && !_renderer_found)
    {
        HMODULE library = GetModuleHandle(OpenGL_Hook::DLL_NAME);
        decltype(wglMakeCurrent)* wglMakeCurrent = nullptr;
        OpenGL_Hook::wglSwapBuffers_t wglSwapBuffers = nullptr;
        if (library != nullptr)
        {
            wglMakeCurrent = (decltype(wglMakeCurrent))GetProcAddress(library, "wglMakeCurrent");
            wglSwapBuffers = (decltype(wglSwapBuffers))GetProcAddress(library, "wglSwapBuffers");
        }
        if (wglMakeCurrent != nullptr && wglSwapBuffers != nullptr)
        {
            PRINT_DEBUG("Hooked wglMakeCurrent to detect OpenGL\n");

            _ogl_hooked = true;
            auto h = OpenGL_Hook::Inst();
            h->loadFunctions(wglSwapBuffers);
            Hook_Manager::Inst().AddHook(h);
            HookwglMakeCurrent(wglMakeCurrent);
        }
        else
        {
            PRINT_DEBUG("Failed to Hook wglMakeCurrent to detect OpenGL\n");
        }
    }
}

void Renderer_Detector::create_hook(const char* libname)
{
    if (!_stricmp(libname, "d3d9.dll"))
        hook_dx9();
    else if (!_stricmp(libname, "d3d10.dll"))
        hook_dx10();
    else if (!_stricmp(libname, "d3d11.dll"))
        hook_dx11();
    else if (!_stricmp(libname, "d3d12.dll"))
        hook_dx12();
    else if (!_stricmp(libname, "opengl32.dll"))
        hook_opengl();
}

bool Renderer_Detector::stop_retry()
{
    // Retry or not
    bool stop = ++_hook_retries >= max_hook_retries;

    if (stop)
        renderer_found(nullptr);

    return stop;
}

void Renderer_Detector::find_renderer_proc(Renderer_Detector* _this)
{
    _this->rendererdetect_hook = new Base_Hook();
    Hook_Manager& hm = Hook_Manager::Inst();
    hm.AddHook(_this->rendererdetect_hook);

    std::vector<std::string> const libraries = { "opengl32.dll", "d3d12.dll", "d3d11.dll", "d3d10.dll", "d3d9.dll" };

    while (!_this->_renderer_found && !_this->stop_retry())
    {
        std::vector<std::string>::const_iterator it = libraries.begin();
        while (it != libraries.end())
        {
            it = std::find_if(it, libraries.end(), [](std::string const& name) {
                auto x = GetModuleHandle(name.c_str());
                if (x != NULL)
                    return true;
                return false;
                });

            if (it == libraries.end())
                break;

            _this->create_hook(it->c_str());
            ++it;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

#endif

void Renderer_Detector::find_renderer()
{
    if (_hook_thread == nullptr)
    {
#ifdef STEAM_WIN32
        _hook_thread = new std::thread(&Renderer_Detector::find_renderer_proc, this);
#endif
    }
}

void Renderer_Detector::renderer_found(Base_Hook* hook)
{
    Hook_Manager& hm = Hook_Manager::Inst();

    _renderer_found = true;
    game_renderer = hook;

    if (hook == nullptr)
        PRINT_DEBUG("We found a renderer but couldn't hook it, aborting overlay hook.\n");
    else
        PRINT_DEBUG("Hooked renderer in %d/%d tries\n", _hook_retries, max_hook_retries);

    _hook_thread->join();
    delete _hook_thread;
    _hook_thread = nullptr;

    hm.RemoveHook(rendererdetect_hook);
    destroy_hwnd();

#ifdef STEAM_WIN32
    if (hook == nullptr) // Couldn't hook renderer
    {
        hm.RemoveHook(Windows_Hook::Inst());
    }
    else
    {
        hm.AddHook(Windows_Hook::Inst());
    }
    if (_ogl_hooked)
    {
        auto h = OpenGL_Hook::Inst();
        if (h != hook)
        {
            _ogl_hooked = false;
            hm.RemoveHook(h);
        }
    }
    if (_dx9_hooked)
    {
        auto h = DX9_Hook::Inst();
        if (h != hook)
        {
            _dx9_hooked = false;
            hm.RemoveHook(h);
        }
    }
    if (_dx10_hooked)
    {
        auto h = DX10_Hook::Inst();
        if (h != hook)
        {
            _dx10_hooked = false;
            hm.RemoveHook(h);
        }
    }
    if (_dx11_hooked)
    {
        auto h = DX11_Hook::Inst();
        if (h != hook)
        {
            _dx11_hooked = false;
            hm.RemoveHook(h);
        }
    }
    if (_dx12_hooked)
    {
        auto h = DX12_Hook::Inst();
        if (h != hook)
        {
            _dx12_hooked = false;
            hm.RemoveHook(h);
        }
    }
#endif
}

Renderer_Detector& Renderer_Detector::Inst()
{
    static Renderer_Detector inst;
    return inst;
}

Base_Hook* Renderer_Detector::get_renderer() const
{
    return game_renderer;
}

Renderer_Detector::Renderer_Detector():
    _hook_thread(nullptr),
    _hook_retries(0),
    _renderer_found(false),
    _dx9_hooked(false),
    _dx10_hooked(false),
    _dx11_hooked(false),
    _dx12_hooked(false),
    _dxgi_hooked(false),
    _ogl_hooked(false),
    rendererdetect_hook(nullptr),
    game_renderer(nullptr),
    atom(0),
    dummy_hWnd(nullptr)
{}

Renderer_Detector::~Renderer_Detector()
{
    if (_hook_thread != nullptr)
    {
        _hook_retries = max_hook_retries;
        _hook_thread->join();
        delete _hook_thread;
    }
}