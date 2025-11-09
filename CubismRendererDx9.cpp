#include "CubismRendererDx9.h"
#include <algorithm>

// CubismRenderer 静的ファクトリ実装（リンクエラー対策）
Live2DC3::Cubism::Framework::Rendering::CubismRenderer* Live2DC3::Cubism::Framework::Rendering::CubismRenderer::Create(csmUint32 width, csmUint32 height)
{
	CubismRendererDx9* r = CubismRendererDx9::Create(width, height);
	r->SetRenderTargetSize(width, height);
	return r;
}


LPDIRECT3DDEVICE9	CubismRendererDx9::g_dev = NULL;
LPDIRECT3DTEXTURE9	CubismRendererDx9::g_maskTexture = NULL;
LPDIRECT3DSURFACE9	CubismRendererDx9::g_maskSurface = NULL;
LPD3DXEFFECT		CubismRendererDx9::g_effect = NULL;

// 現在 Begin 中のオフスクリーンを追跡（フレーム内）
static csmInt32 s_ActiveOffscreenIndex = -1;

// RTコピー用
static LPDIRECT3DTEXTURE9 s_rtCopyTex = NULL;
static csmUint32 s_rtCopyW = 0, s_rtCopyH = 0;

static bool getUseCopyBuffer(csmInt32 colorBlendType, csmInt32 alphaBlendType) {
	switch (colorBlendType)
	{
	case ::csmColorBlendType_AddCompatible: // Additive(compatible)
	case ::csmColorBlendType_MultiplyCompatible: // Multiplicative(compatible)
		return false;
	case ::csmColorBlendType_Normal:
		break;
	default:
		return true;
	}
	if( alphaBlendType == L2DBlend::AlphaBlendType::AlphaBlend_Over ) {
		return false;
	}
	return true;
}

static void EnsureRtCopyTexture(LPDIRECT3DDEVICE9 dev, csmUint32 w, csmUint32 h)
{
	if (s_rtCopyTex && s_rtCopyW == w && s_rtCopyH == h) return;
	if (s_rtCopyTex) { s_rtCopyTex->Release(); s_rtCopyTex = NULL; }
	s_rtCopyW = w; s_rtCopyH = h;
	V(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_rtCopyTex, NULL));
}

static void CopyCurrentRTToTexture(LPDIRECT3DDEVICE9 dev)
{
	LPDIRECT3DSURFACE9 src = NULL; V(dev->GetRenderTarget(0, &src));
	D3DSURFACE_DESC d; src->GetDesc(&d);
	EnsureRtCopyTexture(dev, d.Width, d.Height);
	LPDIRECT3DSURFACE9 dst = NULL; V(s_rtCopyTex->GetSurfaceLevel(0, &dst));
	V(dev->StretchRect(src, NULL, dst, NULL, D3DTEXF_NONE));
	if (dst) dst->Release();
	if (src) src->Release();
}

// オフスクリーンの内容を転写（SRCALPHA考慮）
void CubismRendererDx9::TransferOffscreenBuffer(
	LPDIRECT3DDEVICE9 dev,
	LPD3DXEFFECT effect,
	csmVector<CubismOffscreenFrame_Dx9*>& buffers,
	csmVector<OffscreenShaderSetting*>& settings,
	CubismModel* model,
	csmInt32 srcIndex)
{
	if (!dev || !effect) return;
	if (srcIndex < 0 || srcIndex >= (csmInt32)buffers.GetSize()) return;
	CubismOffscreenFrame_Dx9* srcOff = buffers[srcIndex];
	if (!srcOff || !srcOff->IsValid()) return;
	LPDIRECT3DTEXTURE9 srcTex = srcOff->GetTexture();
	if (!srcTex) return;
	// 転写先（-1 は現在のRT）
	OffscreenShaderSetting* setting = NULL;
	csmInt32 dstIndex = -1;
	if (srcIndex < (csmInt32)settings.GetSize() && settings[srcIndex])
	{
		dstIndex = settings[srcIndex]->GetTransferOffscreenIndex();
		setting = settings[srcIndex];
	}
	else {
		return;
	}

	CubismLogVerbose("offsetBake index:%d target:%d", srcIndex, dstIndex);

	// パーツ（オーナーDrawable）の不透明度を取得
	float ownerOpacity = model->GetOffscreenOpacity(srcIndex);
	

	// シーンを一旦終了し、RT切替
	V(dev->EndScene());

	LPDIRECT3DSURFACE9 prevRT = nullptr;
	V(dev->GetRenderTarget(0, &prevRT));
	D3DVIEWPORT9 prevVP; V(dev->GetViewport(&prevVP));
	D3DXMATRIXA16 prevWorld, prevView, prevProj;
	V(dev->GetTransform(D3DTS_WORLD, &prevWorld));
	V(dev->GetTransform(D3DTS_VIEW, &prevView));
	V(dev->GetTransform(D3DTS_PROJECTION, &prevProj));
	D3DXMATRIXA16 prevFxMvp; ZeroMemory(&prevFxMvp, sizeof(prevFxMvp));
	effect->GetMatrix("g_mWorldViewProjection", &prevFxMvp);

	// 転写先サーフェス決定
	LPDIRECT3DSURFACE9 dstSurf = prevRT;
	if (dstIndex >= 0 && dstIndex < (csmInt32)buffers.GetSize())
	{
		CubismOffscreenFrame_Dx9* dstOff = buffers[dstIndex];
		if (dstOff && dstOff->IsValid())
		{
			dstSurf = dstOff->GetSurface();
		}
	}

	// ここで「dstSurf の直前の中身」を s_rtCopyTex にコピーする（RT切替前）
	if (dstSurf)
	{
		D3DSURFACE_DESC srcDesc; dstSurf->GetDesc(&srcDesc);
		EnsureRtCopyTexture(dev, srcDesc.Width, srcDesc.Height);

		LPDIRECT3DSURFACE9 dstLevel0 = nullptr;
		V(s_rtCopyTex->GetSurfaceLevel(0, &dstLevel0));

		if (srcDesc.MultiSampleType != D3DMULTISAMPLE_NONE)
		{
			// MSAA → 非MSAAへ一旦解決してから Texture へコピー
			LPDIRECT3DSURFACE9 tmp = nullptr;
			V(dev->CreateRenderTarget(srcDesc.Width, srcDesc.Height, srcDesc.Format,
				D3DMULTISAMPLE_NONE, 0, FALSE, &tmp, NULL));
			if (tmp)
			{
				V(dev->StretchRect(dstSurf, nullptr, tmp, nullptr, D3DTEXF_NONE));
				V(dev->StretchRect(tmp, nullptr, dstLevel0, nullptr, D3DTEXF_NONE));
				tmp->Release();
			}
		}
		else
		{
			V(dev->StretchRect(dstSurf, nullptr, dstLevel0, nullptr, D3DTEXF_NONE));
		}

		if (dstLevel0) dstLevel0->Release();

		// XRGB系のときはαが0になる対策フラグ（PS側で alpha=1 に補正）
		const bool outBufIsXRGB =
			(srcDesc.Format == D3DFMT_X8R8G8B8) || (srcDesc.Format == D3DFMT_X8B8G8R8);
		//V(effect->SetBool("outBufIsXRGB", outBufIsXRGB));
	}

	// 以降、描画RTを設定
	if (dstSurf) { V(dev->SetRenderTarget(0, dstSurf)); }

	if (setting->GetMaskCount() > 0)
	{
		MakeMaskForOffscreen(setting->GetOffscreenIndex());
		V(g_effect->SetTexture("Mask", g_maskTexture));
		V(g_effect->SetTechnique("TransferOffscreenMasked"));
		V(g_effect->SetBool("isInvertMask", setting->GetIsInvertMask()));
	}
	else
	{
		V(effect->SetTechnique("TransferOffscreenNomask"));
	}

	// 転写先に合わせてビューポート設定
	D3DSURFACE_DESC dstDesc; dstSurf->GetDesc(&dstDesc);
	D3DVIEWPORT9 vp; vp.X = 0; vp.Y = 0; vp.MinZ = 0.0f; vp.MaxZ = 1.0f; vp.Width = dstDesc.Width; vp.Height = dstDesc.Height;
	V(dev->SetViewport(&vp));

	// 行列をアイデンティティへ
	D3DXMATRIXA16 id; D3DXMatrixIdentity(&id);
	V(dev->SetTransform(D3DTS_WORLD, &id));
	V(dev->SetTransform(D3DTS_VIEW, &id));
	V(dev->SetTransform(D3DTS_PROJECTION, &id));
	V(effect->SetMatrix("g_mWorldViewProjection", &id));

	V(dev->BeginScene());
	V(dev->SetRenderState(D3DRS_LIGHTING, FALSE));

	// テクスチャ
	V(effect->SetTexture("TexMain", srcTex));
	
	D3DXVECTOR2 screenSize = D3DXVECTOR2(-1.0f / dstDesc.Width, -1.0f / dstDesc.Height);
	V(effect->SetValue("OutputBufferTexelSize", &screenSize, sizeof(D3DXVECTOR2) ));

	// ブレンドタイプを渡す
	V(effect->SetInt("colorBlendType", setting->GetColorBlendType()));
	V(effect->SetInt("alphaBlendType", setting->GetAlphaBlendType()));

	// usedSelfBuffer が真なら現在のRTをテクスチャコピーして渡す
	if (setting->GetUsedSelfBuffer())
	{
		V(effect->SetTexture("OutputBuffer", s_rtCopyTex));
		V(effect->SetBool("useOutBuffer", true));
	}
	else
	{
		V(effect->SetTexture("OutputBuffer", NULL));
		V(effect->SetBool("useOutBuffer", false));
	}
	V(effect->SetFloat("drawableOpacity", ownerOpacity));

	// フルスクリーンクワッド（頂点カラーに不透明度適用）
	struct TLVertex { float x, y, z, rhw; DWORD diffuse; float u, v; };
	const DWORD TL_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	TLVertex quad[4];
	const float w = static_cast<float>(dstDesc.Width);
	const float h = static_cast<float>(dstDesc.Height);
	const float ox = -0.5f, oy = -0.5f;
	DWORD vcol = D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, 1.0f);
	quad[0] = { 0.0f + ox, 0.0f + oy, 0.0f, 1.0f, vcol, 0.0f, 0.0f };
	quad[1] = { w    + ox, 0.0f + oy, 0.0f, 1.0f, vcol, 1.0f, 0.0f };
	quad[2] = { 0.0f + ox, h    + oy, 0.0f, 1.0f, vcol, 0.0f, 1.0f };
	quad[3] = { w    + ox, h    + oy, 0.0f, 1.0f, vcol, 1.0f, 1.0f };
	V(dev->SetFVF(TL_FVF));

	V(effect->SetBool("isPremultipliedAlpha", false));
	setting->SetDrawSetting(dev);

	UINT32 passes; V(effect->Begin(&passes, 0)); V(effect->BeginPass(0));
	V(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TLVertex)));
	V(effect->EndPass()); V(effect->End());


	// 後処理
	V(dev->SetTexture(0, NULL));
	V(dev->EndScene());

	// 復元
	if (prevRT)
	{
		V(dev->SetRenderTarget(0, prevRT));
		prevRT->Release();
	}


	V(dev->SetViewport(&prevVP));
	V(dev->SetTransform(D3DTS_WORLD, &prevWorld));
	V(dev->SetTransform(D3DTS_VIEW, &prevView));
	V(dev->SetTransform(D3DTS_PROJECTION, &prevProj));
	V(effect->SetMatrix("g_mWorldViewProjection", &prevFxMvp));
	V(dev->BeginScene());

	V(dev->SetIndices(_indice));
	V(dev->SetStreamSource(0, _vertex, 0, sizeof(L2DAPPVertex)));

	V(dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1));

	CubismLogVerbose("currentOffscrennChange %d -> %d %s color:%2d alpha:%2d",
		s_ActiveOffscreenIndex,
		dstIndex, setting->GetUsedSelfBuffer() ? "tr" : "fa",
		setting->GetColorBlendType(),
		setting->GetAlphaBlendType());
	s_ActiveOffscreenIndex = dstIndex;
}


CubismRendererDx9 * CubismRendererDx9::Create(int width, int height)
{
	return new CubismRendererDx9(width, height);
}

CubismRendererDx9::CubismRendererDx9(int width, int height)
	: CubismRenderer(width, height)
	, _vertex(nullptr)
	, _indice(nullptr)
{
}


CubismRendererDx9::~CubismRendererDx9()
{
	for (csmUint32 i = 0; i < _textures.GetSize(); ++i)
	{
		delete _textures[i];
	}

	for (csmUint32 i = 0; i < _drawable.GetSize(); ++i)
	{
		delete _drawable[i];
	}

	for (csmUint32 i = 0; i < _offscreenBuffers.GetSize(); ++i)
	{
		if (_offscreenBuffers[i]) { _offscreenBuffers[i]->Destroy(); delete _offscreenBuffers[i]; }
	}
	_offscreenBuffers.Clear();

	for (csmUint32 i = 0; i < _offscreenSettings.GetSize(); ++i) { delete _offscreenSettings[i]; }
	_offscreenSettings.Clear();

	if (_vertex) { _vertex->Release(); _vertex = NULL; }
	if (_indice) { _indice->Release(); _indice = NULL; }

	if (s_rtCopyTex) { s_rtCopyTex->Release(); s_rtCopyTex = NULL; s_rtCopyW = s_rtCopyH = 0; }
}

/**
* @brief   
*           
*
* @param[in]  model -> 
*/

void CubismRendererDx9::Initialize(CubismModel * model, L2DModelMatrix * mat, CubismModelUserData* userdata)
{
	CubismRenderer::Initialize(model);
	_mtrx = mat;

	int vertexTotalSize = 0;
	int indexTotalSize = 0;
	for (csmInt32 i = 0; i < model->GetDrawableCount(); ++i)
	{
		vertexTotalSize += model->GetDrawableVertexCount(i);
		indexTotalSize += model->GetDrawableVertexIndexCount(i);
	}

	// 既存バッファ解放（再初期化パスを考慮）
	if (_vertex) { _vertex->Release(); _vertex = nullptr; }
	if (_indice) { _indice->Release(); _indice = nullptr; }

	V(g_dev->CreateVertexBuffer(sizeof(L2DAPPVertex) * vertexTotalSize,
		0,
		D3DFVF_XYZ | D3DFVF_TEX1,
		D3DPOOL_MANAGED,
		&_vertex,
		NULL));
	V(g_dev->CreateIndexBuffer(sizeof(WORD) * indexTotalSize,
		0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &_indice, NULL));

	L2DAPPVertex* vertexBuffer;
	if (FAILED(_vertex->Lock(0, 0, (void**)&vertexBuffer, D3DLOCK_DISCARD)))
	{
		return;
	}
	int* indiceBuffer;
	if (FAILED(_indice->Lock(0, 0, (void**)&indiceBuffer, D3DLOCK_DISCARD)))
	{
		return;
	}

	memcpy(indiceBuffer, csmGetDrawableIndices(model->GetModel())[0], sizeof(WORD) * indexTotalSize);

	_drawable.Resize(model->GetDrawableCount());

	int vertexCountStack = 0;
	int indiceCountStack = 0;
	for (csmInt32 i = 0; i < model->GetDrawableCount(); ++i)
	{
		for (int j = 0; j < model->GetDrawableVertexCount(i); ++j)
		{
			vertexBuffer[vertexCountStack + j].x = model->GetDrawableVertexPositions(i)[j].X;
			vertexBuffer[vertexCountStack + j].y = model->GetDrawableVertexPositions(i)[j].Y;
			vertexBuffer[vertexCountStack + j].z = 0;
			vertexBuffer[vertexCountStack + j].u = model->GetDrawableVertexUvs(i)[j].X;
			vertexBuffer[vertexCountStack + j].v = 1.0f - model->GetDrawableVertexUvs(i)[j].Y;
		}

		_drawable[i] = new DrawableShaderSetting();
		_drawable[i]->Initialize(model, i, vertexCountStack, indiceCountStack);
		if (userdata != NULL)
		{
			for (csmUint32 k = 0; k < userdata->GetArtMeshUserDatas().GetSize(); ++k)
			{
				if (userdata->GetArtMeshUserDatas()[k]->TargetId == GetModel()->GetDrawableId(i))
				{
					_drawable[i]->AddElements(userdata->GetArtMeshUserDatas()[k]->Value);
				}
			}
		}
	}

	_vertex->Unlock();
	_indice->Unlock();

	// モデルのオフスクリーン数に応じたレンダーターゲットを確保
	for (csmUint32 i = 0; i < _offscreenBuffers.GetSize(); ++i)
	{
		if (_offscreenBuffers[i]) { _offscreenBuffers[i]->Destroy(); delete _offscreenBuffers[i]; }
	}
	_offscreenBuffers.Clear();
	for (csmUint32 i = 0; i < _offscreenSettings.GetSize(); ++i) { delete _offscreenSettings[i]; }
	_offscreenSettings.Clear();

	csmInt32 offscreenCount = model->GetOffscreenCount();
	if (offscreenCount > 0)
	{
		_offscreenBuffers.Resize(offscreenCount);
		_offscreenSettings.Resize(offscreenCount);
		for (csmInt32 i = 0; i < offscreenCount; ++i)
		{
			_offscreenBuffers[i] = new CubismOffscreenFrame_Dx9();
			_offscreenBuffers[i]->Create(g_dev, _modelRenderTargetWidth, _modelRenderTargetHeight, D3DFMT_A8R8G8B8);


			_offscreenSettings[i] = new OffscreenShaderSetting();
			_offscreenSettings[i]->Initialize(model, i); // ownerPartsIndex を転写元として暫定利用
		}
	}
}

/**
* @brief   
*
*/

void CubismRendererDx9::DrawModel()
{
	SaveProfile();

	DoDrawModel();

	RestoreProfile();
}

bool HitId(csmVector<CubismIdHandle>& ids, CubismIdHandle& target) {
	for (auto i = ids.Begin(); i != ids.End(); i++) {
		if (*i == target) {
			return true;
		}
	}
	return false;
}

void CubismRendererDx9::DrawMasking(
							bool selected,
							int mode,
							csmVector<CubismIdHandle>& ids) 
{
	if (!selected) return;

	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); ++i)
	{
		CubismIdHandle drawableId = GetModel()->GetDrawableId(i);
		bool isFit = false;

		if ( (mode & MASKING_MODE_ID_FIT_FLAG) != 0 ) {
			isFit |= HitId(ids, drawableId);
		}
		if ( (mode & MASKING_MODE_USERDATA_FIT_FLAG) != 0 ) {
			isFit |= _drawable[i]->HaveElements(ids);
		}

		if ( (mode & MASKING_MODE_FITTING_MEAN_INVERT) != 0 ) {
			isFit = !isFit;
		}

		DrawingMaskingMode setMode = DrawingMaskingMode::DrawMask;

		if (!isFit) {
			if ( (mode & MASKING_MODE_NONFIT_SKIP) != 0 ) {
				setMode = DrawingMaskingMode::Skip;
			}
			else {
				setMode = DrawingMaskingMode::EraseMask;
			}
		}
		_drawable[i]->SetDrawingMaskingType(setMode);
	}

	SaveProfile();
	
	D3DXMATRIXA16 view;
	V(g_dev->GetTransform(D3DTS_VIEW, &view));

	D3DXMATRIXA16 projection;
	V(g_dev->GetTransform(D3DTS_PROJECTION, &projection));

	D3DXMATRIXA16 mmvp;
	D3DXMatrixIdentity(&mmvp);

	D3DXMATRIXA16 modelMat(_mtrx->getArray());

	D3DXMATRIXA16 normalizeMat;
	D3DXMatrixIdentity(&normalizeMat);
	normalizeMat._11 *= -1;
	normalizeMat._41 += 0.5;
	normalizeMat._42 -= 0.5;

	D3DXMatrixMultiply(&mmvp, &mmvp, &normalizeMat);
	D3DXMatrixMultiply(&mmvp, &mmvp, &modelMat);
	D3DXMatrixMultiply(&mmvp, &mmvp, &view);
	D3DXMatrixMultiply(&mmvp, &mmvp, &projection);

	V(g_effect->SetMatrix("g_mWorldViewProjection", &mmvp));
	V(g_effect->SetBool("isPremultipliedAlpha", IsPremultipliedAlpha()));

	UpdateVertexs();
	
	for (csmUint32 i = 0; i < _textures.GetSize(); ++i)
	{
		_nowTexture[i] = _textures[i]->getNowTex();
	}

	V(g_dev->SetIndices(_indice));
	V(g_dev->SetStreamSource(0, _vertex, 0, sizeof(L2DAPPVertex)));

	V(g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1));

	csmVector<DrawableShaderSetting*> sort(GetModel()->GetDrawableCount());

	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); ++i)
	{
		sort[GetModel()->GetRenderOrders()[i]] = _drawable[i];
	}

	V(g_dev->SetRenderState(D3DRS_LIGHTING, FALSE));

	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); i++)
	{
		if (!GetModel()->GetDrawableDynamicFlagIsVisible(sort[i]->GetDrawableIndex()))
		{
			sort[i]->GetDiffuse();
			continue;
		}
		float opa = GetModel()->GetDrawableOpacity(sort[i]->GetDrawableIndex());
		if (sort[i]->GetMaskCount() > 0)
		{
			MakeMaskForDrawable(sort[i]->GetDrawableIndex());

			V(g_effect->SetTexture("Mask", g_maskTexture));

			V(g_effect->SetTechnique("RenderMaskingMasked"));
			V(g_effect->SetBool("isInvertMask", sort[i]->GetIsInvertMask()));
		}
		else
		{
			V(g_effect->SetTechnique("RenderMaskingNoMask"));
		}

		V(g_effect->SetFloat("drawableOpacity", opa));

		CubismTextureColor modelColor = GetModelColor();
		D3DXCOLOR modelDiffuse = D3DXCOLOR(modelColor.R, modelColor.G, modelColor.B, modelColor.A);
		D3DXCOLOR diffuse = D3DXCOLOR(1, 1, 1, 1);
		diffuse.r *= modelDiffuse.r;
		diffuse.g *= modelDiffuse.g;
		diffuse.b *= modelDiffuse.b;
		diffuse.a *= modelDiffuse.a;
		V(g_effect->SetValue("drawableDiffuse", &diffuse, sizeof(D3DXCOLOR)));
		V(g_effect->SetValue("baseColor", &diffuse, sizeof(D3DXCOLOR)));

		csmVector4 multiColorCsm = GetModel()->GetDrawableMultiplyColor(sort[i]->GetDrawableIndex());
		D3DXCOLOR multiColor = D3DXCOLOR(multiColorCsm.X, multiColorCsm.Y, multiColorCsm.Z, multiColorCsm.W);
		V(g_effect->SetValue("multiplyColor", &multiColor, sizeof(D3DXCOLOR)));
		csmVector4 screenColorCsm = GetModel()->GetDrawableScreenColor(sort[i]->GetDrawableIndex());
		D3DXCOLOR screenColor = D3DXCOLOR(screenColorCsm.X, screenColorCsm.Y, screenColorCsm.Z, screenColorCsm.W);
		V(g_effect->SetValue("screenColor", &screenColor, sizeof(D3DXCOLOR)));

		int texnum = sort[i]->GetTextureIndex();
		LPDIRECT3DTEXTURE9 tex = _nowTexture[texnum];
		V(g_effect->SetTexture("TexMain", tex));

		// ブレンドタイプはここでも渡す（マスキング描画の一貫性のため）
		V(g_effect->SetInt("colorBlendType", _drawable[sort[i]->GetDrawableIndex()]->GetColorBlendType()));
		V(g_effect->SetInt("alphaBlendType", _drawable[sort[i]->GetDrawableIndex()]->GetAlphaBlendType()));

		UINT32	xxxx;
		V(g_effect->Begin(&xxxx, 0));
		V(g_effect->BeginPass(0));
		sort[i]->DrawMaskingMesh(g_dev);
		V(g_effect->EndPass());
		V(g_effect->End());
	}

	RestoreProfile();
}

bool CubismRendererDx9::AddTexture(const char * filepath)
{
	LPDIRECT3DTEXTURE9 newtex;
	if (FAILED(D3DXCreateTextureFromFileExA(g_dev
		, filepath
		, 0
		, 0
		, 0
		, 0
		, D3DFMT_A8R8G8B8
		, D3DPOOL_MANAGED
		, D3DX_FILTER_LINEAR
		, D3DX_FILTER_BOX
		, 0
		, NULL
		, NULL
		, &newtex)))
	{
		return false;
	}
	else
	{
		_textures.PushBack(new LAppTextureDesc(newtex));
		_nowTexture.Resize(_textures.GetSize());
		return true;
	}
}

void CubismRendererDx9::UpdateTex(int texNo, void * dataIndex, int nWidth, int nHeight)
{
	if (_textures.GetSize() <= static_cast<size_t>(texNo))
		return;
	LAppTextureDesc* target = (LAppTextureDesc*)_textures[texNo];
	if (target == NULL)
		return;
	if (!target->isSameSize(nWidth, nHeight))
	{
		LPDIRECT3DTEXTURE9 ntex;
		if (FAILED(g_dev->CreateTexture(nWidth, nHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ntex, NULL)))
		{
			return;
		}
		target->changeData(ntex);
		target->UpdateTexture((DWORD*)dataIndex, nWidth, nHeight);
	}
	else
	{
		target->UpdateTexture((DWORD*)dataIndex, nWidth, nHeight);
	}
}

int CubismRendererDx9::GetUsedTextureCount()
{
	int maxUse = -1;
	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); ++i)
	{
		maxUse = max(maxUse, GetModel()->GetDrawableTextureIndices(i));
	}
	return maxUse + 1;
}

void CubismRendererDx9::SetD3D9Device(LPDIRECT3DDEVICE9 dev)
{
	g_dev = dev;
}

void CubismRendererDx9::SetD3D9Masking(LPDIRECT3DTEXTURE9 masktex, LPDIRECT3DSURFACE9 masksurface)
{
	g_maskSurface = masksurface;
	g_maskTexture = masktex;
}

void CubismRendererDx9::SetD3D9Effecter(LPD3DXEFFECT effect)
{
	g_effect = effect;
}

void CubismRendererDx9::AddColorOnElement(CubismIdHandle ID, float opa, float r, float g, float b, float a)
{
	for (csmUint32 i = 0; i < _drawable.GetSize(); ++i)
	{
		if (_drawable[i]->HaveElement(ID))
		{
			_drawable[i]->MixDiffuseColor(opa, r, g, b, a);
		}
	}
}

 void CubismRendererDx9::DoDrawModel()
{
    D3DXMATRIXA16 view;
    V(g_dev->GetTransform(D3DTS_VIEW, &view));

    D3DXMATRIXA16 projection;
    V(g_dev->GetTransform(D3DTS_PROJECTION, &projection));

    D3DXMATRIXA16 mmvp;
    D3DXMatrixIdentity(&mmvp);

    D3DXMATRIXA16 modelMat(_mtrx->getArray());
    
    D3DXMATRIXA16 normalizeMat;
    D3DXMatrixIdentity(&normalizeMat);
    normalizeMat._11 *= -1;
    normalizeMat._41 += 0.5f;
    normalizeMat._42 -= 0.5f;

    D3DXMatrixMultiply(&mmvp, &mmvp, &normalizeMat);
    D3DXMatrixMultiply(&mmvp, &mmvp, &modelMat);
    D3DXMatrixMultiply(&mmvp, &mmvp, &view);
    D3DXMatrixMultiply(&mmvp, &mmvp, &projection);

    V(g_effect->SetMatrix("g_mWorldViewProjection", &mmvp));
	V(g_effect->SetBool("isPremultipliedAlpha", IsPremultipliedAlpha()));

    // 頂点更新
    UpdateVertexs();

    // テクスチャ更新
    for (csmUint32 i = 0; i < _textures.GetSize(); ++i)
    {
        _nowTexture[i] = _textures[i]->getNowTex();
    }

    V(g_dev->SetIndices(_indice));
    V(g_dev->SetStreamSource(0, _vertex, 0, sizeof(L2DAPPVertex)));
    V(g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1));

    csmInt32 drawableCount  = GetModel()->GetDrawableCount();
    csmInt32 offscreenCount = GetModel()->GetOffscreenCount();

    // 描画情報を Drawable と Offscreen で分離して保持
    struct RenderEntry
    {
        csmInt32 Order;      // ソート後の描画順序
        csmInt32 Index;      // DrawableIndex or OffscreenIndex
        bool     IsOffscreen;
    };

    csmVector<RenderEntry> entries;
    entries.Resize(drawableCount + offscreenCount);

    // 一旦 push するためのカウンタ
    csmInt32 pushPos = 0;

    // Drawable 分
    for (csmInt32 i = 0; i < drawableCount; ++i)
    {
        RenderEntry e;
        e.Order = GetModel()->GetRenderOrders()[i];
        e.Index = i;
        e.IsOffscreen = false;
        entries[pushPos++] = e;
    }
    // Offscreen 分（描画順配列の後半にあると想定）
    for (csmInt32 i = 0; i < offscreenCount; ++i)
    {
        RenderEntry e;
        e.Order = GetModel()->GetRenderOrders()[drawableCount + i];
        e.Index = i; // OffscreenIndex
        e.IsOffscreen = true;
        entries[pushPos++] = e;
    }

    // 実際の描画順でソート (昇順: 小さいOrderが先)
    for (csmInt32 i = 1; i < entries.GetSize(); ++i)
    {
        RenderEntry key = entries[i];
        csmInt32 j = i - 1;
        while (j >= 0 && entries[j].Order > key.Order)
        {
            entries[j + 1] = entries[j];
            --j;
        }
        entries[j + 1] = key;
    }

    // ライティング OFF
    V(g_dev->SetRenderState(D3DRS_LIGHTING, FALSE));

    // ソート済みエントリを順に描画
    for (csmInt32 i = 0; i < entries.GetSize(); ++i)
    {
        const RenderEntry& e = entries[i];
        if (e.IsOffscreen)
        {
            DrawOffscreen(_offscreenSettings[e.Index]);
			CubismLogVerbose("%d offset index:%d target:%d id:%s useBuff:%s color:%2d alpha:%2d",
				i, e.Index,
				_offscreenSettings[e.Index]->GetTransferOffscreenIndex(),
				GetModel()->GetOffscreenOwnerId(e.Index)->GetString().GetRawString(),
				_offscreenSettings[e.Index]->GetUsedSelfBuffer() ? "true " : "false",
				_offscreenSettings[e.Index]->GetColorBlendType(),
				_offscreenSettings[e.Index]->GetAlphaBlendType());
        }
        else
        {
            DrawDrawable(_drawable[e.Index]);
            CubismLogVerbose("%d drawable index:%d target:%d id:%s useBuff:%s color:%2d alpha:%2d", 
				i, e.Index,
				_drawable[e.Index]->GetOffscreenIndex(),
				GetModel()->GetDrawableId(e.Index)->GetString().GetRawString(),
				_drawable[e.Index]->GetUsedSelfBuffer() ? "true " : "false",
				_drawable[e.Index]->GetColorBlendType(),
				_drawable[e.Index]->GetAlphaBlendType());
        }
    }

    // フレーム末尾でオフスクリーンが開いていれば閉じて転写
	while (s_ActiveOffscreenIndex >= 0 && s_ActiveOffscreenIndex < (csmInt32)_offscreenBuffers.GetSize())
    {
        CubismOffscreenFrame_Dx9* off = _offscreenBuffers[s_ActiveOffscreenIndex];
        if (off && off->IsValid()) { off->EndDraw(g_dev); }
		TransferOffscreenBuffer(g_dev, g_effect, _offscreenBuffers, _offscreenSettings, GetModel(), s_ActiveOffscreenIndex);
    }
}

void CubismRendererDx9::DrawOffscreen(OffscreenShaderSetting* offscreenSetting)
{
	// Offscreen エントリ＝開始合図として扱う（転写はクローズ時に実施）
	if (!offscreenSetting) return;
	csmInt32 idx = offscreenSetting->GetOffscreenIndex();
	if (idx < 0 || idx >= (csmInt32)_offscreenBuffers.GetSize()) return;
	CubismOffscreenFrame_Dx9* target = _offscreenBuffers[idx];
	if (!target || !target->IsValid()) return;

	// 既に他のオフスクリーンが Begin 済みなら閉じて転写
	while (s_ActiveOffscreenIndex >= 0 && s_ActiveOffscreenIndex != offscreenSetting->GetTransferOffscreenIndex())
	{
		CubismOffscreenFrame_Dx9* prev = _offscreenBuffers[s_ActiveOffscreenIndex];
		if (prev && prev->IsValid()) { prev->EndDraw(g_dev); }
		TransferOffscreenBuffer(g_dev, g_effect, _offscreenBuffers, _offscreenSettings, GetModel(), s_ActiveOffscreenIndex);
	}

	// まだ Begin していなければ開始（クリア: 透明）
	target->BeginDraw(g_dev, true, D3DCOLOR_ARGB(0, 0, 0, 0));
	s_ActiveOffscreenIndex = idx;
}

void CubismRendererDx9::DrawDrawable(DrawableShaderSetting* drawableSetting)
{
	if (!drawableSetting) return;
	if (!GetModel()->GetDrawableDynamicFlagIsVisible(drawableSetting->GetDrawableIndex())) { drawableSetting->GetDiffuse(); return; }

	// アクティブなオフスクリーンと対象DrawableのオフスクリーンIndexが異なる場合はクローズ＋転写
	while (s_ActiveOffscreenIndex >= 0 && drawableSetting->GetOffscreenIndex() != s_ActiveOffscreenIndex)
	{
		CubismOffscreenFrame_Dx9* prev = _offscreenBuffers[s_ActiveOffscreenIndex];
		if (prev && prev->IsValid()) { prev->EndDraw(g_dev); }
		TransferOffscreenBuffer(g_dev, g_effect, _offscreenBuffers, _offscreenSettings, GetModel(), s_ActiveOffscreenIndex);
	}

	float opa = GetModel()->GetDrawableOpacity(drawableSetting->GetDrawableIndex());

	if (drawableSetting->GetMaskCount() > 0)
	{
		MakeMaskForDrawable(drawableSetting->GetDrawableIndex());
		V(g_effect->SetTexture("Mask", g_maskTexture));
		V(g_effect->SetTechnique("RenderSceneMasked"));
		V(g_effect->SetBool("isInvertMask", drawableSetting->GetIsInvertMask()));
	}
	else
	{
		V(g_effect->SetTechnique("RenderSceneNomask"));
	}

	V(g_effect->SetFloat("drawableOpacity", opa));
	CubismTextureColor modelColor = GetModelColor();
	D3DXCOLOR modelDiffuse = D3DXCOLOR(modelColor.R, modelColor.G, modelColor.B, modelColor.A);
	D3DXCOLOR diffuse = drawableSetting->GetDiffuse();
	diffuse.r *= modelDiffuse.r; diffuse.g *= modelDiffuse.g; diffuse.b *= modelDiffuse.b; diffuse.a *= modelDiffuse.a;
	V(g_effect->SetValue("drawableDiffuse", &diffuse, sizeof(D3DXCOLOR)));

	int texnum = drawableSetting->GetTextureIndex();
	LPDIRECT3DTEXTURE9 tex = _nowTexture[texnum];
	V(g_effect->SetTexture("TexMain", tex));

	csmVector4 multiColorCsm = GetModel()->GetDrawableMultiplyColor(drawableSetting->GetDrawableIndex());
	D3DXCOLOR multiColor = D3DXCOLOR(multiColorCsm.X, multiColorCsm.Y, multiColorCsm.Z, multiColorCsm.W);
	V(g_effect->SetValue("multiplyColor", &multiColor, sizeof(D3DXCOLOR)));
	csmVector4 screenColorCsm = GetModel()->GetDrawableScreenColor(drawableSetting->GetDrawableIndex());
	D3DXCOLOR screenColor = D3DXCOLOR(screenColorCsm.X, screenColorCsm.Y, screenColorCsm.Z, screenColorCsm.W);
	V(g_effect->SetValue("screenColor", &screenColor, sizeof(D3DXCOLOR)));

	// ブレンドタイプを渡す
	V(g_effect->SetInt("colorBlendType", drawableSetting->GetColorBlendType()));
	V(g_effect->SetInt("alphaBlendType", drawableSetting->GetAlphaBlendType()));

	// usedSelfBuffer が真なら現在のRTをテクスチャコピーして渡す
	if (drawableSetting->GetUsedSelfBuffer())
	{
		CopyCurrentRTToTexture(g_dev);
		V(g_effect->SetTexture("OutputBuffer", s_rtCopyTex));
		V(g_effect->SetBool("useOutBuffer", true));
	}
	else
	{
		V(g_effect->SetTexture("OutputBuffer", NULL));
		V(g_effect->SetBool("useOutBuffer", false));
	}
	V(g_effect->SetBool("isPremultipliedAlpha", IsPremultipliedAlpha()));

	UINT32 passes; V(g_effect->Begin(&passes, 0)); V(g_effect->BeginPass(0));
	drawableSetting->DrawMesh(g_dev);
	V(g_effect->EndPass()); V(g_effect->End());
}
 
void CubismRendererDx9::UpdateVertexs()
 {
	 L2DAPPVertex* vertexBuffer;
	 if (FAILED(_vertex->Lock(0, 0, (void**)&vertexBuffer, D3DLOCK_DISCARD)))
	 {
		 return;
	 }

	 int vpos = 0;
	 for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); ++i)
	 {
		 for (int j = 0; j < GetModel()->GetDrawableVertexCount(i); ++j)
		 {
			 vertexBuffer[vpos].x = GetModel()->GetDrawableVertexPositions(i)[j].X;
			 vertexBuffer[vpos].y = GetModel()->GetDrawableVertexPositions(i)[j].Y;
			 ++vpos;
		 }
	 }
	 _vertex->Unlock();
 }

void CubismRendererDx9::MakeMaskForOffscreen(int tindex)
{
	LPDIRECT3DSURFACE9	preRenderSurface;
	V(g_dev->GetRenderTarget(0, &preRenderSurface));
	V(g_effect->SetTexture("Mask", NULL));
	V(g_dev->SetRenderTarget(0, g_maskSurface));

	V(g_dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0));

	V(g_dev->BeginScene());
	V(g_effect->SetTechnique("RenderMask"));

	UINT32	xxxx;
	for (int i = 0; i < _offscreenSettings[tindex]->GetMaskCount(); i++)
	{
		int targetIndex = _offscreenSettings[tindex]->GetMask(i);
		if (targetIndex == -1)
		{
			continue;
		}
		if (!GetModel()->GetDrawableDynamicFlagVertexPositionsDidChange(targetIndex))
		{
			continue;
		}
		LPDIRECT3DTEXTURE9 tex = _nowTexture[_drawable[targetIndex]->GetTextureIndex()];
		V(g_effect->SetTexture("TexMain", tex));

		V(g_effect->SetBool("isPremultipliedAlpha", IsPremultipliedAlpha()));

		V(g_effect->Begin(&xxxx, 0));
		V(g_effect->BeginPass(0));
		_drawable[targetIndex]->DrawMask(g_dev);
		V(g_effect->EndPass());
		V(g_effect->End());
	}

	V(g_dev->EndScene());

	V(g_dev->SetRenderTarget(0, preRenderSurface));

	preRenderSurface->Release();
}

 void CubismRendererDx9::MakeMaskForDrawable(int tindex)
 {
	 V(g_dev->EndScene());

	 // Clear mask RT
	 LPDIRECT3DSURFACE9	preRenderSurface;
	 V(g_dev->GetRenderTarget(0, &preRenderSurface));
	 V(g_effect->SetTexture("Mask", NULL));
	 V(g_dev->SetRenderTarget(0, g_maskSurface));

	 V(g_dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0));

	 V(g_dev->BeginScene());
	 V(g_effect->SetTechnique("RenderMask"));

	 UINT32	xxxx;
	 for (int i = 0; i < _drawable[tindex]->GetMaskCount(); i++)
	 {
		 int targetIndex = _drawable[tindex]->GetMask(i);
		 if (targetIndex == -1)
		 {
			 continue;
		 }
		 if (!GetModel()->GetDrawableDynamicFlagVertexPositionsDidChange(targetIndex))
		 {
			 continue;
		 }
		 LPDIRECT3DTEXTURE9 tex = _nowTexture[_drawable[targetIndex]->GetTextureIndex()];
		 V(g_effect->SetTexture("TexMain", tex));

		 V(g_effect->SetBool("isPremultipliedAlpha", IsPremultipliedAlpha()));

		 V(g_effect->Begin(&xxxx, 0));
		 V(g_effect->BeginPass(0));
		 _drawable[targetIndex]->DrawMask(g_dev);
		 V(g_effect->EndPass());
		 V(g_effect->End());
	 }

	 V(g_dev->EndScene());

	 V(g_dev->SetRenderTarget(0, preRenderSurface));

	 preRenderSurface->Release();

	 V(g_dev->BeginScene());
 }

DrawableShaderSetting::DrawableShaderSetting()
{
	diffuse = D3DXCOLOR(1, 1, 1, 1);
}

DrawableShaderSetting::~DrawableShaderSetting()
{
	masks.Clear();
}

void DrawableShaderSetting::Initialize(CubismModel* model, int drawindex, int & vertexCountStack, int & indiceCountStack)
{
	drawableIndex = drawindex;
	textureIndex = model->GetDrawableTextureIndices(drawableIndex);
	nonCulling = !model->GetDrawableCulling(drawableIndex);

	vertexCount = model->GetDrawableVertexCount(drawableIndex);
	vertexStart = vertexCountStack;
	vertexCountStack += vertexCount;
	indiceCount = model->GetDrawableVertexIndexCount(drawableIndex);
	indiceStart = indiceCountStack;
	indiceCountStack += indiceCount;

	masks.Resize(model->GetDrawableMaskCounts()[drawableIndex]);
	isInvertMask = model->GetDrawableInvertedMask(drawableIndex);
	for (csmUint32 i = 0; i < masks.GetSize(); ++i)
	{
		masks[i] = model->GetDrawableMasks()[drawableIndex][i];
	}

	csmBlendMode blendMode = model->GetDrawableBlendModeType(drawableIndex);
	colorBlendType = blendMode.GetColorBlendType();
	alphaBlendType = blendMode.GetAlphaBlendType();
	usedSelfBuffer = getUseCopyBuffer(colorBlendType, alphaBlendType);


	// 親パーツを辿ってオフスクリーンIndexを探索
	offscreenIndex = -1;
	csmInt32 currentPart = model->GetDrawableParentPartIndex(drawableIndex);
	const csmInt32* partParentTable = model->GetPartParentPartIndices();
	const csmInt32* partOffscreenTable = model->GetPartOffscreenIndices();
	while (currentPart != CubismModel::CubismNoIndex_Parent && currentPart >= 0)
	{
		if (partOffscreenTable && partOffscreenTable[currentPart] != CubismModel::CubismNoIndex_Offscreen)
		{
			offscreenIndex = partOffscreenTable[currentPart];
			break;
		}
		if (partParentTable)
		{
			currentPart = partParentTable[currentPart];
		}
		else
		{
			break;
		}
	}
}

void DrawableShaderSetting::DrawMesh(LPDIRECT3DDEVICE9 dev)
{
	if (nonCulling)
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	}
	else
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));
	}



	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
	if (usedSelfBuffer) {
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO));
		V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO));
	}else{
		switch (colorBlendType)
		{
		case ::csmColorBlendType_AddCompatible:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE));
			break;
		case ::csmColorBlendType_MultiplyCompatible:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE));
			break;
		default:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));
			break;
		}
	}

	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vertexStart, 0, vertexCount, indiceStart, indiceCount / 3);
}

void DrawableShaderSetting::DrawMask(LPDIRECT3DDEVICE9 dev)
{
	if (nonCulling)
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	}
	else
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));
	}

	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
	V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
	V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
	V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));

	V(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vertexStart, 0, vertexCount, indiceStart, indiceCount / 3));
}

void DrawableShaderSetting::DrawMaskingMesh(LPDIRECT3DDEVICE9 dev)
{
	if (nonCulling)
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	}
	else
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));
	}

	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
	switch (maskingType)
	{
	case DrawingMaskingMode::Skip:
		return;
	case DrawingMaskingMode::EraseMask:
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO));
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
		V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));
		break;
	case DrawingMaskingMode::DrawMask:
	default:
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
		V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));
		break;
	}

	dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vertexStart, 0, vertexCount, indiceStart, indiceCount / 3);
}

int DrawableShaderSetting::GetMaskCount()
{
	return masks.GetSize();
}

int DrawableShaderSetting::GetTextureIndex()
{
	return textureIndex;
}

int DrawableShaderSetting::GetDrawableIndex()
{
	return drawableIndex;
}

int DrawableShaderSetting::GetMask(int i)
{
	return masks[i];
}

D3DXCOLOR DrawableShaderSetting::GetDiffuse()
{
	D3DXCOLOR ret = diffuse;
	diffuse = D3DXCOLOR(1, 1, 1, 1);
	return ret;
}

void DrawableShaderSetting::MixDiffuseColor(float opa, float r, float g, float b, float a)
{
	diffuse = D3DXCOLOR(r, g, b, a) * opa + diffuse * (1 - opa);
}

bool DrawableShaderSetting::HaveElement(CubismIdHandle ele)
{
	for (csmUint32 i = 0; i < userdataElements.GetSize(); ++i)
	{
		if (userdataElements[i] == ele)
		{
			return true;
		}
	}
	return false;
}

bool DrawableShaderSetting::HaveElements(csmVector<CubismIdHandle>& elements)
{
	for (csmUint32 i = 0; i < userdataElements.GetSize(); ++i)
	{
		for (csmUint32 j = 0; j < elements.GetSize(); ++j) {
			if (userdataElements[i] == elements[j])
			{
				return true;
			}
		}
	}
	return false;
}

void DrawableShaderSetting::AddElements(const csmString & userDataValue)
{
	char buffer[128];
	const char* userdataSentence = userDataValue.GetRawString();
	int allpos = 0;
	int bufferPos = 0;
	while (allpos < userDataValue.GetLength())
	{
		if (userdataSentence[allpos] == '\n' || userdataSentence[allpos] == '\0')
		{
			buffer[bufferPos] = '\0';
			userdataElements.PushBack(CubismFramework::GetIdManager()->GetId(buffer));
			bufferPos = 0;
		}
		else
		{
			buffer[bufferPos] = userdataSentence[allpos];
			++bufferPos;
		}
		++allpos;
	}
	buffer[bufferPos] = '\0';
	userdataElements.PushBack(CubismFramework::GetIdManager()->GetId(buffer));
}

void OffscreenShaderSetting::Initialize(CubismModel* model, int offscreenindex)
{
	CubismIdHandle ownerId = model->GetOffscreenOwnerId(offscreenindex);
	csmInt32 ownerPartsIndex = model->GetPartIndex(ownerId);

	// currentPartsIndexは転写先のOffscreenPartsを示す。
	csmInt32 currentPartsIndex = model->GetPartParentPartIndex(ownerPartsIndex);
	while (currentPartsIndex != -1)
	{
		if (model->GetPartOffscreenIndices()[currentPartsIndex] != CubismModel::CubismNoIndex_Offscreen) {
			break;
		}
		currentPartsIndex = model->GetPartParentPartIndex(currentPartsIndex);
	}
	csmInt32 offscreenTargetIndex = -1;
	if (currentPartsIndex != -1) {
		offscreenTargetIndex = model->GetPartOffscreenIndices()[currentPartsIndex];
	}

	masks.Resize(model->GetOffscreenMaskCounts()[offscreenindex]);
	isInvertMask = model->GetOffscreenInvertedMask(offscreenindex);
	for (csmUint32 i = 0; i < masks.GetSize(); ++i)
	{
		masks[i] = model->GetOffscreenMasks()[offscreenindex][i];
	}

	_offscreenIndex = offscreenindex;
	_transferOffscreenIndex = offscreenTargetIndex;


	csmBlendMode blendMode = model->GetOffscreenBlendModeType(offscreenindex);
	colorBlendType = blendMode.GetColorBlendType();
	alphaBlendType = blendMode.GetAlphaBlendType();
	usedSelfBuffer = getUseCopyBuffer(colorBlendType,alphaBlendType);
}

// OffscreenShaderSetting 実装
void OffscreenShaderSetting::SetDrawSetting(LPDIRECT3DDEVICE9 dev)
{
	if (nonCulling) { V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE)); }
	else { V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW)); }

	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
	if (usedSelfBuffer) {
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO));
		V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO));
	}
	else {
		switch (drawtype)
		{
		case Rendering::CubismRenderer::CubismBlendMode::CubismBlendMode_Additive:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE));
			break;
		case Rendering::CubismRenderer::CubismBlendMode::CubismBlendMode_Multiplicative:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE));
			break;
		case Rendering::CubismRenderer::CubismBlendMode::CubismBlendMode_Normal:
		default:
			V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
			V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
			V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));
			break;
		}
	}
}


int OffscreenShaderSetting::GetMaskCount() { return masks.GetSize(); }
int OffscreenShaderSetting::GetMask(int i) { return masks[i]; }
D3DXCOLOR OffscreenShaderSetting::GetDiffuse() { D3DXCOLOR ret = diffuse; diffuse = D3DXCOLOR(1,1,1,1); return ret; }
D3DXCOLOR OffscreenShaderSetting::GetMultipleColor() { return D3DXCOLOR(1,1,1,1); }
D3DXCOLOR OffscreenShaderSetting::GetScreenColor() { return D3DXCOLOR(0,0,0,0); }
void OffscreenShaderSetting::MixDiffuseColor(float opa, float r, float g, float b, float a) { diffuse = D3DXCOLOR(r,g,b,a)*opa + diffuse*(1-opa); }
