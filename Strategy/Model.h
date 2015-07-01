#ifndef __MODEL_H_INCLUDED__
#define __MODEL_H_INCLUDED__

#pragma once

#include <iostream>

#include "object.h"
#include "Mesh.h"

class Model : public Object {
public:
	Model(Mesh *mesh) : Model(mesh, Vec3<double>(0, 0, 0), Vec3<double>(0, 0, 0)){}
	Model(Mesh *mesh, double x, double y, double z) : Model(mesh, Vec3<double>(x, y, z), Vec3<double>(0, 0, 0)){}
	Model(Mesh *mesh, double x, double y, double z, double xr, double yr, double zr) : Model(mesh, Vec3<double>(x, y, z), Vec3<double>(xr, yr, zr)){}

	Model(Mesh *mesh, Vec3<double> position) : Model(mesh, position, Vec3<double>(0, 0, 0)){};
	Model(Mesh *mesh, Vec3<double> position, Vec3<double> rotation);
	~Model(void);

	virtual void draw();

	virtual const std::string getName(){ return "Model"; };
private:
	Mesh *mesh;
};

#endif

