#pragma once
#include "ipc_common.h"
#include "../src\LAppTextureDesc.h"
#include "../framework\L2DModelMatrix.h"
#include "../OWFramework\src\CubismFramework.hpp"
#include "../OWFramework\src\Model\CubismModel.hpp"
#include "../OWFramework\src\Rendering\CubismRenderer.hpp"
#include "../OWFramework\src\Id\CubismIdManager.hpp"
#include "../OWFramework\src\Model\CubismModelUserData.hpp"


using namespace Live2DC3::Cubism::Framework;
using namespace Live2DC3::Cubism::Core;
using namespace Live2DC3::Cubism::Framework::Rendering;
using live2d::framework::L2DModelMatrix;

/*

	�o�b�t�@���
	Drawable�o�^


�e�N�X�`���o�^
�e�N�X�`���X�V

�}�X�N�n��̊֐�
�V�F�[�_�[�A�f�o�C�X���

�f�o�C�X���X�g
*/

#define V(hr) if(((HRESULT)(hr)) < 0){live2d::UtDebug::error("%s(%d) DirectX Error[ %s ]\n",__FILE__,__LINE__,#hr);}
//#define V(hr) hr

struct L2DAPPVertex
{
	float x, y, z;
	float u, v;
};

typedef struct RenderOrderSorter
{
	int DrawableIndex;
	int RenderOrderIndex;
}RenderOrderSorter;

static int CompareSortableDrawables(const void *a, const void *b)
{
	const RenderOrderSorter* drawableA = (const RenderOrderSorter*)a;
	const RenderOrderSorter* drawableB = (const RenderOrderSorter*)b;


	return (drawableA->RenderOrderIndex > drawableB->RenderOrderIndex) - (drawableA->RenderOrderIndex < drawableB->RenderOrderIndex);
}

enum DrawingMaskingMode
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

	void SetDrawingMaskingType(DrawingMaskingMode set) {
		maskingType = set;
	}

	bool GetIsInvertMask() const
	{
		return isInvertMask;
	}

private:
	
	int vertexCount;
	int vertexStart;
	int indiceCount;
	int indiceStart;

	int drawableIndex;
	int textureIndex;

	csmVector<int> masks;
	bool isInvertMask;

	Rendering::CubismRenderer::CubismBlendMode drawtype;
	DrawingMaskingMode maskingType;
	bool nonCulling;

	D3DXCOLOR diffuse;

	csmVector<CubismIdHandle> userdataElements;
};

class CubismRendererDx9 : public CubismRenderer
{
public:
	static CubismRendererDx9* Create();

	CubismRendererDx9();
	~CubismRendererDx9();
	/**
	* @brief   �����_���̏��������������s����<br>
	*           �����ɓn�������f�����烌���_���̏����������ɕK�v�ȏ������o�����Ƃ��ł���
	*
	* @param[in]  model -> ���f���̃C���X�^���X
	*/
	void Initialize(Csm::CubismModel* model, L2DModelMatrix* mat, CubismModelUserData* userdata);


	/**
	* @brief   ���f����`�悷��
	*
	*/
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
	/**
	* @brief   ���f���`��̎���
	*
	*/
	void DoDrawModel();

	void UpdateVertexs();

	void MakeMask(int tindex);

	/**
	* @brief   �`��I�u�W�F�N�g�i�A�[�g���b�V���j��`�悷��B<br>
	*           �|���S�����b�V���ƃe�N�X�`���ԍ����Z�b�g�œn���B
	*
	* @param[in]   textureNo            ->  �`�悷��e�N�X�`���ԍ�
	* @param[in]   indexCount           ->  �`��I�u�W�F�N�g�̃C���f�b�N�X�l
	* @param[in]   vertexCount          ->  �|���S�����b�V���̒��_��
	* @param[in]   indexArray           ->  �|���S�����b�V�����_�̃C���f�b�N�X�z��
	* @param[in]   vertexArray          ->  �|���S�����b�V���̒��_�z��
	* @param[in]   uvArray              ->  uv�z��
	* @param[in]   opacity              ->  �s�����x
	* @param[in]   colorBlendMode       ->  �J���[�u�����f�B���O�̃^�C�v
	*
	*/
	virtual void DrawMesh(csmInt32 textureNo, csmInt32 indexCount, csmInt32 vertexCount
		, csmUint16* indexArray, csmFloat32* vertexArray, csmFloat32* uvArray
		, csmFloat32 opacity, CubismBlendMode colorBlendMode, csmBool invertedMask)
	{

	}

	/**
	* @brief   ���f���`�撼�O�̃����_���̃X�e�[�g��ێ�����
	*/
	void SaveProfile()
	{
	}


	/**
	* @brief   ���f���`�撼�O�̃����_���̃X�e�[�g�𕜋A������
	*/
	void RestoreProfile()
	{
	}

	Csm::csmVector<LAppTextureDesc*> _textures;
	Csm::csmVector<DrawableShaderSetting*> _drawable;

	csmVector<LPDIRECT3DTEXTURE9> _nowTexture;

	LPDIRECT3DVERTEXBUFFER9 _vertex;
	LPDIRECT3DINDEXBUFFER9  _indice;

	L2DModelMatrix* _mtrx;
	
	static	LPDIRECT3DDEVICE9 g_dev;
	static	LPDIRECT3DTEXTURE9 g_maskTexture;
	static	LPDIRECT3DSURFACE9 g_maskSurface;
	static  LPD3DXEFFECT		g_effect;
};

