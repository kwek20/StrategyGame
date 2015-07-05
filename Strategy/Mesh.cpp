#include "Mesh.h"

#include "assimp\Importer.hpp"
#include "assimp\postprocess.h"

#include <iostream>

/**
*	Mesh constructor, loads the specified filename if supported by Assimp
**/
Mesh::Mesh(const char *filename){
	path = filename; 
	if (!Import3DFromFile(filename)){
		return;
	}

	LoadGLTextures(scene);
	genVAOsAndUniformBuffer(scene);
}

void Mesh::recursive_render(const aiScene *sc, const aiNode* nd){
	// Get node transformation matrix
	aiMatrix4x4 m = nd->mTransformation;
	// OpenGL matrices are column major
	m.Transpose();

	// save model matrix and apply node transformation
	//pushMatrix();

	float aux[16];
	memcpy(aux, &m, sizeof(float) * 16);
	multMatrix(modelMatrix, aux);
	//setModelMatrix();

	// draw all meshes assigned to this node
	for (unsigned int n = 0; n < nd->mNumMeshes; ++n){
		/*glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0, 600, 0.0, 400, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();


		glLoadIdentity();
		glDisable(GL_LIGHTING);


		glColor3f(1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, myMeshes[nd->mMeshes[n]].texIndex);


		// Draw a textured quad
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex3f(0, 0, 0);
		glTexCoord2f(0, 1); glVertex3f(0, 100, 0);
		glTexCoord2f(1, 1); glVertex3f(100, 100, 0);
		glTexCoord2f(1, 0); glVertex3f(100, 0, 0);
		glEnd();


		glDisable(GL_TEXTURE_2D);
		glPopMatrix();


		glMatrixMode(GL_PROJECTION);
		glPopMatrix();

		glMatrixMode(GL_MODELVIEW);*/

		// bind material uniform
		//glBindBufferRange(GL_UNIFORM_BUFFER, materialUniLoc, myMeshes[nd->mMeshes[n]].uniformBlockIndex, 0, sizeof(struct MyMaterial));
		// bind texture

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, myMeshes[nd->mMeshes[n]].texIndex);

		// bind VAO
		glBindVertexArray(myMeshes[nd->mMeshes[n]].vao);

		// draw
		glDrawElements(GL_TRIANGLES, myMeshes[nd->mMeshes[n]].numFaces * 3, GL_UNSIGNED_INT, 0);
	}

	// draw all children
	for (unsigned int n = 0; n < nd->mNumChildren; ++n){
		recursive_render(sc, nd->mChildren[n]);
	}
	//popMatrix();
}

int Mesh::LoadGLTextures(const aiScene* scene){
	if (scene->HasTextures()) return -1;
	/* getTexture Filenames and Numb of Textures */
	for (unsigned int m = 0; m<scene->mNumMaterials; m++){
		int texIndex = 0;

		aiReturn texFound;
		aiString path;	// filename

		while ((texFound = scene->mMaterials[m]->GetTexture(aiTextureType_DIFFUSE, texIndex, &path)) == AI_SUCCESS){
			textureIdMap[path.data] = NULL; //fill map with textures, pointers still NULL yet
			texIndex++;
		}
	}

	int numTextures = textureIdMap.size();
	/* create and fill array with GL texture ids */
	GLuint* textureIds = new GLuint[numTextures];

	/* get iterator */
	std::map<std::string, GLuint>::iterator itr = textureIdMap.begin();

	std::string basepath = getBasePath(path);

	ALLEGRO_BITMAP *image;
	for (int i = 0; i<numTextures; i++){
		std::string filename = (*itr).first;  // get filename
		(*itr).second = textureIds[i];	  // save texture id for filename in map
		itr++;								  // next texture

		std::string fileloc = basepath + filename;	/* Loading of image */
		image = al_load_bitmap(fileloc.c_str());

		if (image) /* If no error occured: */{
			GLuint texId = al_get_opengl_texture(image);

			//glGenTextures(numTextures, &textureIds[i]); /* Texture name generation */
			glBindTexture(GL_TEXTURE_2D, texId); /* Binding of texture name */
			//redefine standard texture values
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); /* We will use linear
																			  interpolation for magnification filter */
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); /* We will use linear
																			  interpolation for minifying filter */
			textureIdMap[filename] = texId;
		} else {
			/* Error occured */
			std::cout << "Couldn't load Image: " << fileloc.c_str() << "\n";
		}
	}

	//Cleanup
	delete[] textureIds;

	//return success
	return true;
}

std::string Mesh::getBasePath(const std::string& path){
	size_t pos = path.find_last_of("\\/");
	return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}

void Mesh::genVAOsAndUniformBuffer(const aiScene *sc) {
	struct MyMesh aMesh;
	struct MyMaterial aMat;
	GLuint buffer;

	// For each mesh
	for (unsigned int n = 0; n < sc->mNumMeshes; ++n){
		const aiMesh* mesh = sc->mMeshes[n];

		// create array with faces
		// have to convert from Assimp format to array
		unsigned int *faceArray;
		faceArray = (unsigned int *)malloc(sizeof(unsigned int) * mesh->mNumFaces * 3);
		unsigned int faceIndex = 0;

		for (unsigned int t = 0; t < mesh->mNumFaces; ++t) {
			const aiFace* face = &mesh->mFaces[t];

			memcpy(&faceArray[faceIndex], face->mIndices, 3 * sizeof(unsigned int));
			faceIndex += 3;
		}
		aMesh.numFaces = sc->mMeshes[n]->mNumFaces;

		// generate Vertex Array for mesh
		glGenVertexArrays(1, &(aMesh.vao));
		glBindVertexArray(aMesh.vao);

		// buffer for faces
		glGenBuffers(1, &buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * mesh->mNumFaces * 3, faceArray, GL_STATIC_DRAW);

		// buffer for vertex positions
		if (mesh->HasPositions()) {
			glGenBuffers(1, &buffer);
			glBindBuffer(GL_ARRAY_BUFFER, buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * mesh->mNumVertices, mesh->mVertices, GL_STATIC_DRAW);
			glEnableVertexAttribArray(vertexLoc);
			glVertexAttribPointer(vertexLoc, 3, GL_FLOAT, 0, 0, 0);
		}

		// buffer for vertex normals
		if (mesh->HasNormals()) {
			glGenBuffers(1, &buffer);
			glBindBuffer(GL_ARRAY_BUFFER, buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * mesh->mNumVertices, mesh->mNormals, GL_STATIC_DRAW);
			glEnableVertexAttribArray(normalLoc);
			glVertexAttribPointer(normalLoc, 3, GL_FLOAT, 0, 0, 0);
		}

		// buffer for vertex texture coordinates
		if (mesh->HasTextureCoords(0)) {
			float *texCoords = (float *)malloc(sizeof(float) * 2 * mesh->mNumVertices);
			for (unsigned int k = 0; k < mesh->mNumVertices; ++k) {
				texCoords[k * 2] = mesh->mTextureCoords[0][k].x;
				texCoords[k * 2 + 1] = mesh->mTextureCoords[0][k].y;
			}

			glGenBuffers(1, &buffer);
			glEnableVertexAttribArray(texCoordLoc);
			glBindBuffer(GL_ARRAY_BUFFER, buffer);

			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * mesh->mNumVertices, texCoords, GL_STATIC_DRAW);
			glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
		}

		// unbind buffers
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		// create material uniform buffer
		aiMaterial *mtl = sc->mMaterials[mesh->mMaterialIndex];

		aiString texPath;	//contains filename of texture
		if (AI_SUCCESS == mtl->GetTexture(aiTextureType_DIFFUSE, 0, &texPath)){
			//bind texture
			unsigned int texId = textureIdMap[texPath.data];
			aMesh.texIndex = texId;
			aMat.texCount = 1;
		} else {
			aMat.texCount = 0;
		}

		float c[4];
		set_float4(c, 0.8f, 0.8f, 0.8f, 1.0f);
		aiColor4D diffuse;
		if (AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE, &diffuse))
			color4_to_float4(&diffuse, c);
		memcpy(aMat.diffuse, c, sizeof(c));

		set_float4(c, 0.2f, 0.2f, 0.2f, 1.0f);
		aiColor4D ambient;
		if (AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_AMBIENT, &ambient))
			color4_to_float4(&ambient, c);
		memcpy(aMat.ambient, c, sizeof(c));

		set_float4(c, 0.0f, 0.0f, 0.0f, 1.0f);
		aiColor4D specular;
		if (AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_SPECULAR, &specular))
			color4_to_float4(&specular, c);
		memcpy(aMat.specular, c, sizeof(c));

		set_float4(c, 0.0f, 0.0f, 0.0f, 1.0f);
		aiColor4D emission;
		if (AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_EMISSIVE, &emission))
			color4_to_float4(&emission, c);
		memcpy(aMat.emissive, c, sizeof(c));

		float shininess = 0.0;
		unsigned int max;
		aiGetMaterialFloatArray(mtl, AI_MATKEY_SHININESS, &shininess, &max);
		aMat.shininess = shininess;

		glGenBuffers(1, &(aMesh.uniformBlockIndex));
		glBindBuffer(GL_UNIFORM_BUFFER, aMesh.uniformBlockIndex);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(aMat), (void *)(&aMat), GL_STATIC_DRAW);

		myMeshes.push_back(aMesh);
	}
}

void Mesh::set_float4(float f[4], float a, float b, float c, float d){
	f[0] = a;
	f[1] = b;
	f[2] = c;
	f[3] = d;
}

void Mesh::color4_to_float4(const aiColor4D *c, float f[4]){
	f[0] = c->r;
	f[1] = c->g;
	f[2] = c->b;
	f[3] = c->a;
}

bool Mesh::Import3DFromFile(const std::string& pFile){
	scene = importer.ReadFile(pFile, aiProcessPreset_TargetRealtime_Quality);

	// If the import failed, report it
	if (!scene){
		printf("%s\n", importer.GetErrorString());
		return false;
	}

	// Now we can access the file's contents.
	aiVector3D scene_min, scene_max, scene_center;
	get_bounding_box(&scene_min, &scene_max);
	float tmp;
	tmp = scene_max.x - scene_min.x;
	tmp = scene_max.y - scene_min.y > tmp ? scene_max.y - scene_min.y : tmp;
	tmp = scene_max.z - scene_min.z > tmp ? scene_max.z - scene_min.z : tmp;
	scaleFactor = 1.f / tmp;

	// We're done. Everything will be cleaned up by the importer destructor
	return true;
}

void Mesh::get_bounding_box_for_node(const aiNode* nd,
	aiVector3D* min,
	aiVector3D* max){
	aiMatrix4x4 prev;
	unsigned int n = 0, t;

	for (; n < nd->mNumMeshes; ++n) {
		const aiMesh* mesh = scene->mMeshes[nd->mMeshes[n]];
		for (t = 0; t < mesh->mNumVertices; ++t) {

			aiVector3D tmp = mesh->mVertices[t];

			min->x = aisgl_min(min->x, tmp.x);
			min->y = aisgl_min(min->y, tmp.y);
			min->z = aisgl_min(min->z, tmp.z);

			max->x = aisgl_max(max->x, tmp.x);
			max->y = aisgl_max(max->y, tmp.y);
			max->z = aisgl_max(max->z, tmp.z);
		}
	}

	for (n = 0; n < nd->mNumChildren; ++n) {
		get_bounding_box_for_node(nd->mChildren[n], min, max);
	}
}


void Mesh::get_bounding_box(aiVector3D* min, aiVector3D* max){

	min->x = min->y = min->z = 1e10f;
	max->x = max->y = max->z = -1e10f;
	get_bounding_box_for_node(scene->mRootNode, min, max);
}

/**
*	Clears all loaded MeshEntries
**/
Mesh::~Mesh(void){
	// clear myMeshes stuff
	textureIdMap.clear();
	for (unsigned int i = 0; i < myMeshes.size(); ++i) {
		glDeleteVertexArrays(1, &(myMeshes[i].vao));
		glDeleteTextures(1, &(myMeshes[i].texIndex));
		glDeleteBuffers(1, &(myMeshes[i].uniformBlockIndex));
	}
	// delete buffers
	glDeleteBuffers(1, &matricesUniBuffer);
}

/**
*	Renders all loaded MeshEntries
**/
void Mesh::render() {
	recursive_render(scene, scene->mRootNode);
}

//////////////////////
//MATRIX STUFF AFTER//
//////////////////////

void Mesh::pushMatrix() {

	float *aux = (float *)malloc(sizeof(float) * 16);
	memcpy(aux, modelMatrix, sizeof(float) * 16);
	matrixStack.push_back(aux);
}

void Mesh::popMatrix() {

	float *m = matrixStack[matrixStack.size() - 1];
	memcpy(modelMatrix, m, sizeof(float) * 16);
	matrixStack.pop_back();
	free(m);
}

// sets the square matrix mat to the identity matrix,
// size refers to the number of rows (or columns)
void Mesh::setIdentityMatrix(float *mat, int size) {

	// fill matrix with 0s
	for (int i = 0; i < size * size; ++i)
		mat[i] = 0.0f;

	// fill diagonal with 1s
	for (int i = 0; i < size; ++i)
		mat[i + i * size] = 1.0f;
}


//
// a = a * b;
//
void Mesh::multMatrix(float *a, float *b) {

	float res[16];

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			res[j * 4 + i] = 0.0f;
			for (int k = 0; k < 4; ++k) {
				res[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
			}
		}
	}
	memcpy(a, res, 16 * sizeof(float));

}


// Defines a transformation matrix mat with a translation
void Mesh::setTranslationMatrix(float *mat, float x, float y, float z) {

	setIdentityMatrix(mat, 4);
	mat[12] = x;
	mat[13] = y;
	mat[14] = z;
}

// Defines a transformation matrix mat with a scale
void Mesh::setScaleMatrix(float *mat, float sx, float sy, float sz) {

	setIdentityMatrix(mat, 4);
	mat[0] = sx;
	mat[5] = sy;
	mat[10] = sz;
}

// Defines a transformation matrix mat with a rotation 
// angle alpha and a rotation axis (x,y,z)
void Mesh::setRotationMatrix(float *mat, float angle, float x, float y, float z) {

	float radAngle = DegToRad(angle);
	float co = cos(radAngle);
	float si = sin(radAngle);
	float x2 = x*x;
	float y2 = y*y;
	float z2 = z*z;

	mat[0] = x2 + (y2 + z2) * co;
	mat[4] = x * y * (1 - co) - z * si;
	mat[8] = x * z * (1 - co) + y * si;
	mat[12] = 0.0f;

	mat[1] = x * y * (1 - co) + z * si;
	mat[5] = y2 + (x2 + z2) * co;
	mat[9] = y * z * (1 - co) - x * si;
	mat[13] = 0.0f;

	mat[2] = x * z * (1 - co) - y * si;
	mat[6] = y * z * (1 - co) + x * si;
	mat[10] = z2 + (x2 + y2) * co;
	mat[14] = 0.0f;

	mat[3] = 0.0f;
	mat[7] = 0.0f;
	mat[11] = 0.0f;
	mat[15] = 1.0f;

}

// ----------------------------------------------------
// Model Matrix 
//
// Copies the modelMatrix to the uniform buffer


void Mesh::setModelMatrix() {

	glBindBuffer(GL_UNIFORM_BUFFER, matricesUniBuffer);
	glBufferSubData(GL_UNIFORM_BUFFER,
		ModelMatrixOffset, MatrixSize, modelMatrix);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

}

// The equivalent to glTranslate applied to the model matrix
void Mesh::translate(float x, float y, float z) {

	float aux[16];

	setTranslationMatrix(aux, x, y, z);
	multMatrix(modelMatrix, aux);
	setModelMatrix();
}

// The equivalent to glRotate applied to the model matrix
void Mesh::rotate(float angle, float x, float y, float z) {

	float aux[16];

	setRotationMatrix(aux, angle, x, y, z);
	multMatrix(modelMatrix, aux);
	setModelMatrix();
}

// The equivalent to glScale applied to the model matrix
void Mesh::scale(float x, float y, float z) {

	float aux[16];

	setScaleMatrix(aux, x, y, z);
	multMatrix(modelMatrix, aux);
	setModelMatrix();
}

// ----------------------------------------------------
// Projection Matrix 
//
// Computes the projection Matrix and stores it in the uniform buffer

void Mesh::buildProjectionMatrix(float fov, float ratio, float nearp, float farp) {

	float projMatrix[16];

	float f = 1.0f / tan(fov * (M_PI / 360.0f));

	setIdentityMatrix(projMatrix, 4);

	projMatrix[0] = f / ratio;
	projMatrix[1 * 4 + 1] = f;
	projMatrix[2 * 4 + 2] = (farp + nearp) / (nearp - farp);
	projMatrix[3 * 4 + 2] = (2.0f * farp * nearp) / (nearp - farp);
	projMatrix[2 * 4 + 3] = -1.0f;
	projMatrix[3 * 4 + 3] = 0.0f;

	glBindBuffer(GL_UNIFORM_BUFFER, matricesUniBuffer);
	glBufferSubData(GL_UNIFORM_BUFFER, ProjMatrixOffset, MatrixSize, projMatrix);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

}