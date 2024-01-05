# DX12Notes

### 01 区分全局变量&函数

```cpp
windowClass.hIcon = ::LoadIcon(hInst, NULL);
```

一些像这样的函数有 ::的标识，其作用是区分同名的全局函数和作用域内函数；

举个例子，如果在全局命名空间中有一个名为 myFunction 的函数，而当前作用域中也有一个同名的函数，为了明确调用全局命名空间中的函数，可以使用 :: 运算符，如 ::myFunction()。

需要注意的是，:: 运算符只用于指定作用域，而不代表函数的可见性。函数的可见性仍然受限于其声明的位置和可访问性规则。


### 02 SwapChain的四个Present Bit的区别 以及 Multi Sampling的适用性

```cpp
enum DXGI_SWAP_EFFECT
    {
        DXGI_SWAP_EFFECT_DISCARD	= 0,
        DXGI_SWAP_EFFECT_SEQUENTIAL	= 1,
        DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL	= 3,
        DXGI_SWAP_EFFECT_FLIP_DISCARD	= 4
    } 	DXGI_SWAP_EFFECT;
```
这个规定的是DX12里SwapChain的四种Swap模式:</br>

Flip和非Flip的区别:</br>
Flip可以实现垂直同步（Vertical Synchronization）和可适应刷新率（Adaptive Refresh Rate），以避免屏幕撕裂（Screen Tearing）。
主要的区别是Flip可以控制刷新的时机，他会选在显示器每次刷新（垂直回扫）开始时或结束时进行Swap，所以就不会出现扫到一半的时候Swap导致的（Screen Tearing）问题。

Sequential和Discard:</br>
Sequential是在Present完之后，还保留该buffer的内容，直到该Buffer现需要被重新写入的时候才被覆盖。适合做一些需要采样上一帧的情况，比如FXAA。Discard就是Present完就直接丢弃，会比较高效。

DX12文档里提到Flip的在创建SwapChain的时候都不能做 Multisampling, 估计也是出于性能考虑。

### 03 DX12内的不同的View

1-Render Target View (RTV)：渲染目标视图用于将渲染操作的结果绘制到后台缓冲区（如交换链缓冲区）上。RTV将后台缓冲区作为渲染目标，允许将渲染结果显示在屏幕上。每个后台缓冲区都需要一个独立的RTV。

2-Shader Resource View (SRV)：着色器资源视图用于将纹理、缓冲区等资源绑定到着色器阶段。SRV允许着色器读取这些资源的数据。多个SRV可以存储在同一个Descriptor Heap中，方便在绘制过程中切换不同的纹理或缓冲区。

3-Unordered Access View (UAV)：无序访问视图用于对纹理或缓冲区进行读写操作，通常用于计算着色器中的并行计算。UAV允许并发读写，可以实现高效的数据并行处理。多个UAV可以存储在同一个Descriptor Heap中。

4-Constant Buffer View (CBV)：常量缓冲区视图用于将常量数据绑定到着色器中的常量缓冲区，供着色器使用。CBV可以用于传递常量参数、矩阵变换等数据给着色器。多个CBV可以存储在同一个Descriptor Heap中。

### 04 static变量只在第一次调用的时候进行初始化

当一个函数中的变量被声明为 static，它的初始化只会在首次调用该函数时进行，之后的函数调用会复用已经初始化过的变量。这种行为对于需要保持变量状态或存储静态信息的情况非常有用。以下是一个简单的例子：

```cpp
#include <iostream>

void IncrementCounter()
{
    static int counter = 0;  // 静态变量，在第一次调用时进行初始化

    counter++;
    std::cout << "Counter: " << counter << std::endl;
}

int main()
{
    IncrementCounter();  // 输出 Counter: 1
    IncrementCounter();  // 输出 Counter: 2
    IncrementCounter();  // 输出 Counter: 3

    return 0;
}
```
其中初始化 counter = 0 就只在第一次被调用。


### 05 Cmake
github上下来的code，需要首先Cmake编译一下生成Visual

### 06 Fence的框架
这个伪代码还是有点误导性的，因为其 _nextFrameID 实际上就是上一帧(double buffering的存在)

```cpp
// 01 检查fence值是否达到
method IsFenceComplete( _fenceValue )
    return fence->GetCompletedValue() >= _fenceValue
end method

// 02 阻塞CPU进程 直到达到fenceValue
method WaitForFenceValue( _fenceValue )
    if ( !IsFenceComplete( _fenceValue )
        fence->SetEventOnCompletion( _fenceValue, fenceEvent )
        WaitForEvent( fenceEvent )
    end if
end method

// 03 
method Signal
    _fenceValue <- AtomicIncrement( fenceValue )
    commandQueue->Signal( fence, _fenceValue )
    return _fenceValue
end method

// 04 Render
method Render( frameID )
    _commandList <- PopulateCommandList( frameID )
    commandQueue->ExecuteCommandList( _commandList ) // 执行 CommandList
    
    _nextFrameID <- Present()    // 画面present到屏幕上

    fenceValues[frameID] = Signal()    // 生成唯一Signal值 并添加fence(当前帧)

    // 这里虽然是_nextFrameID 但因为是double buffering，其实是上一帧
    WaitForFenceValue( fenceValues[_nextFrameID] ) // 等待上一帧渲染完成，才覆盖其buffer(理解为double buffering的另一个buffer更为恰当)

    frameID <- _nextFrameID
end method
```
![image](./Images/GPU-Synchronization.png)</p>

这张图就明确标识清楚了 在CPU main里面的提交命令顺序和在GPU的 Queue里的执行顺序；是解耦的
然后由于Double Buffering的存在，CPU每次提交完一个frame，在开始下一个frame的时候，都需要等待上一个frame执行完毕(等待它空出来的buffer)。

### 07 DX12里的resource分配类型

#### Committed Resources</br>
这种资源分配方式是最常见和最简单的方式。当您创建一个已提交资源时，您需要指定资源的类型、大小和用途。然后，DirectX 12 会为该资源分配连续的内存空间，并保证该资源一直占用该内存空间，直到您显式地释放它。已提交资源适用于需要频繁更新或需要高速访问的资源，例如常量缓冲区或纹理。

#### Placed Resources</br>
已放置资源提供了更灵活的资源分配方式。与已提交资源不同，已放置资源允许您指定资源的精确内存位置。您可以通过提供资源的虚拟内存地址或 GPU 虚拟地址，将资源放置在已分配的内存区域中。这种方式适用于需要更复杂内存布局或需要与其他系统共享内存的资源。

#### Reserved Resources</br>
已放置资源提供了更灵活的资源分配方式。与已提交资源不同，已放置资源允许您指定资源的精确内存位置。您可以通过提供资源的虚拟内存地址或 GPU 虚拟地址，将资源放置在已分配的内存区域中。这种方式适用于需要更复杂内存布局或需要与其他系统共享内存的资源。

总结:</br>
Committed Resources 是最常见和简单的资源分配方式，适用于频繁更新或需要高速访问的资源。</br>
Placed Resources允许您指定资源的精确内存位置，适用于需要更复杂内存布局或与其他系统共享内存的资源。</br>
Reserved Resources是一种预留内存的方式，可在需要时转换为其他类型的资源，适用于动态分配和释放内存的情况。</br>

### 08 Root Signature
Root Signature就是一系列 Shader需要使用的资源的集合,主要包含三种类型:32-Bit constant； inline descriptor ；Descriptor Table。 其中其中 32-Bit constant直接包含数据，直接寻址，间接寻址的次数=0，如MVP； inline descriptor 直接包括需要频繁访问的数据和资源本身(不是指向Descriptor Heap的引用)，比如constant数组等，它但有个混淆就是它需要间接寻址次数=1；Descriptor Table 就包括了指向texture等较大数据的指针，需要间接寻址2次。可以理解成 32-Bit constant； inline descriptor  是直接把数据内嵌入Root Signature让Shader访问，而Descriptor Table就是通过指针间接访问。

#### 32-Bit Constant：
32位常量用于存储较小的常量数据，例如矩阵、向量、标志位等。这些常量数据直接嵌入到根签名中，而无需使用额外的Descriptor Heap或描述符表。32位常量非常适合存储需要频繁访问的数据，因为它们可以直接从根签名中访问，避免了额外的访存开销。

#### Inline Descriptor：
内联描述符用于直接将某些资源的描述符放置在根签名中，而无需使用Descriptor heap和 Descriptor table(但是这里有一个疑问，为什么需要间接寻址一次？)。内联描述符适用于常量缓冲区（CBV）和包含32位（FLOAT、UINT或SINT）组件的缓冲资源（SRV、UAV）。这些资源的描述符可以直接嵌入到根签名中，使得着色器可以直接访问这些资源，而无需通过描述符表间接引用。

#### Descriptor Table：
描述符表用于存储指向资源的描述符。它是一个包含多个描述符的数组或者是一个Descriptor Heap的引用。描述符表适用于各种类型的资源，包括纹理资源、结构化缓冲区、常量缓冲区等。通过描述符表，着色器可以通过索引或者动态索引来访问所需的资源。

其实这里还有一个混淆的点，就是 Inline Descriptor 究竟是直接存数据，还是存了一个指向descriptor的指针？ 我倾向于前者，但是在文章里有一句话是: The cost of accessing a root argument in a root signature in terms of levels of indirection is zero for 32-bit constants, 1 for inline descriptors, and 2 for descriptor tables [5]. 引起了一些混淆，既然inline是直接把资源嵌入在root signature里，那为什么还需要间接引用一次？

![image](./Images/Descriptor-Tables.png)</p>


## 关于DX12 Tutorial2的一些疑问

### 09 Vertex 的信息(包含Position / Normal / UV) 以及 Index的信息是怎么传进 Shader的？
vertex buffer， index buffer 就相当于VBO和IBO，来存储顶点和索引的数据。</br>
而他们的buffer view就起到像 OpenGL里VAO的作用，来描述了缓冲区的布局和访问方式，并提供给GPU用于读取和解释缓冲区数据的信息。但是还需要另外传进一个inputLayout，也是类似VAO</br>

应该这样理解 Vertex Input Layout和Vertex Buffer View两个概念结合起来更类似于OpenGL中的Vertex Array Object（VAO）。

任何 resource资源，都可以通过UpdateBufferResource函数传递进GPU。其做法是和Vulkan类似地，先传递到一个 stage buffer(CPU 和 GPU都可见，在DX12里是叫pIntermediateResource)，然后再传递到 GPU read only的 pDestinationResource。

### 10 Shader是怎么绑定在 Pipeline上的，需不需要预编译？
```cpp
    // Load the vertex shader.
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob));

    // Load the pixel shader.
    ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"PixelShader.cso", &pixelShaderBlob));
```
通过这两段代码去load shader。D3DCompileFromFile函数可以在运行时动态地将HLSL着色器文件编译为二进制格式的Compiled Shader Object（CSO）文件。CSO文件是经过编译的着色器的二进制表示形式，可以直接加载和使用。

### 11 Root Signature 是怎么Create以及绑定在Pipeline上的？
Root Signature需要先设置version，再绑定parameter，再序列化之后变成二进制格式才能create。
最后是绑定到pipelineStateStream上。</br>
最后在commandList里 使用 commandList->SetGraphicsRootSignature(rootSignature.Get());</br>
来绑定 rootSignature，同时也需要使用 commandList->SetGraphicsRoot32BitConstants 等去设置不同的resource。</br>

### 12 Descriptor Heap创建的个数与标准，怎么和Root Signature联系 
Descriptor Heap的个数和标准通常是根据应用程序的需求和配置来确定的，并没有固定的规定。但是，以下是一些常见的Descriptor Heap使用的标准：

常量缓冲区Descriptor Heap：通常，应用程序会为常量缓冲区创建一个单独的Descriptor Heap。这个Descriptor Heap用于存储常量缓冲区的描述符，以便在着色器中访问它们。

纹理和采样器Descriptor Heap：纹理和采样器通常会共享一个Descriptor Heap。这个Descriptor Heap用于存储纹理和采样器的描述符，以便在着色器中对它们进行采样或绑定。

渲染目标和深度目标Descriptor Heap：对于每个渲染目标和深度目标，通常会为其创建一个单独的Descriptor Heap。这些Descriptor Heap用于存储渲染目标和深度目标的视图描述符，以便在渲染过程中将它们绑定到图形管道。

### 13 DX12里有没有render Pass的概念
有，可以参考这一篇:</br>
https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-render-passes