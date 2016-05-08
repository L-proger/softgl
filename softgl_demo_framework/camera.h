#ifndef camera_h__
#define camera_h__

#include "lmath.h"
#include "gameobject.h"

class Camera {
public:
	float fov;
	float near_clip;
	float far_clip;
	float aspect;

	Camera(Game_object* object):
		fov(45.0f), near_clip(0.03f), far_clip(100.0f), aspect(1.0f), _object(object)
	{}

	lm::float4x4 world_to_camera_matrix(){
		auto tf = _object->transform.get_global_transform();
		return lm::matrix4x4_inverse(tf, false);
	}
	Game_object* GameObject()
	{
		return _object;
	}
private:
	Game_object* _object;
};
#endif // camera_h__