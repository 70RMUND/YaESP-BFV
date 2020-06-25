#pragma once
#include <vector>
#include <unordered_map>
#include <utility>

class EntityMgr
{
private:
	static EntityMgr* m_pInstance;
	std::vector<DWORD64> entitylist;
	std::unordered_map<DWORD64,std::pair<DWORD64,DWORD>> keyvault;
	DWORD64 currTypeinfo;
	DWORD64 currKey;
	DWORD64 currOffset;
public:
	static EntityMgr* GetInstance();
	void assign_type(DWORD64 typeinfo);
	std::vector<DWORD64>* get();
	EntityMgr();
	~EntityMgr();
};

