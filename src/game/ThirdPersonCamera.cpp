#include "game/ThirdPersonCamera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
namespace strategy {
void ThirdPersonCamera::orbit(float dx,float dy){yawDegrees_=std::fmod(yawDegrees_-dx*0.24F,360.0F);pitchDegrees_=std::clamp(pitchDegrees_+dy*0.18F,-5.0F,55.0F);}
void ThirdPersonCamera::zoom(float wheel){distance_=std::clamp(distance_-wheel*0.5F,3.0F,10.0F);}
glm::vec2 ThirdPersonCamera::groundMovement(float forward,float right)const{const float yaw=glm::radians(yawDegrees_);glm::vec3 movement={-std::sin(yaw)*forward+std::cos(yaw)*right,0.0F,-std::cos(yaw)*forward-std::sin(yaw)*right};if(glm::length(movement)>0)movement=glm::normalize(movement);return{movement.x,movement.z};}
CameraView ThirdPersonCamera::view(float aspect,const glm::vec3& feet)const{const glm::vec3 target=feet+glm::vec3{0,1.35F,0};const float yaw=glm::radians(yawDegrees_),pitch=glm::radians(pitchDegrees_);const float horizontal=std::cos(pitch)*distance_;const glm::vec3 eye=target+glm::vec3{std::sin(yaw)*horizontal,std::sin(pitch)*distance_+0.6F,std::cos(yaw)*horizontal};CameraView result;result.position=eye;result.target=target;result.view=glm::lookAt(eye,target,{0,1,0});result.projection=glm::perspective(glm::radians(68.0F),aspect,0.05F,350.0F);result.detailDistance=12.0F;return result;}
float ThirdPersonCamera::characterFacingDegrees()const{return std::fmod(yawDegrees_+180.0F,360.0F);}
}
