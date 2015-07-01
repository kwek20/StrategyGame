#ifndef __BUILDING_H_INCLUDED__
#define __BUILDING_H_INCLUDED__

#pragma once

#include "Model.h"

class Building : public Model
{
public:
	Building(Mesh *mesh) : Model(mesh, Vec3<double>(0, 0, 0), Vec3<double>(0, 0, 0)){}
	Building(Mesh *mesh, double x, double y, double z) : Model(mesh, Vec3<double>(x, y, z), Vec3<double>(0, 0, 0)){}
	Building(Mesh *mesh, double x, double y, double z, double xr, double yr, double zr) : Model(mesh, Vec3<double>(x, y, z), Vec3<double>(xr, yr, zr)){}

	Building(Mesh *mesh, Vec3<double> position) : Model(mesh, position, Vec3<double>(0, 0, 0)){};
	Building(Mesh *mesh, Vec3<double> position, Vec3<double> rotation);
	~Building(void);

	virtual const std::string getName(){return "Building";};
};

#endif