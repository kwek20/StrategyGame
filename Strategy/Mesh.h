#ifndef __MESH_H_INCLUDED__
#define __MESH_H_INCLUDED__

#pragma once

#include <allegro5\allegro.h>
#include "allegro5\allegro_opengl.h"

#include "assimp\scene.h"
#include "assimp\mesh.h"
#include "assimp\Importer.hpp"

#include <vector>
#include <map>

#define aisgl_min(x,y) (x<y?x:y)
#define aisgl_max(x,y) (y>x?y:x)

#define MatricesUniBufferSize sizeof(float) * 16 * 3
#define ProjMatrixOffset 0
#define ViewMatrixOffset sizeof(float) * 16
#define ModelMatrixOffset sizeof(float) * 16 * 2
#define MatrixSize sizeof(float) * 16

#define M_PI       3.14159265358979323846f

static inline float
DegToRad(float degrees){
	return (float)(degrees * (M_PI / 180.0f));
};

/**
 * Mesh code from http://www.nexcius.net/2014/04/13/loading-meshes-using-assimp-in-opengl/
 */
class Mesh
{
public:
	// Information to render each assimp node
	struct MyMesh{

		GLuint vao;
		GLuint texIndex;
		GLuint uniformBlockIndex;
		int numFaces;
	};

	// This is for a shader uniform block
	struct MyMaterial{

		float diffuse[4];
		float ambient[4];
		float specular[4];
		float emissive[4];
		float shininess;
		int texCount;
	};

	void recursive_render(const aiScene *sc, const aiNode* nd);
	void genVAOsAndUniformBuffer(const aiScene *sc);
	int LoadGLTextures(const aiScene* scene);
private:
	void set_float4(float f[4], float a, float b, float c, float d);
	void color4_to_float4(const aiColor4D *c, float f[4]);
	bool Import3DFromFile(const std::string& pFile);

	void get_bounding_box(aiVector3D* min, aiVector3D* max);
	void get_bounding_box_for_node(const aiNode* nd, aiVector3D* min, aiVector3D* max);

	void setModelMatrix();
	void translate(float x, float y, float z);
	void rotate(float angle, float x, float y, float z);
	void scale(float x, float y, float z);
	void buildProjectionMatrix(float fov, float ratio, float nearp, float farp);

	void pushMatrix();
	void popMatrix();
	void setIdentityMatrix(float *mat, int size);
	void multMatrix(float *a, float *b);

	void setTranslationMatrix(float *mat, float x, float y, float z);
	void setScaleMatrix(float *mat, float sx, float sy, float sz);
	void setRotationMatrix(float *mat, float angle, float x, float y, float z);

	std::string getBasePath(const std::string& path);
	std::string path;

	// Model Matrix (part of the OpenGL Model View Matrix)
	float modelMatrix[16];

	// For push and pop matrix
	std::vector<float *> matrixStack;

	// Vertex Attribute Locations
	GLuint vertexLoc = 0, normalLoc = 1, texCoordLoc = 2;

	// Uniform Bindings Points
	GLuint matricesUniLoc = 1, materialUniLoc = 2;

	// The sampler uniform for textured models
	// we are assuming a single texture so this will
	//always be texture unit 0
	GLuint texUnit = 0;

	// Uniform Buffer for Matrices
	// this buffer will contain 3 matrices: projection, view and model
	// each matrix is a float array with 16 components
	GLuint matricesUniBuffer;

	// Create an instance of the Importer class
	Assimp::Importer importer;

	// the global Assimp scene object
	const aiScene* scene = NULL;

	// scale factor for the model to fit in the window
	float scaleFactor;


	// images / texture
	// map image filenames to textureIds
	// pointer to texture Array
	std::map<std::string, GLuint> textureIdMap;

	std::vector<struct MyMesh> myMeshes;
public:
	Mesh(const char *filename);
	~Mesh(void);

	void render();
};
#endif