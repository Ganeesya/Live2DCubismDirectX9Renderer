/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * DirectX9 offscreen render target helper equivalent to CubismRenderTarget_OpenGLES2.
 */
#pragma once

#include <d3d9.h>
#include <d3dx9.h>

/**
 * Simple RAII wrapper for an offscreen render target (color texture + surface)
 * used for mask / offscreen drawing in the DirectX9 renderer.
 */
class CubismOffscreenFrame_Dx9
{
public:
    CubismOffscreenFrame_Dx9();
    ~CubismOffscreenFrame_Dx9();

    /** Create resources. Existing resources are released first. */
    bool Create(LPDIRECT3DDEVICE9 device, int width, int height, D3DFORMAT format = D3DFMT_A8R8G8B8);

    /** Release resources. */
    void Destroy();

    /** Begin drawing into the offscreen surface (stores previous RT + viewport). */
    void BeginDraw(LPDIRECT3DDEVICE9 device, bool clear = true, D3DCOLOR clearColor = 0x00000000);

    /** Finish drawing and restore previous render target + viewport. */
    void EndDraw(LPDIRECT3DDEVICE9 device);

    /** Resize (recreate) keeping same format. */
    bool Resize(LPDIRECT3DDEVICE9 device, int width, int height);

    /** Clear only (if already bound). */
    void Clear(LPDIRECT3DDEVICE9 device, D3DCOLOR clearColor = 0x00000000);

    bool IsValid() const { return _texture != nullptr && _surface != nullptr; }

    LPDIRECT3DTEXTURE9 GetTexture() const { return _texture; }
    LPDIRECT3DSURFACE9 GetSurface() const { return _surface; }

    int GetWidth() const { return _width; }
    int GetHeight() const { return _height; }

private:
    // non copyable
    CubismOffscreenFrame_Dx9(const CubismOffscreenFrame_Dx9&);
    CubismOffscreenFrame_Dx9& operator=(const CubismOffscreenFrame_Dx9&);

    void ReleaseInternal();

    LPDIRECT3DTEXTURE9 _texture;          ///< Color texture
    LPDIRECT3DSURFACE9 _surface;          ///< Surface level 0 of the texture (RT)

    // Stored state while drawing
    LPDIRECT3DSURFACE9 _prevSurface;      ///< Previous render target 0
    D3DVIEWPORT9       _prevViewport;     ///< Previous viewport
    bool               _drawing;          ///< In BeginDraw..EndDraw scope

    int _width;
    int _height;
    D3DFORMAT _format;
};

/**
 * Manager for a pair of offscreen targets (e.g. one for masks, one general purpose).
 * Optional utility mirroring the OpenGLES2 implementation pattern.
 */
class CubismRenderTargetManager_Dx9
{
public:
    CubismOffscreenFrame_Dx9& GetMaskBuffer() { return _maskBuffer; }
    CubismOffscreenFrame_Dx9& GetOffscreenBuffer() { return _offscreenBuffer; }

    bool Initialize(LPDIRECT3DDEVICE9 device, int width, int height);
    void Release();
    bool Resize(LPDIRECT3DDEVICE9 device, int width, int height);

private:
    CubismOffscreenFrame_Dx9 _maskBuffer;
    CubismOffscreenFrame_Dx9 _offscreenBuffer;
};
