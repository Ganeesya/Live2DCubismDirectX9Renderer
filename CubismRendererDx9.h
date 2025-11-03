#pragma once
#include "ipc_common.h"
#include "../src\LAppTextureDesc.h"
#include "../framework\L2DModelMatrix.h"
#include "../OWFramework\src\CubismFramework.hpp"
#include "../OWFramework\src\Model\CubismModel.hpp"
#include "../OWFramework\src\Rendering\CubismRenderer.hpp"
#include "../OWFramework\src\Id\CubismIdManager.hpp"
#include "../OWFramework\src\Model\CubismModelUserData.hpp"
#include "CubismRenderTargetDx9.h" // 追加: Offscreen 用


using namespace Live2DC3::Cubism::Framework;
using namespace Live2DC3::Cubism::Core;
using namespace Live2DC3::Cubism::Framework::Rendering;
using live2d::framework::L2DModelMatrix;

#define V(hr) if(((HRESULT)(hr)) < 0){live2d::UtDebug::error("%s(%d) DirectX Error[ %s ]\n",__FILE__,__LINE__,#hr);} 

// ブレンド定数（Color/Alpha）
namespace L2DBlend
{
	// カラーブレンド種別（Core::csmColorBlendType_* と同順）
	enum ColorBlendType
	{
		ColorBlend_Normal = 0,
		ColorBlend_Add = 1,
		ColorBlend_AddGlow = 2,
		ColorBlend_Darken = 3,
		ColorBlend_Multiply = 4,
		ColorBlend_ColorBurn = 5,
		ColorBlend_LinearBurn = 6,
		ColorBlend_Lighten = 7,
		ColorBlend_Screen = 8,
		ColorBlend_ColorDodge = 9,
		ColorBlend_Overlay = 10,
		ColorBlend_SoftLight = 11,
		ColorBlend_HardLight = 12,
		ColorBlend_LinearLight = 13,
		ColorBlend_Hue = 14,
		ColorBlend_Color = 15,
		ColorBlend_AddCompatible = 16,		// SetRenderState用
		ColorBlend_MultiplyCompatible = 17  // SetRenderState用
	};

	// アルファブレンド種別（Core::csmAlphaBlendType_* と同順）
	enum AlphaBlendType
	{
		AlphaBlend_Over = 0,			 // SetRenderState用
		AlphaBlend_Atop = 1,
		AlphaBlend_Out = 2,
		AlphaBlend_ConjointOver = 3,
		AlphaBlend_DisjointOver = 4
	};
}

struct L2DAPPVertex
{
	float x, y, z;
	float u, v;
};

// マスキング描画モード（scoped enum で cpp 側の DrawingMaskingMode::X 記法に対応）
enum class DrawingMaskingMode
{
	DrawMask,
	EraseMask,
	Skip
};

class DrawableShaderSetting
{
public:	
	DrawableShaderSetting();

	~DrawableShaderSetting();

	void Initialize(Csm::CubismModel* model, int drawindex, int& vertexCountStack, int& indiceCountStack);
	
	void DrawMesh(LPDIRECT3DDEVICE9 dev);

	void DrawMask(LPDIRECT3DDEVICE9 dev);

	void DrawMaskingMesh(LPDIRECT3DDEVICE9 dev);

	int GetMaskCount();

	int GetTextureIndex();
		
	int GetDrawableIndex();

	int GetMask(int i);

	D3DXCOLOR GetDiffuse();
	
	D3DXCOLOR GetMultipleColor();
	
	D3DXCOLOR GetScreenColor();

	void MixDiffuseColor(float opa, float r, float g, float b, float a);

	bool HaveElement(CubismIdHandle ele);

	bool HaveElements(csmVector<CubismIdHandle>& elements);

	void AddElements(const csmString& userDataValue);

	void SetDrawingMaskingType(DrawingMaskingMode set) { maskingType = set; }

	bool GetIsInvertMask() const { return isInvertMask; }

	int GetOffscreenIndex() const { return offscreenIndex; }

	// 追加: ブレンド/自己バッファ利用の取得
	csmInt32 GetColorBlendType() const { return colorBlendType; }
	csmInt32 GetAlphaBlendType() const { return alphaBlendType; }
	bool GetUsedSelfBuffer() const { return usedSelfBuffer; }

private:
	int vertexCount; int vertexStart; 
	int indiceCount; int indiceStart; 
	int drawableIndex; int textureIndex; 
	csmVector<int> masks; bool isInvertMask; 
	Rendering::CubismRenderer::CubismBlendMode drawtype; 
	DrawingMaskingMode maskingType; bool nonCulling; 
	D3DXCOLOR diffuse; 
	csmVector<CubismIdHandle> userdataElements; 
	int offscreenIndex; // 親パーツから辿ったオフスクリーンIndex (-1で無し)
	csmInt32 colorBlendType;
	csmInt32 alphaBlendType;
	bool usedSelfBuffer;
};

// 追加: Offscreen 用設定クラス（拡張）
class OffscreenShaderSetting
{
public:
	OffscreenShaderSetting()
		: _offscreenIndex(-1)
		, _transferOffscreenIndex(-1)
		, vertexCount(0)
		, vertexStart(0)
		, indiceCount(0)
		, indiceStart(0)
		, isInvertMask(false)
		, drawtype(Rendering::CubismRenderer::CubismBlendMode::CubismBlendMode_Normal)
		, maskingType(DrawingMaskingMode::DrawMask)
		, nonCulling(true)
		, diffuse(1,1,1,1)
	{}

	~OffscreenShaderSetting() { masks.Clear(); userdataElements.Clear(); }

	// offscreenIndex: このオフスクリーン自身のIndex
	// transferOffscreenIndex: 描画時に転写(ブリット)元となる別オフスクリーンIndex (-1で未設定/自前テクスチャ)
	void Initialize(Csm::CubismModel* model, csmInt32 offscreenIndex);

	csmInt32 GetOffscreenIndex() const { return _offscreenIndex; }
	csmInt32 GetTransferOffscreenIndex() const { return _transferOffscreenIndex; }

	// DrawableShaderSetting 互換 API
	void SetDrawSetting(LPDIRECT3DDEVICE9 dev);
	void DrawMask(LPDIRECT3DDEVICE9 dev);
	void DrawMaskingMesh(LPDIRECT3DDEVICE9 dev);
	int GetMaskCount();
	int GetTextureIndex();
	int GetMask(int i);
	D3DXCOLOR GetDiffuse();
	D3DXCOLOR GetMultipleColor();
	D3DXCOLOR GetScreenColor();
	void MixDiffuseColor(float opa, float r, float g, float b, float a);
	bool GetIsInvertMask() const { return isInvertMask; }
	void SetDrawingMaskingType(DrawingMaskingMode set) { maskingType = set; }

	// 公開用簡易初期設定（外部で直接設定される想定）
	void SetGeometry(int vStart, int vCount, int iStart, int iCount)
	{ vertexStart = vStart; vertexCount = vCount; indiceStart = iStart; indiceCount = iCount; }
	void SetMaskInfo(const csmVector<int>& src, bool invert) { masks = src; isInvertMask = invert; }
	void SetBlendAndCulling(Rendering::CubismRenderer::CubismBlendMode bt, bool noCull) { drawtype = bt; nonCulling = noCull; }
	
	csmInt32 GetColorBlendType() const { return colorBlendType; }
	csmInt32 GetAlphaBlendType() const { return alphaBlendType; }
	bool GetUsedSelfBuffer() const { return usedSelfBuffer; }

private:
	csmInt32 _offscreenIndex;              // このオフスクリーンのインデックス
	csmInt32 _transferOffscreenIndex;      // 転写元となるオフスクリーンインデックス (-1で自身/未使用)

	// Drawable 相当の保持情報
	int vertexCount; int vertexStart; int indiceCount; int indiceStart; 
	csmVector<int> masks; bool isInvertMask; 
	Rendering::CubismRenderer::CubismBlendMode drawtype; DrawingMaskingMode maskingType; bool nonCulling; 
	D3DXCOLOR diffuse; csmVector<CubismIdHandle> userdataElements;
	csmInt32 colorBlendType;
	csmInt32 alphaBlendType;
	bool usedSelfBuffer;
};

class CubismRendererDx9 : public CubismRenderer
{
public:
	static CubismRendererDx9* Create(int width, int height);

	CubismRendererDx9(int width, int height);
	~CubismRendererDx9();

	void Initialize(Csm::CubismModel* model, L2DModelMatrix* mat, CubismModelUserData* userdata);

	void DrawModel();
	
	void DrawMasking(bool selected, int mode, csmVector<CubismIdHandle>& ids);

	bool AddTexture(const char* filepath);

	void UpdateTex(int texNo, void* dataIndex, int nWidth, int nHeight);

	int GetUsedTextureCount();

	static	void SetD3D9Device(LPDIRECT3DDEVICE9 dev);

	static	void SetD3D9Masking(LPDIRECT3DTEXTURE9 masktex, LPDIRECT3DSURFACE9 masksurface);

	static	void SetD3D9Effecter(LPD3DXEFFECT	effect);

	void AddColorOnElement(CubismIdHandle ID, float opa, float r, float g, float b, float a);

protected:
	void DoDrawModel();

	void UpdateVertexs();

	void MakeMaskForDrawable(int tindex);
	void MakeMaskForOffscreen(int tindex);

	// 分離: Drawable と Offscreen 描画
	void DrawDrawable(DrawableShaderSetting* drawableSetting);
	void DrawOffscreen(OffscreenShaderSetting* offscreenSetting);

	// 追加: オフスクリーン転写処理（クラス関数化, 非static）
	void TransferOffscreenBuffer(
		LPDIRECT3DDEVICE9 dev,
		LPD3DXEFFECT effect,
		csmVector<CubismOffscreenFrame_Dx9*>& buffers,
		csmVector<OffscreenShaderSetting*>& settings,
		CubismModel* model,
		csmInt32 srcIndex);

	virtual void DrawMesh(csmInt32 textureNo, csmInt32 indexCount, csmInt32 vertexCount
		, csmUint16* indexArray, csmFloat32* vertexArray, csmFloat32* uvArray
		, csmFloat32 opacity, CubismBlendMode colorBlendMode, csmBool invertedMask)
	{

	}

	void SaveProfile(){}
	void RestoreProfile(){}

	void BeforeDrawModelRenderTarget() {}
	void AfterDrawModelRenderTarget() {}

	Csm::csmVector<LAppTextureDesc*> _textures;
	Csm::csmVector<DrawableShaderSetting*> _drawable;
	csmVector<LPDIRECT3DTEXTURE9> _nowTexture;

	// 追加: モデルの Offscreen 数に対応するオフスクリーンフレーム
	csmVector<CubismOffscreenFrame_Dx9*> _offscreenBuffers;
	csmVector<OffscreenShaderSetting*> _offscreenSettings; // Offscreen設定

	LPDIRECT3DVERTEXBUFFER9 _vertex;
	LPDIRECT3DINDEXBUFFER9  _indice;

	L2DModelMatrix* _mtrx;
	
	static	LPDIRECT3DDEVICE9 g_dev;
	static	LPDIRECT3DTEXTURE9 g_maskTexture;
	static	LPDIRECT3DSURFACE9 g_maskSurface;
	static  LPD3DXEFFECT		g_effect;
};

