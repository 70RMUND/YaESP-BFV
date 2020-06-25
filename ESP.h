#pragma once
#include "Aero/include/render/Overlay.hpp"
#include "Aero/include/render/Device2D.hpp"
#include "Aero/include/render/Device3D9.hpp"
using namespace render;

class ESP
{
private:
	static ESP* m_pInstance;

public:
	std::unique_ptr<render::Overlay> overlay;
	ESP();
	~ESP();
	static ESP* GetInstance();
	void DrawBanner();
	void DrawCrosshair();
	void DrawClientplayerESP();
	void DrawModels();
	LinearTransform GetViewProjection();
};

