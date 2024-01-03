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

2-Shader Resource View (SRV)：着色器资源视图用于将纹理、缓冲区等资源绑定到着色器阶段。SRV允许着色器读取这些资源的数据。多个SRV可以存储在同一个描述符堆中，方便在绘制过程中切换不同的纹理或缓冲区。

3-Unordered Access View (UAV)：无序访问视图用于对纹理或缓冲区进行读写操作，通常用于计算着色器中的并行计算。UAV允许并发读写，可以实现高效的数据并行处理。多个UAV可以存储在同一个描述符堆中。

4-Constant Buffer View (CBV)：常量缓冲区视图用于将常量数据绑定到着色器中的常量缓冲区，供着色器使用。CBV可以用于传递常量参数、矩阵变换等数据给着色器。多个CBV可以存储在同一个描述符堆中。

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