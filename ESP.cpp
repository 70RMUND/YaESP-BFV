#include "pch.h"
#include "ESP.h"
#include "Mem.h"
#include "ObfuscationMgr.h"
#include "EntityMgr.h"
#include "Aero/include/render/Overlay.hpp"
#include "Aero/include/render/Device2D.hpp"
#include "Aero/include/render/Device3D9.hpp"
using namespace render;

ESP* ESP::m_pInstance = NULL;

inline bool IsSoldierOccluded(DWORD64 ClientSoldierEntity)
{
	return m_pMem->Start(ClientSoldierEntity)->load<bool>(CLIENTSOLDIER_OCCLUDED_OFFSET);
}


inline void GetNameFromPlayer(DWORD64 ClientPlayer, char* name)
{ // expects name to hold at least 16 bytes
	stringblob tmp = m_pMem->Start(ClientPlayer)->r(CLIENTPLAYER_NAME_OFFSET)->load<stringblob>(0);
	char * tname = (char*)&tmp;
	for (int i = 0; i < 16; i++) name[i] = tname[i];
}

inline BYTE GetClientPlayerTeamID(DWORD64 ClientPlayer)
{
	return m_pMem->Start(ClientPlayer)->load<BYTE>(CLIENTPLAYER_TEAMID_OFFSET);
}

inline AxisAlignedBox GetSoldierAABB(DWORD64 ClientSolider)
{
	AxisAlignedBox box;
    BYTE pose = m_pMem->Start(ClientSolider)->load<BYTE>(0x9A8);

	if (pose == 0)
	{
		box.max = { -0.475, 0.000, -0.375, 0.0 };
		box.min = { 0.375, 2.300,  0.475, 0.0 };
	}
	else if (pose == 1)
	{
		box.max = { -0.375, 0.000, -0.875, 0.0 };
		box.min = { 0.475, 1.250, 0.675, 0.0 };
	}
	else if (pose == 2)
	{
		box.max = { -0.575, 0.000, -1.575, 0.0 };
		box.min = { 0.575, 0.700, 1.375, 0.0 };
	}
	
	return box;
}


inline DWORD64 GetSoldierFromPlayer(DWORD64 ClientPlayer)
{
	return m_pMem->Start(ClientPlayer)->r(CLIENTPLAYER_SOLDIER_OFFSET)->load(0x0) - 0x8; // eastl::weakptr
}

inline DWORD64 GetVehicleFromPlayer(DWORD64 ClientPlayer)
{
	return m_pMem->Start(ClientPlayer)->r(CLIENTPLAYER_VEHICLE_OFFSET)->load(0x0) - 0x8; // eastl::weakptr
}

inline DWORD64 GetHealthComponent(DWORD64 ClientSoldierEntity)
{
	return m_pMem->Start(ClientSoldierEntity)->load(CLIENTSOLDIER_HEALTHCOMPONENT_OFFSET);
}

bool IsAlive(DWORD64 HealthComponent)
{
	BYTE AliveFlag = m_pMem->Start(HealthComponent)->r(0x5C8)->r(0)->r(0x28)->load<BYTE>(0x1);
	if (AliveFlag == 0x01)
		return true;
	return false;
}

LinearTransform GetTransform(DWORD64 Entity)
{
	DWORD64 m_collection = m_pMem->Start(Entity)->load(0x40);
	BYTE _9 = m_pMem->Start(m_collection)->load<BYTE>(9);
	BYTE _10 = m_pMem->Start(m_collection)->load<BYTE>(10);
	DWORD64 ComponentCollectionOffset = 0x20 * (_10 + (2 * _9));
	return m_pMem->Start(m_collection)->load<LinearTransform>(ComponentCollectionOffset + 0x10);
}

double Distance2D(float x1, float y1, float x2, float y2)
{
	return sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
}

//credits to Chevyyy
float XAngle(float x1, float y1, float x2, float y2, float myangle)
{
	float dl = Distance2D(x1, y1, x2, y2);
	if (dl == 0)dl = 1.0;
	float dl2 = abs((int)(x2 - x1));
	float teta = ((180.0 / D3DX_PI)*acos(dl2 / dl));
	if (x2 < x1)teta = 180 - teta;
	if (y2 < y1)teta = teta * -1.0;
	teta = teta - myangle;
	if (teta > 180.0)teta = (360.0 - teta)*(-1.0);
	if (teta < -180.0)teta = (360.0 + teta);
	return teta;
}

//I don't want to include or link the DirectX SDK neither do I want to do it my self ^^
static HMODULE hD3dx9_43 = NULL;
void RotatePointAlpha(float *outV, float x, float y, float z, float cx, float cy, float cz, float alpha)
{
	if (hD3dx9_43 == NULL)
		hD3dx9_43 = LoadLibraryA("d3dx9_43.dll");

	typedef LinearTransform* (WINAPI* t_D3DXMatrixRotationY)(LinearTransform *pOut, FLOAT Angle);
	static t_D3DXMatrixRotationY D3DXMatrixRotationY = NULL;
	if (D3DXMatrixRotationY == NULL)
		D3DXMatrixRotationY = (t_D3DXMatrixRotationY)GetProcAddress(hD3dx9_43, "D3DXMatrixRotationY");

	typedef Vec4* (WINAPI* t_D3DXVec3Transform)(Vec4 *pOut, CONST Vec4 *pV, CONST LinearTransform *pM);
	static t_D3DXVec3Transform D3DXVec4Transform = NULL;
	if (D3DXVec4Transform == NULL)
		D3DXVec4Transform = (t_D3DXVec3Transform)GetProcAddress(hD3dx9_43, "D3DXVec4Transform");

	LinearTransform rot1;
	Vec4 vec;
	vec.x = x - cx;
	vec.z = y - cy;
	vec.y = z - cz;
	vec.w = 1.0;
	D3DXMatrixRotationY(&rot1, alpha*D3DX_PI / 180.0);
	D3DXVec4Transform(&vec, &vec, &rot1);
	outV[0] = vec.x + cx;
	outV[1] = vec.z + cy;
	outV[2] = vec.y + cz;
};


bool GetBonePos(DWORD64 SoldierEntity, int BoneId, Vec4 *vOut)
{
	DWORD64 BoneCollisionComponent = m_pMem->Start(SoldierEntity)->load(0x6e0);
	if (!ValidPointer(BoneCollisionComponent))
		return false;

	bool ValidTransforms = m_pMem->Start(BoneCollisionComponent)->load<bool>(0x38);
	if (!ValidTransforms) return false;


	QuatTransform* pQuat = m_pMem->Start(BoneCollisionComponent)->load<QuatTransform*>(0x20);
	if (!ValidPointer(pQuat))
		return false;

	QuatTransform Quat = m_pMem->Start(BoneCollisionComponent)->r(0x20)->load<QuatTransform>(sizeof(QuatTransform)*BoneId);

	vOut->x = Quat.m_TransAndScale.x;
	vOut->y = Quat.m_TransAndScale.y;
	vOut->z = Quat.m_TransAndScale.z;
	vOut->w = Quat.m_TransAndScale.w;

	return true;
}



bool ScreenProject(Vec4 WorldPos, Vec4* ScreenPos)
{
	auto m_pESP = ESP::GetInstance();
	auto viewProj = m_pESP->GetViewProjection();
	
	float mX = m_pESP->overlay->m_Width * 0.5f;
	float mY = m_pESP->overlay->m_Height * 0.5f;


	float w =
		viewProj.right.w	* WorldPos.x +
		viewProj.up.w		* WorldPos.y +
		viewProj.forward.w	* WorldPos.z +
		viewProj.trans.w;

	if (w < 0.65f)
	{
		ScreenPos->z = w;
		return false;
	}

	float x =
		viewProj.right.x	* WorldPos.x +
		viewProj.up.x		* WorldPos.y +
		viewProj.forward.x	* WorldPos.z +
		viewProj.trans.x;

	float y =
		viewProj.right.y	* WorldPos.x +
		viewProj.up.y		* WorldPos.y +
		viewProj.forward.y	* WorldPos.z +
		viewProj.trans.y;

	ScreenPos->x = mX + mX * x / w;
	ScreenPos->y = mY - mY * y / w;
	ScreenPos->z = w;

	return true;
}

Vec4 Vec4Transform(Vec4 In, LinearTransform * pm)
{
	Vec4 Out;

	Out.x = pm->m[0][0] * In.x + pm->m[1][0] * In.y + pm->m[2][0] * In.z + pm->m[3][0];
	Out.y = pm->m[0][1] * In.x + pm->m[1][1] * In.y + pm->m[2][1] * In.z + pm->m[3][1];
	Out.z = pm->m[0][2] * In.x + pm->m[1][2] * In.y + pm->m[2][2] * In.z + pm->m[3][2];
	Out.w = 0.0;

	return Out;
}

bool ScreenProjectAABB(AxisAlignedBox* InBox, LinearTransform* trans, AxisAlignedBox2* OutBox)
{
	AxisAlignedBox box = *InBox;

	OutBox->updateBox(&box);

	OutBox->max = Vec4Transform(box.max, trans);
	OutBox->min = Vec4Transform(box.min, trans);

	OutBox->crnr2 = Vec4Transform(OutBox->crnr2, trans);
	OutBox->crnr3 = Vec4Transform(OutBox->crnr3, trans);
	OutBox->crnr4 = Vec4Transform(OutBox->crnr4, trans);
	OutBox->crnr5 = Vec4Transform(OutBox->crnr5, trans);
	OutBox->crnr6 = Vec4Transform(OutBox->crnr6, trans);
	OutBox->crnr7 = Vec4Transform(OutBox->crnr7, trans);

	// ScreenProject === World2Screen

	bool bSP = ScreenProject(OutBox->crnr2, &OutBox->crnr2);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->crnr3, &OutBox->crnr3);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->crnr4, &OutBox->crnr4);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->crnr5, &OutBox->crnr5);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->crnr6, &OutBox->crnr6);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->crnr7, &OutBox->crnr7);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->max, &OutBox->max);
	if (bSP == false) return false;
	bSP = ScreenProject(OutBox->min, &OutBox->min);
	if (bSP == false) return false;

	return true;
}

void DrawAABB(AxisAlignedBox2* AABB, drawing::Color Color)
{
	auto s = ESP::GetInstance()->overlay->get_surface();

	//min to 2,4,6
	s->line(AABB->min.x, AABB->min.y, AABB->crnr2.x, AABB->crnr2.y, Color);
	s->line(AABB->min.x, AABB->min.y, AABB->crnr4.x, AABB->crnr4.y, Color);
	s->line(AABB->min.x, AABB->min.y, AABB->crnr6.x, AABB->crnr6.y, Color);

	//max to 5,7,3
	s->line(AABB->max.x, AABB->max.y, AABB->crnr5.x, AABB->crnr5.y, Color);
	s->line(AABB->max.x, AABB->max.y, AABB->crnr7.x, AABB->crnr7.y, Color);
	s->line(AABB->max.x, AABB->max.y, AABB->crnr3.x, AABB->crnr3.y, Color);

	//2 to 7,3
	s->line(AABB->crnr2.x, AABB->crnr2.y, AABB->crnr3.x, AABB->crnr3.y, Color);
	s->line(AABB->crnr2.x, AABB->crnr2.y, AABB->crnr7.x, AABB->crnr7.y, Color);

	//4 to 5,3
	s->line(AABB->crnr4.x, AABB->crnr4.y, AABB->crnr5.x, AABB->crnr5.y, Color);
	s->line(AABB->crnr4.x, AABB->crnr4.y, AABB->crnr3.x, AABB->crnr3.y, Color);

	//6 to 5,7
	s->line(AABB->crnr6.x, AABB->crnr6.y, AABB->crnr5.x, AABB->crnr5.y, Color);
	s->line(AABB->crnr6.x, AABB->crnr6.y, AABB->crnr7.x, AABB->crnr7.y, Color);
}

bool ScreenCoords(Vec4 LocalPlayerTransform, Vec4 RemoteEntityTransform, Vec4* EntityScreen_xywh)
{

	Vec4 vecPosition = RemoteEntityTransform, vecScreenPos;
	if (!ScreenProject(vecPosition, &vecScreenPos))
		return false;

	float anglex = XAngle(LocalPlayerTransform.x,
		LocalPlayerTransform.z,
		vecPosition.x,
		vecPosition.z,
		0
	);
	float posl[4], posr[4];

	RotatePointAlpha(posl, -0.5, 0, 1.8, 0, 0, 0, -anglex + 90);
	RotatePointAlpha(posr, 0.5, 0, 0, 0, 0, 0, -anglex + 90);

	Vec4 vposl, vposr;

	vposl.x = vecPosition.x + posl[0];
	vposl.y = vecPosition.y + posl[2];
	vposl.z = vecPosition.z + posl[1];
	vposl.w = 0;
	vposr.x = vecPosition.x + posr[0];
	vposr.y = vecPosition.y + posr[2];
	vposr.z = vecPosition.z + posr[1];
	vposr.w = 0;
	Vec4 screenPosl, screenPosr;

	if (!ScreenProject(vposl, &screenPosl) ||
		!ScreenProject(vposr, &screenPosr)
		)
	{
		return false;
	}

	EntityScreen_xywh->v[0] = screenPosr.x;
	EntityScreen_xywh->v[1] = screenPosl.y;
	EntityScreen_xywh->v[2] = abs((int)(screenPosr.x - screenPosl.x));
	EntityScreen_xywh->v[3] = abs((int)(screenPosl.y - screenPosr.y));
	return true;
}

void DrawBone(Vec4 from, Vec4 to, drawing::Color Color)
{
	auto s = ESP::GetInstance()->overlay->get_surface();
	Vec4 W2S_from;
	if (!ScreenProject(from, &W2S_from)) return;

	Vec4 W2S_to;
	if (!ScreenProject(to, &W2S_to)) return;

	s->line(W2S_from.x, W2S_from.y, W2S_to.x, W2S_to.y, Color);
}

void DrawBox(AxisAlignedBox* box, LinearTransform* transform, drawing::Color Color)
{
	AxisAlignedBox2 box2;
	if (ScreenProjectAABB(box, transform, &box2))
		DrawAABB(&box2, Color);
}

void DrawInfo(LinearTransform* transform, char* name)
{
	Vec4 pos;
	auto s = ESP::GetInstance()->overlay->get_surface();
	Vec4 EntityTransposed;
	if (!ScreenProject(transform->trans, &EntityTransposed)) return;
	s->text(EntityTransposed.x, EntityTransposed.y, "default", 0xFFFFFFFF, name);
}

void DrawMark(Vec4 EntityPos)
{
	Vec4 EntityTransposed;
	Vec4 EntityTransposedCursor[4];
	if (!ScreenProject(EntityPos, &EntityTransposed)) return;
	for (int i = 0; i < 4; i++)
	{
		EntityTransposedCursor[i] = EntityTransposed;
		if (i == 0)
		{
			EntityTransposedCursor[i].x -= 5;
			EntityTransposedCursor[i].y -= 5;
		}
		if (i == 1)
		{
			EntityTransposedCursor[i].x += 5;
			EntityTransposedCursor[i].y += 5;
		}
		if (i == 2)
		{
			EntityTransposedCursor[i].x -= 5;
			EntityTransposedCursor[i].y += 5;
		}
		if (i == 3)
		{
			EntityTransposedCursor[i].x += 5;
			EntityTransposedCursor[i].y -= 5;
		}
	}
	auto s = ESP::GetInstance()->overlay->get_surface();
	s->line(EntityTransposedCursor[0].x, EntityTransposedCursor[0].y, EntityTransposedCursor[1].x, EntityTransposedCursor[1].y, drawing::Color(0xFF, 0, 0, 0xFF));
	s->line(EntityTransposedCursor[2].x, EntityTransposedCursor[2].y, EntityTransposedCursor[3].x, EntityTransposedCursor[3].y, drawing::Color(0xFF, 0, 0, 0xFF));
}

void DrawSkeletonESP(DWORD64 pEnemySoldier, drawing::Color Color)
{
	Vec4 BonePosition[12];
	int BoneID[12] = {
				 BONE_Head,				//00
				 BONE_LeftHand,			//01
				 BONE_RightHand,		//02
				 BONE_RightFoot,		//03
				 BONE_RightKneeRoll,	//04
				 BONE_LeftKneeRoll,		//05
				 BONE_LeftFoot,			//06
				 BONE_RightShoulder,	//07
				 BONE_LeftShoulder,		//08
				 BONE_Spine,			//09
				 BONE_RightElbowRoll,	//10
				 BONE_LeftElbowRoll		//11
	};

	for (int i = 0; i < 12; i++)
	{
		if (!GetBonePos(pEnemySoldier, BoneID[i], &BonePosition[i])) return;
	}

	Vec4 TheHead;
	Vec4 HeadCursor[4];
	if (!ScreenProject(BonePosition[0], &TheHead)) return;
	for (int i = 0; i < 4; i++)
	{
		HeadCursor[i] = TheHead;
		if (i == 0)
		{
			HeadCursor[i].x -= 5;
			HeadCursor[i].y -= 5;
		}
		if (i == 1)
		{
			HeadCursor[i].x += 5;
			HeadCursor[i].y += 5;
		}
		if (i == 2)
		{
			HeadCursor[i].x -= 5;
			HeadCursor[i].y += 5;
		}
		if (i == 3)
		{
			HeadCursor[i].x += 5;
			HeadCursor[i].y -= 5;
		}
	}
	auto s = ESP::GetInstance()->overlay->get_surface();
	s->line(HeadCursor[0].x, HeadCursor[0].y, HeadCursor[1].x, HeadCursor[1].y, drawing::Color(0xFF, 0, 0, 0xFF));
	s->line(HeadCursor[2].x, HeadCursor[2].y, HeadCursor[3].x, HeadCursor[3].y, drawing::Color(0xFF, 0, 0, 0xFF));

	// Head -> Hips
	DrawBone(BonePosition[0], BonePosition[9], Color);

	// Hips -> left knee
	DrawBone(BonePosition[9], BonePosition[5], Color);
	// left knee -> left food
	DrawBone(BonePosition[5], BonePosition[6], Color);

	// Hips -> right knee
	DrawBone(BonePosition[9], BonePosition[4], Color);
	// right knee -> right food
	DrawBone(BonePosition[4], BonePosition[3], Color);

	//right shoulder -> left shoulder
	DrawBone(BonePosition[7], BonePosition[8], Color);

	// right shoulder -> right elbow -> right hand
	DrawBone(BonePosition[7], BonePosition[10], Color);
	DrawBone(BonePosition[10], BonePosition[2], Color);

	// left shoulder -> left elbow -> left hand
	DrawBone(BonePosition[8], BonePosition[11], Color);
	DrawBone(BonePosition[11], BonePosition[1], Color);
}

ESP* ESP::GetInstance()
{
	if (!m_pInstance)
		m_pInstance = new ESP();
	return m_pInstance;
}

ESP::ESP()
{
	overlay = NULL;
}

ESP::~ESP()
{
}

LinearTransform ESP::GetViewProjection()
{
	LinearTransform viewProj;
	viewProj = m_pMem->Read(OFFSET_GAMERENDERER)->r(0x60)->load<LinearTransform>(0x04F0);
	return viewProj;
}


void ESP::DrawBanner()
{
	auto s = overlay->get_surface();
	s->text(5, 5, "default", 0xFFFFFFFF, "Tormund's YaESP-BFV");
}

void ESP::DrawCrosshair()
{
	auto s = overlay->get_surface();
	float mX = overlay->m_Width * 0.5f;
	float mY = overlay->m_Height * 0.5f;
	s->line(mX - 10.0, mY, mX + 10.0, mY, drawing::Color(0, 255, 0, 255));
	s->line(mX, mY - 10.0, mX, mY + 10.0, drawing::Color(0, 255, 0, 255));
}

DWORD g_ModelCounter = 0;
bool g_StaticModelsExtracted = false;
std::vector<DWORD64> g_HealthStations;
std::vector<DWORD64> g_AmmoStations;

AxisAlignedBox AABox_ATmine = { { -0.18, 0.000, -0.17, 0.0 }, { 0.185, 0.08,  0.155, 0.0 } };
AxisAlignedBox AABox_Dynamite = { { -0.15, 0, -0.07 , 0.0 }, { 0.185, 0.1,  0.055, 0.0 } };
AxisAlignedBox AABox_APmine = { { -0.06, 0.03, -0.05, 0.0 }, { 0.065, 0.23,  0.055, 0.0 } };

void ESP::DrawModels()
{
	g_ModelCounter++;
	ObfuscationMgr* OM = ObfuscationMgr::GetInstance();
	EntityMgr* EM = EntityMgr::GetInstance();
	ESP * e = ESP::GetInstance();
	std::vector<DWORD64>* ents;
	drawing::Color EntityColor;
	EntityColor.set(0x00, 0xFF, 0x00, 0xFF);

	DWORD64 LocalPlayer = OM->GetLocalPlayer();
	BYTE PlayerTeamId = GetClientPlayerTeamID(LocalPlayer);

	if (!g_StaticModelsExtracted)
	{

		EM->assign_type(TYPEINFO_ClientStaticModelEntity);
		ents = EM->get();
		g_HealthStations.clear();
		g_AmmoStations.clear();

		for (int i = 0; i < ents->size(); i++)
		{
			DWORD64 ent = ents->at(i);
			std::string Name;
			stringblob ModelName;
			m_pMem->Start(ent)->r(0x38)->r(0xA8)->r(0x18)->Get<stringblob>(0x0, &ModelName);
			Name.assign((const char *)&ModelName);
			if (Name.find("supplystation_health/healthstation_Mesh") != std::string::npos)
				g_HealthStations.push_back(ent);
			else if (Name.find("supplystation_ammo/ammostation_Mesh") != std::string::npos)
				g_AmmoStations.push_back(ent);
		}
		g_StaticModelsExtracted = true;
	}
	else 
	{
		for (int i = 0; i < g_HealthStations.size(); i++)
		{
			DWORD64 ent = g_HealthStations.at(i);
			DWORD64 vtbl = m_pMem->Start(ent)->load(0x0);
			if (!ValidPointer(ent) || (vtbl != TYPEINFO_ClientStaticModelEntity_vtbl))
			{
				g_StaticModelsExtracted = false;
				break;
			}
			LinearTransform EntityTransform = GetTransform(ent);
			AxisAlignedBox VehEnemyBox = m_pMem->Start(ent)->load<AxisAlignedBox>(0x240);
			EntityColor.set(0xD8, 0x00, 0xFF, 0xFF);
			DrawBox(&VehEnemyBox, &EntityTransform, EntityColor);
		}
		for (int i = 0; i < g_AmmoStations.size(); i++)
		{
			DWORD64 ent = g_AmmoStations.at(i);
			DWORD64 vtbl = m_pMem->Start(ent)->load(0x0);
			if (!ValidPointer(ent) || (vtbl != TYPEINFO_ClientStaticModelEntity_vtbl))
			{
				g_StaticModelsExtracted = false;
				break;
			}
			LinearTransform EntityTransform = GetTransform(ent);
			AxisAlignedBox VehEnemyBox = m_pMem->Start(ent)->load<AxisAlignedBox>(0x240);
			EntityColor.set(0x00, 0x00, 0xFF, 0xFF);
			DrawBox(&VehEnemyBox, &EntityTransform, EntityColor);
		}
		if ((g_HealthStations.size() == 0) && (g_AmmoStations.size() == 0))
			g_StaticModelsExtracted = false;
	}

	EM->assign_type(TYPEINFO_ClientExplosionPackEntity);
	ents = EM->get();

	for (int i = 0; i < ents->size(); i++)
	{
		DWORD64 ent = ents->at(i);

		BYTE entTeamId = m_pMem->Start(ent)->load<BYTE>(0x550);
		if (entTeamId == PlayerTeamId)
			continue;

		LinearTransform EntityTransform = GetTransform(ent);

		stringblob exp_strb;
		m_pMem->Start(ent)->r(0x38)->r(0x100)->r(0x18)->Get<stringblob>(0x0, &exp_strb);
		std::string exp_str((const char *)&exp_strb);

		if ((g_ModelCounter % 11) < 5)
			EntityColor.set(0xFF, 0x00, 0x00, 0xFF);
		else
			EntityColor.set(0x00, 0xFF, 0x00, 0xFF);

		if (exp_str.find("deployable_antitank_mine") != std::string::npos)
			DrawBox(&AABox_ATmine, &EntityTransform, EntityColor);
		else if (exp_str.find("deployable_dynamite_sticky") != std::string::npos)
			DrawBox(&AABox_Dynamite, &EntityTransform, EntityColor);
		else if (exp_str.find("deployable_smine") != std::string::npos)
			DrawBox(&AABox_APmine, &EntityTransform, EntityColor);
	}

	EM->assign_type(TYPEINFO_ClientVehicleEntity);
	ents = EM->get();
	
	for (int i = 0; i < ents->size(); i++)
	{
		DWORD64 ent = ents->at(i);

		LinearTransform EntityTransform = GetTransform(ent);
		AxisAlignedBox VehEnemyBox = m_pMem->Start(ent)->load<AxisAlignedBox>(0x460);
		
		DWORD teamid = m_pMem->Start(ent)->load<DWORD>(0x234);
		stringblob VehName;
		std::string Name;
		m_pMem->Start(ent)->r(0x38)->r(0x1f8)->Get<stringblob>(0x0, &VehName);
		Name.assign((const char *)&VehName);

		if (teamid != GetClientPlayerTeamID(OM->GetLocalPlayer()))
		{
			if (Name.find("Spawn") != std::string::npos)
			{
				if ((g_ModelCounter%11) < 5)
					EntityColor.set(0xFF, 0x00, 0x00, 0xFF);
				else
					EntityColor.set(0x00, 0xFF, 0x00, 0xFF);
				DrawBox(&VehEnemyBox, &EntityTransform, EntityColor);
			}
			
		}
	}
}

void ESP::DrawClientplayerESP()
{
	
	// Grab Obfuscation Manager and Surface
	ObfuscationMgr* OM = ObfuscationMgr::GetInstance();
	auto s = overlay->get_surface();

	// Get your local ClientPlayer
	DWORD64 LocalPlayer = OM->GetLocalPlayer();
	RETURN_IF_BAD(LocalPlayer);

	// Get ClientPlayer Team ID to discern friend from foe
	BYTE LocalPlayerTeamId = GetClientPlayerTeamID(LocalPlayer);

	// Get your local Soldier
	DWORD64 LocalSoldier = GetSoldierFromPlayer(LocalPlayer);
	RETURN_IF_BAD(LocalSoldier);

	// Grab the Solider's transform for positioning
	LinearTransform LocalSoldierTransform = GetTransform(LocalSoldier);
	Vec4 vecLocalPosition = LocalSoldierTransform.trans;

	auto PlayerList = OM->GetPlayers();
	for (int i = 0; i < PlayerList->size(); i++)
	{
		DWORD64 ClientPlayer = PlayerList->at(i);
		CONTINUE_IF_BAD(ClientPlayer);

		// Skip LocalPlayer
		if (ClientPlayer == LocalPlayer)
			continue;

		// Skip Teammates
		BYTE PlayerTeamId = GetClientPlayerTeamID(ClientPlayer);
		if (PlayerTeamId == LocalPlayerTeamId)
			continue;

		// Get enemy's Soldier for positioning
		DWORD64 EnemySoldier = GetSoldierFromPlayer(ClientPlayer);
		CONTINUE_IF_BAD(EnemySoldier);

		// Get enemy's Vehicle for positioning
		DWORD64 EnemyVehicle = GetVehicleFromPlayer(ClientPlayer);

		// If enemy is dead go to the next clientplayer
		DWORD64 HealthComponent = GetHealthComponent(EnemySoldier);
		if (!ValidPointer(HealthComponent) || !IsAlive(HealthComponent))
			continue;

		drawing::Color PlayerColor;
		LinearTransform PlayerTransform;
		bool inVehicle = false;

		// If the enemy is in the vehicle then set the vehicle ESP
		if (ValidPointer(EnemyVehicle))
		{
			PlayerColor.set(0x00, 0xFF, 0xD8, 0xFF);
			inVehicle = true;
			PlayerTransform = GetTransform(EnemyVehicle);
			AxisAlignedBox VehEnemyBox = m_pMem->Start(EnemyVehicle)->load<AxisAlignedBox>(0x460);
			DrawBox(&VehEnemyBox, &PlayerTransform, PlayerColor);
		}
		else // Otherwise if the enemy is on foot then set infantry ESP
		{
			PlayerTransform = GetTransform(EnemySoldier);
			PlayerColor.set(0xFF, 0x00, 0x00, 0xFF);

			bool PlayerOccluded = IsSoldierOccluded(EnemySoldier);
			if (!PlayerOccluded) // Change the ESP color if enemy is visible
				PlayerColor.set(0x00, 0xFF, 0x00, 0xFF);
			inVehicle = false;
		}

		Vec4 Player_xywh;
		if (!ScreenCoords(vecLocalPosition, PlayerTransform.trans, &Player_xywh)) 
			continue;
		
		float flSoldierPos_x = Player_xywh.v[0]; // x
		float flSoldierPos_y = Player_xywh.v[1]; // y
		float flSoldierPos_w = Player_xywh.v[2]; // w
		float flSoldierPos_h = Player_xywh.v[3]; // h

		//Draw the name of the Player:
		char pName[17]; memset(pName, 0, 17);
		GetNameFromPlayer(ClientPlayer, pName);
		s->text(flSoldierPos_x, flSoldierPos_y + flSoldierPos_h + 6, "default", PlayerColor, pName);

		if (!inVehicle){
		
			s->border_box(flSoldierPos_x, flSoldierPos_y, flSoldierPos_w, flSoldierPos_h, 1, PlayerColor);
		
			float health = m_pMem->Start(HealthComponent)->load<float>(0x20);
			float maxhealth = m_pMem->Start(HealthComponent)->load<float>(0x24);
			float armorvalue = m_pMem->Start(HealthComponent)->load<float>(0x114);

			//Draw health status box:
			s->border_box_outlined(flSoldierPos_x, flSoldierPos_y - 10, flSoldierPos_w * (health / maxhealth), 3, 2, drawing::Color(0xFF, 0, 0, 0xFF));
			

			drawing::Color lightblue(0xd6, 0xea, 0xf8, 0xFF);
			drawing::Color medblue(0x5d, 0xad, 0xe2, 0xFF);
			drawing::Color darkblue(0x28, 0x74, 0xa6, 0xFF);
			drawing::Color* armor;
			if (armorvalue <= 50.0) armor = &lightblue;
			else if ((armorvalue > 50.0) && (armorvalue <= 100.0))
				armor = &medblue;
			else armor = &darkblue;

			//Draw armor status box:
			if ((armorvalue > 1.0) && (armorvalue < 151.0))
			{
				s->border_box_outlined(flSoldierPos_x, flSoldierPos_y - 15,
					flSoldierPos_w * (armorvalue / 150.0), 3, 2, *armor);
			}
			// Draw the Enemy Bones
			DrawSkeletonESP(EnemySoldier, PlayerColor);
		}
	}

}
