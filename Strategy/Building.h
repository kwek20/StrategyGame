#ifndef __BUILDING_H_INCLUDED__
#define __BUILDING_H_INCLUDED__

#pragma once

#include "Model.h"

class Building : public Model
{
public:
	Building(std::string sFilePath);
	~Building(void);

	virtual const std::string getName(){return "Entity";};
};

#endif