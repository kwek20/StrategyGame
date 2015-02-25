#include "Model.h"

#include <fstream>

#include "assimp/postprocess.h"

const aiScene *Model::loadFile(const char *fileName){
	const aiScene *scene = Import3DFromFile(fileName);
	if (scene) LoadGLTextures(scene, fileName);
	return scene;
}


const aiScene *Model::Import3DFromFile(const std::string& pFile)
{
	//check if file exists
	std::ifstream fin(pFile.c_str());
	if(!fin.fail())
	{
		fin.close();
	}
	else
	{
		std::cout << "Couldn't open file: " << pFile.c_str() << "\n";
		std::cout << importer.GetErrorString() << "\n";
		return false;
	}

	const aiScene *scene = importer.ReadFile( pFile, aiProcessPreset_TargetRealtime_Quality);

	// If the import failed, report it
	if( !scene)
	{
		std::cout << importer.GetErrorString() << "\n";
		return NULL;
	}

	// Now we can access the file's contents.
	//std::cout << "Import of scene " << pFile << " succeeded.\n";

	// We're done. Everything will be cleaned up by the importer destructor
	return scene;
}

int Model::LoadGLTextures(const aiScene* scene, const std::string path){
	if (scene->HasTextures()) return -1;

	/* getTexture Filenames and Numb of Textures */
	for (unsigned int m=0; m<scene->mNumMaterials; m++)
	{
		int texIndex = 0;
		aiReturn texFound = AI_SUCCESS;

		aiString path;	// filename

		while (texFound == AI_SUCCESS)
		{
			texFound = scene->mMaterials[m]->GetTexture(aiTextureType_DIFFUSE, texIndex, &path);
			textureIdMap[path.data] = NULL; //fill map with textures, pointers still NULL yet
			texIndex++;
		}
	}

	int numTextures = textureIdMap.size();

	/* create and fill array with GL texture ids */
	textureIds = new GLuint[numTextures];

	/* define texture path */
	//std::string texturepath = "../../../test/models/Obj/";

	/* get iterator */
	std::map<std::string, GLuint*>::iterator itr = textureIdMap.begin();

	std::string basepath = getBasePath(path);

	ALLEGRO_BITMAP *image;
	for (int i=0; i<numTextures; i++){
		std::string filename = (*itr).first;  // get filename
		(*itr).second =  &textureIds[i];	  // save texture id for filename in map
		itr++;								  // next texture

		//std::cout << "textureIds[i] = " << textureIds[i] << "\n";
		//std::cout << "filename = " << filename << "\n";

		std::string fileloc = basepath + filename;	/* Loading of image */
		image = al_load_bitmap(fileloc.c_str());
		
		if (image) /* If no error occured: */{
			textureIds[i] = al_get_opengl_texture(image);

			//std::cout << "textureIds[i] new = " << textureIds[i] << "\n";

			//glGenTextures(numTextures, &textureIds[i]); /* Texture name generation */
			glBindTexture(GL_TEXTURE_2D, textureIds[i]); /* Binding of texture name */
			//redefine standard texture values
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); /* We will use linear
			interpolation for magnification filter */
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); /* We will use linear
			interpolation for minifying filter */
			/*glTexImage2D(GL_TEXTURE_2D, 0, ilGetInteger(IL_IMAGE_BPP), ilGetInteger(IL_IMAGE_WIDTH),
				ilGetInteger(IL_IMAGE_HEIGHT), 0, ilGetInteger(IL_IMAGE_FORMAT), GL_UNSIGNED_BYTE,
				ilGetData()); *//* Texture specification */
		} else {
			/* Error occured */
			//std::cout << "Couldn't load Image: " << fileloc.c_str() << "\n";
		}
	}

	//return success;
	return TRUE;
}

std::string Model::getBasePath(const std::string& path){
	size_t pos = path.find_last_of("\\/");
	return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}

void Model::loadDataFile(int delSize, std::string path){
	ALLEGRO_PATH *p = al_create_path(path.c_str());
	const char* comp = al_get_path_component(p, -1);

	if (stricmp(al_get_path_extension(p)+1, comp) == 0){
		Resource::loadDataFile(delSize, path);
	}

	al_destroy_path(p);
}