/**
 * DirectX9 offscreen render target helper implementation.
 */
#include "CubismRenderTargetDx9.h"

CubismOffscreenFrame_Dx9::CubismOffscreenFrame_Dx9()
    : _texture(nullptr)
    , _surface(nullptr)
    , _prevSurface(nullptr)
    , _drawing(false)
    , _width(0)
    , _height(0)
    , _format(D3DFMT_A8R8G8B8)
{
}

CubismOffscreenFrame_Dx9::~CubismOffscreenFrame_Dx9()
{
    ReleaseInternal();
}

void CubismOffscreenFrame_Dx9::ReleaseInternal()
{
    if (_prevSurface) { _prevSurface->Release(); _prevSurface = nullptr; }
    if (_surface) { _surface->Release(); _surface = nullptr; }
    if (_texture) { _texture->Release(); _texture = nullptr; }
    _width = _height = 0;
    _drawing = false;
}

bool CubismOffscreenFrame_Dx9::Create(LPDIRECT3DDEVICE9 device, int width, int height, D3DFORMAT format)
{
    if (!device) return false;
    if (width <= 0 || height <= 0) return false;
    // Recreate if size or format changed
    if (_texture && (width == _width && height == _height && format == _format))
    {
        return true;
    }
    ReleaseInternal();

    _format = format;

    // Create a render target texture (no mipmaps)
    if (FAILED(D3DXCreateTexture(device, width, height, 1, D3DUSAGE_RENDERTARGET,
                                 format, D3DPOOL_DEFAULT, &_texture)))
    {
        ReleaseInternal();
        return false;
    }
    if (FAILED(_texture->GetSurfaceLevel(0, &_surface)))
    {
        ReleaseInternal();
        return false;
    }

    _width = width;
    _height = height;
    return true;
}

bool CubismOffscreenFrame_Dx9::Resize(LPDIRECT3DDEVICE9 device, int width, int height)
{
    return Create(device, width, height, _format);
}

void CubismOffscreenFrame_Dx9::Destroy()
{
    ReleaseInternal();
}

void CubismOffscreenFrame_Dx9::BeginDraw(LPDIRECT3DDEVICE9 device, bool clear, D3DCOLOR clearColor)
{
    if (!device || !_surface || _drawing) return;

    // 古い参照を解放（前回の Begin/End が不均衡でもリークを防ぐ）
    if (_prevSurface) { _prevSurface->Release(); _prevSurface = nullptr; }

    // Store current render target & viewport
    device->GetRenderTarget(0, &_prevSurface);
    device->GetViewport(&_prevViewport);

    device->SetRenderTarget(0, _surface);

    D3DVIEWPORT9 vp; vp.X = 0; vp.Y = 0; vp.Width = _width; vp.Height = _height; vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
    device->SetViewport(&vp);

    if (clear)
    {
        device->ColorFill(_surface, NULL, clearColor);
    }

    _drawing = true;
}

void CubismOffscreenFrame_Dx9::EndDraw(LPDIRECT3DDEVICE9 device)
{
    if (!_drawing) return;

    // Restore previous RT
    device->SetRenderTarget(0, _prevSurface);

    if (_prevSurface) { _prevSurface->Release(); _prevSurface = nullptr; }

    // Restore viewport
    device->SetViewport(&_prevViewport);

    _drawing = false;
}

void CubismOffscreenFrame_Dx9::Clear(LPDIRECT3DDEVICE9 device, D3DCOLOR clearColor)
{
    if (!device || !_surface) return;
    device->ColorFill(_surface, NULL, clearColor);
}

// Manager implementation
bool CubismRenderTargetManager_Dx9::Initialize(LPDIRECT3DDEVICE9 device, int width, int height)
{
    if (!_maskBuffer.Create(device, width, height)) return false;
    if (!_offscreenBuffer.Create(device, width, height)) return false;
    return true;
}

void CubismRenderTargetManager_Dx9::Release()
{
    _maskBuffer.Destroy();
    _offscreenBuffer.Destroy();
}

bool CubismRenderTargetManager_Dx9::Resize(LPDIRECT3DDEVICE9 device, int width, int height)
{
    bool ok1 = _maskBuffer.Resize(device, width, height);
    bool ok2 = _offscreenBuffer.Resize(device, width, height);
    return ok1 && ok2;
}
