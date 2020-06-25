#pragma once
#include <Windows.h>
#include <atomic>
#include <vector>

class ObfuscationMgr
{
private:
	static ObfuscationMgr* m_pInstance;
	HANDLE keyThread;

	static DWORD64 FindObfuscationMgr(ObfuscationMgr* om);
	inline bool TestMultiPlayerKey(DWORD64 key);
	DWORD64 GetSecret();
	void CheckKeyStatus();
	std::vector<DWORD64> PlayerList;
	std::vector<DWORD64> SpectatorList;
	
public:
	DWORD64 m_pGameObfMgr;
	DWORD ProtectedThread;
	ObfuscationMgr();
	~ObfuscationMgr();
	DWORD64 GetLocalPlayer();
	std::vector<DWORD64>* GetPlayers();
	std::vector<DWORD64>* GetSpectators();
	DWORD64 GetPlayerById(DWORD id);
	DWORD64 GetSpectatorById(DWORD id);
	DWORD64 GetEntityKey(DWORD64 TypeInfo);
	DWORD64 DecryptPtr(DWORD64 EncryptedPtr, DWORD64 PointerKey);

	static ObfuscationMgr* GetInstance();
};

