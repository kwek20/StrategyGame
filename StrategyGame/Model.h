#ifndef __MODEL_H_INCLUDED__
#define __MODEL_H_INCLUDED__

#pragma once

#include "object.h"

#include <map>

#include <Windows.h> //TODO remove windows depends
#include <GL/gl.h>

#include "assimp/scene.h"

class Model :
	public Object
{
public:
	Model(const aiScene* scene);
	~Model(void);

	virtual void draw();

	// images / texture
	std::map<std::string, GLuint*> textureIdMap;	// map image filenames to textureIds
	GLuint*		textureIds;							// pointer to texture Array
private:
	const aiScene* model;

	void Color4f(const aiColor4D *color);
	void set_float4(float f[4], float a, float b, float c, float d);
	void color4_to_float4(const aiColor4D *c, float f[4]);

	void apply_material(const aiMaterial *mtl);
	void recursive_render (const struct aiScene *scene, const struct aiNode* nd, float scale);
};

#endif

