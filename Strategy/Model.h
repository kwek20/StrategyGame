#ifndef __MODEL_H_INCLUDED__
#define __MODEL_H_INCLUDED__

#pragma once

#include <iostream>

#include "object.h"
#include "Mesh.h"

class Model : public Object {
public:
	Model(std::string sFilePath);
	~Model(void);

	virtual void draw();

	bool LoadModelFromFile(std::string sFilePath);
private:
	Mesh *mesh;
};

#endif

