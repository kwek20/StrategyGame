#include "ResourceLoaderModel.h"

#include <fstream>

#include "assimp/postprocess.h"

#include "Model.h"

Model *ModelLoader::loadFile(const char *fileName){
	const aiScene *scene = Import3DFromFile(fileName);
	if (scene) return LoadGLTextures(scene, fileName);
	return NULL;
}


const aiScene *ModelLoader::Import3DFromFile(const std::string& pFile)
{
	//check if file exists
	std::ifstream fin(pFile.c_str());
	if(!fin.fail()) {
		fin.close();
	} else {
		std::cout << "Couldn't open file: " << pFile.c_str() << "\n";
		std::cout << importer.GetErrorString() << "\n";
		return false;
	}

	const aiScene *scene = importer.ReadFile( pFile, aiProcessPreset_TargetRealtime_Quality);

	// If the import failed, report it
	if( !scene){
		std::cout << importer.GetErrorString() << "\n";
		return NULL;
	}

	// Now we can access the file's contents.
	//std::cout << "Import of scene " << pFile << " succeeded.\n";

	// We're done. Everything will be cleaned up by the importer destructor
	return scene;
}

Model *ModelLoader::LoadGLTextures(const aiScene* scene, const std::string modelPath){
	if (scene->HasTextures()) return NULL;

	std::cout << "Loading of scene " << modelPath << "\n";
	Model *model = new Model(scene);

	/* getTexture Filenames and Numb of Textures */
	std::cout << "Materials found: " << scene->mNumMaterials << "\n";
	for (unsigned int m=0; m<scene->mNumMaterials; m++){
		aiReturn texFound = AI_SUCCESS;
		aiString path;	// filename

		std::cout << "textures found: " << scene->mMaterials[m]->GetTextureCount(aiTextureType_DIFFUSE ) << "\n";
		for (unsigned int tCount=0; tCount < scene->mMaterials[m]->GetTextureCount(aiTextureType_DIFFUSE ); tCount++){
			texFound = scene->mMaterials[m]->GetTexture(aiTextureType_DIFFUSE, tCount, &path);
			std::cout << "Current loading texture: " << path.data << "\n";

			model->textureIdMap[path.data] = NULL; //fill map with textures, pointers still NULL yet
		}
	}

	int numTextures = model->textureIdMap.size();

	/* create and fill array with GL texture ids */
	model->textureIds = new GLuint[numTextures];

	/* define texture path */
	//std::string texturepath = "../../../test/models/Obj/";

	/* get iterator */
	std::map<std::string, GLuint*>::iterator itr = model->textureIdMap.begin();

	std::string basepath = getBasePath(modelPath);

	ALLEGRO_BITMAP *image;
	for (int i=0; i<numTextures; i++){
		std::string filename = (*itr).first;  // get filename
		(*itr).second = &model->textureIds[i];	  // save texture id for filename in map
		itr++;								  // next texture

		//std::cout << "textureIds[i] = " << textureIds[i] << "\n";
		//std::cout << "filename = " << filename << "\n";

		std::string fileloc = basepath + filename;	/* Loading of image */
		image = al_load_bitmap(fileloc.c_str());
		std::cout << "Loading bitmap: " << fileloc.c_str() << "\n";

		if (image) /* If no error occured: */{
			model->textureIds[i] = al_get_opengl_texture(image);

			//std::cout << "textureIds[i] new = " << textureIds[i] << "\n";

			//glGenTextures(numTextures, &textureIds[i]); /* Texture name generation */
			glBindTexture(GL_TEXTURE_2D, model->textureIds[i]); /* Binding of texture name */
			//redefine standard texture values
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); /* We will use linear
			interpolation for magnification filter */
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); /* We will use linear
			interpolation for minifying filter */

			/* Texture specification */
			/*glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, al_get_bitmap_height(image),
				al_get_bitmap_width(image), 0, GL_RGBA, GL_UNSIGNED_BYTE,
				image); */

			glGenerateMipmap(GL_TEXTURE_2D);
			std::cout << "Loaded Image: " << fileloc.c_str() << "\n";
		} else {
			/* Error occured */
			std::cout << "Couldn't load Image: " << fileloc.c_str() << "\n";
		}
		
		break;
	}


	std::cout << "----------------------";
	//return success;
	return model;
}

std::string ModelLoader::getBasePath(const std::string& path){
	size_t pos = path.find_last_of("\\/");
	return (std::string::npos == pos) ? "" : path.substr(0, pos + 1);
}

void ModelLoader::loadDataFile(int delSize, std::string path){
	ALLEGRO_PATH *p = al_create_path(path.c_str());
	const char* comp = al_get_path_component(p, -1);

	if (stricmp(al_get_path_extension(p)+1, comp) == 0){
		ResourceLoader::loadDataFile(delSize, path);
	}

	al_destroy_path(p);
}