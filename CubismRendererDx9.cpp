#include "CubismRendererDx9.h"



LPDIRECT3DDEVICE9	CubismRendererDx9::g_dev = NULL;
LPDIRECT3DTEXTURE9	CubismRendererDx9::g_maskTexture = NULL;
LPDIRECT3DSURFACE9	CubismRendererDx9::g_maskSurface = NULL;
LPD3DXEFFECT		CubismRendererDx9::g_effect = NULL;

CubismRendererDx9 * CubismRendererDx9::Create()
{
	return new CubismRendererDx9();
}

CubismRendererDx9::CubismRendererDx9()
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
}

/**
* @brief   レンダラの初期化処理を実行する<br>
*           引数に渡したモデルからレンダラの初期化処理に必要な情報を取り出すことができる
*
* @param[in]  model -> モデルのインスタンス
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

	//バッファ作成
	V(g_dev->CreateVertexBuffer(sizeof(L2DAPPVertex) * vertexTotalSize,
		0,
		D3DFVF_XYZ | D3DFVF_TEX1,
		D3DPOOL_MANAGED,
		&_vertex,
		NULL));
	V(g_dev->CreateIndexBuffer(sizeof(WORD) * indexTotalSize,
		0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &_indice, NULL));

	//ロック取得、失敗の場合はLoad失敗
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

	//各Drawableの読み込み処理
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
}

/**
* @brief   モデルを描画する
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
	// マスキング設定のRenderingSettingへの反映

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

		DrawingMaskingMode set = DrawingMaskingMode::DrawMask;

		if (!isFit) {
			if ( (mode & MASKING_MODE_NONFIT_SKIP) != 0 ) {
				set = DrawingMaskingMode::Skip;
			}
			else {
				set = DrawingMaskingMode::EraseMask;
			}
		}
		_drawable[i]->SetDrawingMaskingType(set);
	}

	// DXプロファイル保存
	SaveProfile();
	

	// 描画系処理　↓
	// マトリクス調整
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


	//vertecx update
	UpdateVertexs();
	
	// テクスチャ関連付け
	for (csmUint32 i = 0; i < _textures.GetSize(); ++i)
	{
		_nowTexture[i] = _textures[i]->getNowTex();
	}

	// 頂点転送
	V(g_dev->SetIndices(_indice));
	V(g_dev->SetStreamSource(0, _vertex, 0, sizeof(L2DAPPVertex)));

	V(g_dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1));

	// 描画順序ソート
	csmVector<DrawableShaderSetting*> sort(GetModel()->GetDrawableCount());

	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); ++i)
	{
		sort[GetModel()->GetDrawableRenderOrders()[i]] = _drawable[i];
	}

	// Turn off D3D lighting
	V(g_dev->SetRenderState(D3DRS_LIGHTING, FALSE));

	// Drawable描画
	int xxx = 0;
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
			MakeMask(sort[i]->GetDrawableIndex());

			V(g_effect->SetTexture("Mask", g_maskTexture));

			V(g_effect->SetTechnique("RenderMaskingMasked"));
			V(g_effect->SetBool("isInvertMask", sort[i]->GetIsInvertMask()));
		}
		else
		{
			V(g_effect->SetTechnique("RenderMaskingNoMask"));
		}


		V(g_effect->SetFloat("drawableOpacity", opa));
		D3DXCOLOR diffuse = D3DXCOLOR(1, 1, 1, 1);
		V(g_effect->SetValue("drawableDiffuse", &diffuse, sizeof(D3DXCOLOR)));

		int texnum = sort[i]->GetTextureIndex();
		LPDIRECT3DTEXTURE9 tex = _nowTexture[texnum];
		V(g_effect->SetTexture("TexMain", tex));

		UINT32	xxxx;
		V(g_effect->Begin(&xxxx, 0));
		V(g_effect->BeginPass(0));
		sort[i]->DrawMaskingMesh(g_dev);
		V(g_effect->EndPass());
		V(g_effect->End());
	}

	// DXプロファイル復元
	RestoreProfile();
}

bool CubismRendererDx9::AddTexture(const char * filepath)
{
	LPDIRECT3DTEXTURE9 newtex;
	// テクスチャ画像をDirextXでの表示用に変換
	if (FAILED(D3DXCreateTextureFromFileExA(g_dev
		, filepath
		, 0	//width 
		, 0	//height
		, 0	//mipmap //( 0なら完全なミップマップチェーン）
		, 0	//Usage
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
	//resize
	if (!target->isSameSize(nWidth, nHeight))
	{
		LPDIRECT3DTEXTURE9 ntex;
		//if (FAILED(g_pD3DDevice->CreateTexture(nWidth, nHeight, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &ntex,NULL)))
		if (FAILED(g_dev->CreateTexture(nWidth, nHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ntex, NULL)))
		{
			return;
		}

		//setTex on precheck
		//((Live2DModelD3D*)live2DModel)->setTexture(texNo, ntex);

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

/**
* @brief   モデル描画の実装
*
*/

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
	normalizeMat._41 += 0.5;
	normalizeMat._42 -= 0.5;

	D3DXMatrixMultiply(&mmvp, &mmvp, &normalizeMat);
	D3DXMatrixMultiply(&mmvp, &mmvp, &modelMat);
	D3DXMatrixMultiply(&mmvp, &mmvp, &view);
	D3DXMatrixMultiply(&mmvp, &mmvp, &projection);

	V(g_effect->SetMatrix("g_mWorldViewProjection", &mmvp));


	//vertecx update
	UpdateVertexs();

	//draw(maskmake)

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
		sort[GetModel()->GetDrawableRenderOrders()[i]] = _drawable[i];
	}

	// Turn off D3D lighting
	V(g_dev->SetRenderState(D3DRS_LIGHTING, FALSE));

	int xxx = 0;
	for (csmInt32 i = 0; i < GetModel()->GetDrawableCount(); i++)
	{
		if (!GetModel()->GetDrawableDynamicFlagIsVisible(sort[i]->GetDrawableIndex()))
		{
			sort[i]->GetDiffuse();
			continue;
		}
		//const char* ID = GetModel()->GetDrawableId(sort[i]->GetDrawableIndex())->GetString().GetRawString();
		float opa = GetModel()->GetDrawableOpacity(sort[i]->GetDrawableIndex());
		if (sort[i]->GetMaskCount() > 0)
		{
			MakeMask(sort[i]->GetDrawableIndex());

			V(g_effect->SetTexture("Mask", g_maskTexture));

			V(g_effect->SetTechnique("RenderSceneMasked"));
			V(g_effect->SetBool("isInvertMask", sort[i]->GetIsInvertMask()));
		}
		else
		{
			V(g_effect->SetTechnique("RenderSceneNomask"));
		}


		V(g_effect->SetFloat("drawableOpacity", opa));
		D3DXCOLOR diffuse = sort[i]->GetDiffuse();
		V(g_effect->SetValue("drawableDiffuse", &diffuse, sizeof(D3DXCOLOR)));

		int texnum = sort[i]->GetTextureIndex();
		LPDIRECT3DTEXTURE9 tex = _nowTexture[texnum];
		V(g_effect->SetTexture("TexMain", tex));

		UINT32	xxxx;
		V(g_effect->Begin(&xxxx, 0));
		V(g_effect->BeginPass(0));
		sort[i]->DrawMesh(g_dev);
		V(g_effect->EndPass());
		V(g_effect->End());
	}
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

 void CubismRendererDx9::MakeMask(int tindex)
 {
	 //V(g_effect->EndPass());
	 //V(g_effect->End());
	 V(g_dev->EndScene());

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

		 V(g_effect->Begin(&xxxx, 0));
		 V(g_effect->BeginPass(0));
		 _drawable[targetIndex]->DrawMask(g_dev);
		 V(g_effect->EndPass());
		 V(g_effect->End());
	 }

	 V(g_dev->EndScene());

	 V(g_dev->SetRenderTarget(0, preRenderSurface));

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
	drawtype = model->GetDrawableBlendMode(drawableIndex);
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
}

void DrawableShaderSetting::DrawMesh(LPDIRECT3DDEVICE9 dev)
{
	if (nonCulling)
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	}
	else
	{
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));//D3DCULL_CCW
	}

	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
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
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));//D3DCULL_CCW
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));//D3DCULL_CCW
		V(dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA));
		break;
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
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));//D3DCULL_CCW
	}

	V(dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
	V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));//D3DCULL_CCW
	V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
	V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));//D3DCULL_CCW
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
		V(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW));//D3DCULL_CCW
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
		V(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE));//D3DCULL_CCW
		V(dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE));
		V(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));//D3DCULL_CCW
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

CubismRenderer* CubismRenderer::Create()
{
	return CubismRendererDx9::Create();
}
