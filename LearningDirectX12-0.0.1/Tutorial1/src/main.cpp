#define WIN32_LEAN_AND_MEAN
#include <Windows.h>  // Windows API
#include <shellapi.h> // For CommandLineToArgvW 可以使用Shell函数

// The min/max macros conflict with like-named member functions.
// Only use std::min and std::max defined in <algorithm>.
// 避免C语言库里的min/max 与 <algorithm>冲突
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// In order to define a function called CreateWindow, the Windows macro needs to
// be undefined.
// undefined Windows.h里的CreateWindow函数
#if defined(CreateWindow)
#undef CreateWindow
#endif

// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
// 包含Component Object Model 的智能指针功能 ComPtr
#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX 12 specific headers.
#include <d3d12.h>
#include <dxgi1_6.h>                   // manage low-level tasks
#include <d3dcompiler.h>               // runtime compile GLSL
#include <DirectXMath.h>

// D3D12 extension library.
#include <d3dx12.h>

// STL Headers
#include <algorithm>
#include <cassert>
#include <chrono>

// Helper functions
#include <Helpers.h>

// The number of swap chain back buffers.
const uint8_t g_NumFrames = 3;

// Use WARP adapter (Windows Advanced Rasterization Platform）
bool g_UseWarp = false;

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

// Set to true once the DX12 objects have been initialized.
bool g_IsInitialized = false;

// Window handle.
HWND g_hWnd;

// Window rectangle (used to toggle fullscreen state).
RECT g_WindowRect;

// DirectX 12 Objects
// ComPtr 是一个封装好的模板类，是Shared_Ptr自动管理生命周期 并实现很多方便功能

ComPtr<ID3D12Device2> g_Device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain4> g_SwapChain;
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames];  // 按照buffering数量创建buffer
ComPtr<ID3D12GraphicsCommandList> g_CommandList;      
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames]; // 每个swap chain buffer都需要一个Command Allocators
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;   // RTV means render target view (RTV) 用DescriptorHeap(set)的形式表示
UINT g_RTVDescriptorSize;   
UINT g_CurrentBackBufferIndex;

// Synchronization objects
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0;
uint64_t g_FrameFenceValues[g_NumFrames] = {};  // 每个buffering需要一个fence values
HANDLE g_FenceEvent; 

// By default, enable V-Sync.
// Can be toggled with the V key.
bool g_VSync = true;
bool g_TearingSupported = false;
// By default, use windowed mode.
// Can be toggled with the Alt+Enter or F11
bool g_Fullscreen = false;

// Window callback function. 窗口回调函数
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 解析命令行传入参数 执行相应的变量设置
// eg:-w 800 -h 600 -warp
void ParseCommandLineArguments()
{
    int argc;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    for (size_t i = 0; i < argc; ++i)
    {
        if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
        {
            g_ClientWidth = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
        {
            g_ClientHeight = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
        {
            g_UseWarp = true;
        }
    }

    // Free memory allocated by CommandLineToArgvW
    ::LocalFree(argv);
}

// Enable后将自动监测报告DX12的相关错误
void EnableDebugLayer()
{
#if defined(_DEBUG)
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    ComPtr<ID3D12Debug> debugInterface;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif
}

// 注册windows类(非常类似CreateInfo)
void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName )
{
    // Register a window class for creating our render window with.
    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = hInst;
    windowClass.hIcon = ::LoadIcon(hInst, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));

    static HRESULT hr = ::RegisterClassExW(&windowClass);
    assert(SUCCEEDED(hr));
}

// 用上面的Info CreateWindow
HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst,
    const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    // create window
    HWND hWnd = ::CreateWindowExW(
        NULL,
        windowClassName,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInst,
        nullptr
    );

    // assert was used to debug
    assert(hWnd && "Failed to create window");

    return hWnd;
}


// Get Adapter
// Adapter就是可用的GPU
// 要是用Physical Adapter，就是用GPU做硬件渲染，要是用Wrap，就是用CPU做软件渲染
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;

#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;

    if (useWarp)
    {
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
    }
    else
    {
        SIZE_T maxDedicatedVideoMemory = 0;

        //EnumAdapters1() 枚举返回所有的Physical Adapter
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

            // Check to see if the adapter can create a D3D12 device without actually 
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            // 创建Device

            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
                    D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) && 
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
            }
        }
    }

    return dxgiAdapter4;
}

// Create Device
// Adapter就像是Physical Device 是GPU，Device就是Vulkan里的 Logical Device
ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
    // Create Device
    ComPtr<ID3D12Device2> d3d12Device2;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

    // Enable debug messages in debug mode.
    // output代码debug信息
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
    {
        // 该函数的意义是当发生某个特定严重的debug信息时，决定是否中断
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE); // 内存损坏
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);      // Error
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);    // Warning

        // 让DebugLayer忽略特定的严重等级和特定的ID的报错信息(Suppress messages)

        // 1-特定的种类
        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        // 2-特定的严重等级
        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };

        // 3-特定的ID
        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
    }
#endif

    return d3d12Device2;
}

// CreateCommandQueue
ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type )
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type =     type;                                     // 类型
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;      // 优先级
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

    return d3d12CommandQueue;
}

// Check Nvidia 和 AMD 的GPU是否支持Tearing
bool CheckTearingSupport()
{
    BOOL allowTearing = FALSE;

    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
    // graphics debugging tools which will not support the 1.5 factory interface 
    // until a future update.
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, 
                &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }

    return allowTearing == TRUE;
}

// Create SwapChain (Triple)
ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, 
    ComPtr<ID3D12CommandQueue> commandQueue, 
    uint32_t width, uint32_t height, uint32_t bufferCount )
{
    // IDGXFactory4对象主要用于创建Adapter和SwapChain
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };           // 使用Filp模型就不能使用MultiSample
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;         // SwapChain的方式 用Flip_Discard
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;            // 表示透明度
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // Create Swap Chain
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

    return dxgiSwapChain4;
}

// Create Descriptor Heap
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;                       // Type就可以是四种类型的Resource

    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

// Render target view
void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // 每个SwapBuffer都需要一个targetView
    for (int i = 0; i < g_NumFrames; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        g_BackBuffers[i] = backBuffer;

        rtvHandle.Offset(rtvDescriptorSize);
    }
}

// Command Allocator
// Command Allocator 和 Command Queue对应，一个Queue 有且只有一个 Allocator
// Command Allocator 负责分配和管理 Command List
ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    // Type分成Direct / Bundle / Compute / Copy
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

    return commandAllocator;
}

// Create Command List
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    
    ThrowIfFailed(commandList->Close());

    return commandList;
}

// Create Fence
ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence;

    // 都是通过Device来Create
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    return fence;
}

// Create Fence Event 来自操作系统的Event
HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;
    
    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent && "Failed to create fence event.");

    return fenceEvent;
}

// Signal
uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;  // 如果是多线程 就应该是原子操作的++
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

// WaitForFence(下一个要写入那个buffer的Fence)
void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
    std::chrono::milliseconds duration = std::chrono::milliseconds::max() )
{
    if (fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

// Flush函数包含Signal和WaitForFence 在程序的末尾执行
void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent )
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

// 
void Update()
{
    // 输出每一帧的帧率
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;

    elapsedSeconds += deltaTime.count() * 1e-9; 
    if (elapsedSeconds > 1.0)
    {
        wchar_t buffer[500];
        auto fps = frameCounter / elapsedSeconds;
        swprintf_s(buffer, 500, L"FPS: %f\n", fps);
        OutputDebugStringW(buffer);

        frameCounter = 0;
        elapsedSeconds = 0.0;
    }
}

void Render()
{
    auto commandAllocator = g_CommandAllocators[g_CurrentBackBufferIndex];
    auto backBuffer = g_BackBuffers[g_CurrentBackBufferIndex];

    commandAllocator->Reset();
    g_CommandList->Reset(commandAllocator.Get(), nullptr);

    // 01 Clear the render target.
    {
        // Barriers 用于初始化 resource barriers 是同步的一种
        // 该resource barriers用于将一个资源（在这里是backBuffer）
        // 从D3D12_RESOURCE_STATE_PRESENT（呈现状态）转换为D3D12_RESOURCE_STATE_RENDER_TARGET（渲染目标状态）。
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        g_CommandList->ResourceBarrier(1, &barrier);

        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

        // 获取对应renderTargetView的Descriptor handle
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            g_CurrentBackBufferIndex, g_RTVDescriptorSize);

        g_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    // 02 Present 就是开头fence的伪代码那一段
    {
        // Resource Barriers把Render Target变成Present
        // 非常类似Vulkan的Image Layout
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        g_CommandList->ResourceBarrier(1, &barrier);

        // CommandList->close() 就类似Vulkan的 submit，就是把Command List提交到Queue上(重要)
        ThrowIfFailed(g_CommandList->Close());

        ID3D12CommandList* const commandLists[] = {
            g_CommandList.Get()
        };

        g_CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

        g_FrameFenceValues[g_CurrentBackBufferIndex] = Signal(g_CommandQueue, g_Fence, g_FenceValue);

        UINT syncInterval = g_VSync ? 1 : 0;
        UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(g_SwapChain->Present(syncInterval, presentFlags));

        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

        WaitForFenceValue(g_Fence, g_FrameFenceValues[g_CurrentBackBufferIndex], g_FenceEvent);
    }
}

// Resize
void Resize(uint32_t width, uint32_t height)
{
    if (g_ClientWidth != width || g_ClientHeight != height)
    {
        // Don't allow 0 size swap chain back buffers.
        g_ClientWidth = std::max(1u, width );
        g_ClientHeight = std::max( 1u, height);

        // Flush the GPU queue to make sure the swap chain's back buffers
        // are not being referenced by an in-flight command list.
        Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);

        for (int i = 0; i < g_NumFrames; ++i)
        {
            // Any references to the back buffers must be released
            // before the swap chain can be resized.
            g_BackBuffers[i].Reset();
            g_FrameFenceValues[i] = g_FrameFenceValues[g_CurrentBackBufferIndex];
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        ThrowIfFailed(g_SwapChain->GetDesc(&swapChainDesc));   // 获取SwapChain的Description
        ThrowIfFailed(g_SwapChain->ResizeBuffers(g_NumFrames, g_ClientWidth, g_ClientHeight,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

        UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
    }
}

void SetFullscreen(bool fullscreen)
{
    if (g_Fullscreen != fullscreen)
    {
        g_Fullscreen = fullscreen;

        if (g_Fullscreen) // Switching to fullscreen.
        {
            // Store the current window dimensions so they can be restored 
            // when switching out of fullscreen state.
            ::GetWindowRect(g_hWnd, &g_WindowRect);

            // Set the window style to a borderless window so the client area fills
            // the entire screen.
            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

            ::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

            // 查询显示器信息
            // Query the name of the nearest display device for the window.
            // This is required to set the fullscreen dimensions of the window
            // when using a multi-monitor setup.
            HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEX monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            ::GetMonitorInfo(hMonitor, &monitorInfo);

            ::SetWindowPos(g_hWnd, HWND_TOPMOST,
                monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.top,
                monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);

            ::ShowWindow(g_hWnd, SW_MAXIMIZE);
        }
        else
        {
            // Restore all the window decorators.
            ::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

            ::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
                g_WindowRect.left,
                g_WindowRect.top,
                g_WindowRect.right - g_WindowRect.left,
                g_WindowRect.bottom - g_WindowRect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);

            ::ShowWindow(g_hWnd, SW_NORMAL);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if ( g_IsInitialized )
    {
        switch (message)
        {
        // 重绘
        case WM_PAINT:
            Update();
            Render();
            break;

        // 按下某一些键盘按键时
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
        {
            bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

            switch (wParam)
            {
            case 'V':
                g_VSync = !g_VSync;
                break;
            case VK_ESCAPE:
                ::PostQuitMessage(0);
                break;
            case VK_RETURN:
                if ( alt )
                {
            case VK_F11:
                SetFullscreen(!g_Fullscreen);
                }
                break;
            }
        }
        break;

        // The default window procedure will play a system notification sound 
        // when pressing the Alt+Enter keyboard combination if this message is 
        // not handled.
        // 字符
        case WM_SYSCHAR:
        break;

        // Size变化
        case WM_SIZE:
        {
            RECT clientRect = {};
            ::GetClientRect(g_hWnd, &clientRect);

            int width = clientRect.right - clientRect.left;
            int height = clientRect.bottom - clientRect.top;

            Resize(width, height);
        }
        break;

        // 窗口销毁
        case WM_DESTROY:
            ::PostQuitMessage(0);
            break;
        default:
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
    else
    {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return 0;
}

// 约定俗成是一个CALLBACK
int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
    // Using this awareness context allows the client area of the window 
    // to achieve 100% scaling while still allowing non-client window content to 
    // be rendered in a DPI sensitive fashion.
    // DPI感知，监测窗口的内容和大小
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Window class name. Used for registering / creating the window.
    const wchar_t* windowClassName = L"DX12WindowClass";

    // 解析命令行参数
    ParseCommandLineArguments();

    // 打开DebugLayer
    EnableDebugLayer();

    // 检查Tearing
    g_TearingSupported = CheckTearingSupport();

    // 注册窗口类
    RegisterWindowClass(hInstance, windowClassName);
    g_hWnd = CreateWindow(windowClassName, hInstance, L"Learning DirectX 12",
        g_ClientWidth, g_ClientHeight);

    // Initialize the global window rect variable.
    ::GetWindowRect(g_hWnd, &g_WindowRect);

    // 获取 Adapter + Create Device
    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
    g_Device = CreateDevice(dxgiAdapter4);
    
    // Command Queue
    g_CommandQueue = CreateCommandQueue(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);

    // SwapChain
    g_SwapChain = CreateSwapChain(g_hWnd, g_CommandQueue,
        g_ClientWidth, g_ClientHeight, g_NumFrames);

    // 获取BackBuffer的Index
    g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

    // 获取DescriptorHeap
    g_RTVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
    g_RTVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 更新RTV
    UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);

    // 渲染的准备工作
    for (int i = 0; i < g_NumFrames; ++i)
    {
        g_CommandAllocators[i] = CreateCommandAllocator(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }
    g_CommandList = CreateCommandList(g_Device,
        g_CommandAllocators[g_CurrentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    g_Fence = CreateFence(g_Device);
    g_FenceEvent = CreateEventHandle();

    g_IsInitialized = true;

    ::ShowWindow(g_hWnd, SW_SHOW);

    // 消息循环(其中包含渲染循环)
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    // Flush一个程序才执行一次，是为了确保所有的渲染命令都执行完毕
    // Make sure the command queue has finished all commands before closing.
    Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);

    ::CloseHandle(g_FenceEvent);

    return 0;
}