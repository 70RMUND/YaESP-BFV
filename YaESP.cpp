// YaESP.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include "Mem.h"
#include "ObfuscationMgr.h"
#include "EntityMgr.h"
#include "ESP.h"

ESP* m_pESP;
render::Overlay::RenderCallback *callback;

std::unique_ptr<Overlay> create_overlay(const EDeviceType type, const std::string& window_title)
{
	auto overlay = Overlay::New(type);
	if (!overlay) {
		return nullptr;
	}
	if (!overlay->create(window_title)) {
		return nullptr;
	}

	auto surface = overlay->get_surface();
	if (!surface) {
		return nullptr;
	}

	const auto is_d3d9 = type == EDeviceType::Direct3D9;

	if (!surface->add_font(
		"default",
		"Segoe UI",
		is_d3d9 ? 24 : 22,
		is_d3d9 ? FW_NORMAL : DWRITE_FONT_WEIGHT_NORMAL,
		/// or DEFAULT_QUALITY instead of PROOF_QUALITY for anti aliasing
		is_d3d9 ? PROOF_QUALITY : DWRITE_FONT_STRETCH_NORMAL
	)) {
		return nullptr;
	}

	return std::move(overlay);
}



void OverlayDraw(Surface* s)
{
	if((GetKeyState(VK_CAPITAL) & 0x0001) != 0) return;
	m_pESP->DrawBanner();
	m_pESP->DrawCrosshair();
	m_pESP->DrawClientplayerESP();	
	m_pESP->DrawModels();
}

int main()
{
	DWORD64 ptr = 0x0;
	printf("Start\n");
	m_pMem = new Mem();
	m_pESP = ESP::GetInstance();
	ObfuscationMgr* OM = ObfuscationMgr::GetInstance();

	m_pESP->overlay = create_overlay(EDeviceType::Direct3D9, "Battlefield™ V");
	printf("[+] Created Overlay Object: 0x%I64X\n", m_pESP->overlay.get());



	callback = m_pESP->overlay->add_callback("YaESP", OverlayDraw);

	while (m_pESP->overlay->render())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return ERROR_SUCCESS;
				

	printf("End\n");
	delete m_pESP;
	delete m_pMem;
	system("pause");
}
