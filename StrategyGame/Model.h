#ifndef __MODEL_H_INCLUDED__
#define __MODEL_H_INCLUDED__

#pragma once

#include "object.h"

#include <map>
#include <vector>

#include <Windows.h> //TODO remove windows depends
#include <GL/gl.h>

#include "assimp/scene.h"

#include "vertexBufferObject.h"
#include "texture.h"

#define FOR(q,n) for(int q=0;q<n;q++)
#define SFOR(q,s,e) for(int q=s;q<=e;q++)
#define RFOR(q,n) for(int q=n;q>=0;q--)
#define RSFOR(q,s,e) for(int q=s;q>=e;q--)

#define ESZ(elem) (int)elem.size()


class CMaterial
{
public:
	int iTexture;
};

class Model : public Object {
public:
	Model(std::string sFilePath);
	~Model(void);

	virtual void draw();

	bool LoadModelFromFile(std::string sFilePath);

	static void FinalizeVBO();
	static void BindModelsVAO();
private:
	bool bLoaded;
	static CVertexBufferObject vboModelData;
	static UINT uiVAO;
	static std::vector<CTexture> tTextures;

	std::vector<int> iMeshStartIndices;
	std::vector<int> iMeshSizes;
	std::vector<int> iMaterialIndices;
	int iNumMaterials;
};

#endif

