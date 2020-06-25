#include "pch.h"
#include "ObfuscationMgr.h"
#include "StackAccess.h"
#include "Mem.h"
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include "offsets.h"

ObfuscationMgr* ObfuscationMgr::m_pInstance = NULL;
static DWORD64 g_MultiplayerKey = OBFUS_STATIC_KEY;
static DWORD64 g_Dx11EncBuffer = 0;
static bool g_CryptMode = 0;

// This function is a standard decode pointer function used by all encrypted entity pointers
static DWORD64 decode_pointer(DWORD64 key, DWORD64 EncodedPtr)
{
	key ^= (5 * key);

	DWORD64 DecryptedPtr = NULL;
	BYTE* pDecryptedPtrBytes = (BYTE*)&DecryptedPtr;
	BYTE* pEncryptedPtrBytes = (BYTE*)&EncodedPtr;
	BYTE* pKeyBytes = (BYTE*)&key;

	for (char i = 0; i < 7; i++)
	{
		pDecryptedPtrBytes[i] = (pKeyBytes[i] * 0x3B) ^ (pEncryptedPtrBytes[i] + pKeyBytes[i]);
		key += 8;
	}
	pDecryptedPtrBytes[7] = pEncryptedPtrBytes[7];

	DecryptedPtr &= ~(0x8000000000000000); //to exclude the check bit

	return DecryptedPtr;
}

DWORD64 hashtable_find(DWORD64 table, DWORD64 key)
{
	DWORD bucketcount = 0x0;
	DWORD elemcount = 0x0;
	DWORD64 startcount = 0x0;
	DWORD64 node = 0x0;

	m_pMem->Start(table)->Get<DWORD>(0x10, &bucketcount);
	if (bucketcount == 0)
		return 0;

	m_pMem->Start(table)->Get<DWORD>(0x14, &elemcount);

	startcount = key % bucketcount;

	m_pMem->Start(table)->r(0x8)->Get<DWORD64>(0x8 * startcount, &node);
	if (node == 0)
		return 0;

	DWORD64 first = 0x0;
	DWORD64 second = 0x0;
	DWORD64 next = 0x0;

	while (1)
	{
		m_pMem->Start(node)->Get<DWORD64>(0x0, &first);
		m_pMem->Start(node)->Get<DWORD64>(0x8, &second);
		m_pMem->Start(node)->Get<DWORD64>(0x10, &next);


		if (first == key)
			return second;
		else if (next == 0)
			return 0;

		node = next;
	}

}

DWORD64 ObfuscationMgr::FindObfuscationMgr(ObfuscationMgr* om)
{
	StackAccess * ss = new StackAccess(om->ProtectedThread);
	char* buf;
	char a[] = OBMGR_PATTERN1;
	std::string aa(a, sizeof(a));
	char b[] = OBMFR_PATTERN2;
	std::string bb(b, sizeof(b));
	std::string haystack;
	std::size_t n;

	while (1)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(10));
		buf = (char*)ss->read();
		if (buf == NULL)
			continue;
		haystack.assign((char*)buf, ss->size());

		n = haystack.find(aa);
		if (n == std::string::npos)
		{
			n = haystack.find(bb);
			if (n == std::string::npos) continue;
		}

		for (int i = -160; i < 160; i = i + 8)
		{
			DWORD64 testptr = *(DWORD64*)&(buf[n + i]);
			DWORD64 testptr2;
			m_pMem->Start(testptr)->Get<DWORD64>(0x0, &testptr2);
			if (!ValidPointer(testptr))
				continue;
			if (testptr2 == OBFUS_MGR_PTR_1)
			{
				delete ss;
				return testptr;
			}
		}
	}
}

inline bool ObfuscationMgr::TestMultiPlayerKey(DWORD64 testkey)
{
	// In this test of the multiplayer key we use ClientStaticModelEntity
	// as our test case (since they always exist and there are many of them)
	Mem* mem = m_pMem;
	DWORD64 flink = mem->Start(TYPEINFO_ClientStaticModelEntity)->load(0x88);
	DWORD64 typeinfo_key = mem->Start(TYPEINFO_ClientStaticModelEntity)->load(0x0);
	DWORD64 hashkey = mem->Start(m_pGameObfMgr)->load(0xE0);
	DWORD64 HashTableKey = typeinfo_key ^ hashkey;
	DWORD64 HashTable = m_pGameObfMgr + 0x78;
	DWORD64 EncryptionKey = hashtable_find(HashTable, HashTableKey);
	DWORD64 ptr, ptr_d;
	
	if (EncryptionKey == 0)
		return false;

	EncryptionKey ^= testkey;
	ptr = decode_pointer(EncryptionKey, flink);
	if (ValidPointer(ptr)) // We decode the first link and validate this pointer
	{
		ptr_d = decode_pointer(EncryptionKey, mem->Start(ptr)->load(0x0));
		if (ValidPointer(ptr_d)) // But as an extra check we also validate the next flink
			return true;
	}
	return false;
}

DWORD64 ObfuscationMgr::GetSecret()
{
	StackAccess * ss = new StackAccess(ProtectedThread);
	char* buf;
	std::string haystack;
	std::size_t addr;
	DWORD64 obfmgr_ret1 = OBFUS_MGR_RET_1;
	std::string needle((char*)&obfmgr_ret1, 8);
	DWORD64 testptr = 0x0;

	while (true)
	{
		buf = (char*)ss->read();
		if (buf == NULL)
			continue;
		haystack.assign((char*)buf, ss->size());
		addr = haystack.find(needle);

		while (addr != std::string::npos)
		{
			testptr = *(DWORD64 *)&buf[addr + 24];
			if (TestMultiPlayerKey(testptr))
				return testptr;
			addr = haystack.find(needle, addr+8);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void ObfuscationMgr::CheckKeyStatus(void)
{
	Mem* mem = m_pMem;

	DWORD64 DecFunc_p1 = 0x0;
	DWORD64 DecFunc_p2 = 0x0;
	DWORD64 Dx11EncBuffer;

	// Get DecFunc
	mem->Start(m_pGameObfMgr)->Get<DWORD64>(0xE0, &DecFunc_p1);
	mem->Start(m_pGameObfMgr)->Get<DWORD64>(0xF8, &DecFunc_p2);
	DWORD64 DecFunc = DecFunc_p1 ^ DecFunc_p2;
	
	// Get Dx11EncBuffer
	mem->Start(m_pGameObfMgr)->Get<DWORD64>(0x100, &Dx11EncBuffer);

	if ((Dx11EncBuffer != 0) && (Dx11EncBuffer != g_Dx11EncBuffer))
	{
		// Get Multiplayer Key
		g_MultiplayerKey = GetSecret();
		printf("[+] Dynamic key loaded, root key set to 0x%I64x \n", g_MultiplayerKey);
		g_Dx11EncBuffer = Dx11EncBuffer;
		g_CryptMode = true;
	}
	else
	{
		if (!g_CryptMode)
		{
			if ((DecFunc == OBFUS_MGR_DEC_FUNC) && (Dx11EncBuffer != 0))
			{
				// Get Multiplayer Key
				g_MultiplayerKey = GetSecret();
				printf("[+] Dynamic key loaded, root key set to 0x%I64x \n", g_MultiplayerKey);
				g_Dx11EncBuffer = Dx11EncBuffer;
				g_CryptMode = true;
			}
		}
		else
		{
			if (DecFunc != OBFUS_MGR_DEC_FUNC)
			{
				g_MultiplayerKey = OBFUS_STATIC_KEY;
				g_Dx11EncBuffer = Dx11EncBuffer;
				g_CryptMode = false;
				printf("[+] Static key loaded, root key set to 0x%I64x", g_MultiplayerKey);
			}
		}
	}
}

ObfuscationMgr::ObfuscationMgr()
{
	m_pMem->Start(PROTECTED_THREAD)->Get<DWORD>(0x0, &ProtectedThread);
	printf("[+] Found Protected Thread ID: 0x%08x\n", ProtectedThread);
	printf("[+] Searching for game ObfuscationMgr...\n");
	m_pGameObfMgr = ObfuscationMgr::FindObfuscationMgr(this);
	printf("[+] Found ObfuscationMgr @ 0x%I64x\n", m_pGameObfMgr);
}

ObfuscationMgr::~ObfuscationMgr()
{
}

ObfuscationMgr* ObfuscationMgr::GetInstance()
{
	if (!m_pInstance)
		m_pInstance = new ObfuscationMgr();
	return m_pInstance;
}

DWORD64 ObfuscationMgr::GetLocalPlayer()
{
	DWORD64 ClientPlayerManager = 0x0;
	DWORD64 LocalPlayerListXorValue1 = 0x0;
	DWORD64 LocalPlayerListXorValue2 = 0x0;
	DWORD64 LocalPlayerListKey = 0x0;
	DWORD64 hashtable = m_pGameObfMgr + 0x10;
	DWORD64 EncryptedPlayerManager = 0x0;
	DWORD MaxPlayerCount = 0;
	DWORD64 XorValue1_p1 = 0x0;
	DWORD64 XorValue1_p2 = 0x0;
	DWORD64 XorValue1 = 0x0;
	DWORD64 XorValue2_p1 = 0x0;
	DWORD64 XorValue2_p2 = 0x0;
	DWORD64 XorValue2 = 0x0;
	DWORD64 LocalPlayer = 0x0;

	CheckKeyStatus();

	m_pMem->Read(CLIENT_GAME_CONTEXT)->Get<DWORD64>(0x60, &ClientPlayerManager);
	if (!ValidPointer(ClientPlayerManager))
		return NULL;

	m_pMem->Start(ClientPlayerManager)->Get<DWORD64>(0xF8, &LocalPlayerListXorValue1);
	m_pMem->Start(m_pGameObfMgr)->Get<DWORD64>(0xE0, &LocalPlayerListXorValue2);

	LocalPlayerListKey = LocalPlayerListXorValue1 ^ LocalPlayerListXorValue2;

	EncryptedPlayerManager = hashtable_find(hashtable, LocalPlayerListKey);

	if (!ValidPointer(EncryptedPlayerManager))
		return NULL;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD>(0x18, &MaxPlayerCount);
	if (MaxPlayerCount != 1)
		return NULL;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x20, &XorValue1_p1);
	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x8, &XorValue1_p2);
	XorValue1 = XorValue1_p1 ^ XorValue1_p2;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x10, &XorValue2_p1);
	XorValue2 = XorValue2_p1 ^ g_MultiplayerKey;
	m_pMem->Start(XorValue2)->Get<DWORD64>(0x0, &XorValue2_p2);
	LocalPlayer = XorValue2_p2 ^ XorValue1;

	if (!ValidPointer(LocalPlayer))
		return NULL;

	return LocalPlayer;

}

std::vector<DWORD64>* ObfuscationMgr::GetPlayers()
{
	PlayerList.clear();
	for (int i = 0; i < 70; i++) PlayerList.push_back(GetPlayerById(i));
	sort(PlayerList.begin(), PlayerList.end());
	PlayerList.erase(unique(PlayerList.begin(), PlayerList.end()), PlayerList.end());
	return &PlayerList;
}

std::vector<DWORD64>* ObfuscationMgr::GetSpectators()
{
	SpectatorList.clear();
	for (int i = 0; i < 4; i++) SpectatorList.push_back(GetSpectatorById(i));
	sort(SpectatorList.begin(), SpectatorList.end());
	SpectatorList.erase(unique(SpectatorList.begin(), SpectatorList.end()), SpectatorList.end());
	return &SpectatorList;
}

DWORD64 ObfuscationMgr::GetPlayerById(DWORD id)
{
	DWORD64 ClientPlayerManager = 0x0;
	DWORD64 LocalPlayerListXorValue1 = 0x0;
	DWORD64 LocalPlayerListXorValue2 = 0x0;
	DWORD64 LocalPlayerListKey = 0x0;
	DWORD64 hashtable = m_pGameObfMgr + 0x10;
	DWORD64 EncryptedPlayerManager = 0x0;
	DWORD MaxPlayerCount = 0;
	DWORD64 XorValue1_p1 = 0x0;
	DWORD64 XorValue1_p2 = 0x0;
	DWORD64 XorValue1 = 0x0;
	DWORD64 XorValue2_p1 = 0x0;
	DWORD64 XorValue2_p2 = 0x0;
	DWORD64 XorValue2 = 0x0;
	DWORD64 ClientPlayer = 0x0;

	CheckKeyStatus();

	m_pMem->Read(CLIENT_GAME_CONTEXT)->Get<DWORD64>(0x60, &ClientPlayerManager);
	if (!ValidPointer(ClientPlayerManager))
		return NULL;

	m_pMem->Start(ClientPlayerManager)->Get<DWORD64>(0x100, &LocalPlayerListXorValue1);
	m_pMem->Start(m_pGameObfMgr)->Get<DWORD64>(0xE0, &LocalPlayerListXorValue2);

	LocalPlayerListKey = LocalPlayerListXorValue1 ^ LocalPlayerListXorValue2;

	EncryptedPlayerManager = hashtable_find(hashtable, LocalPlayerListKey);

	if (!ValidPointer(EncryptedPlayerManager))
		return NULL;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD>(0x18, &MaxPlayerCount);
	if (MaxPlayerCount != 70)
		return NULL;


	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x20, &XorValue1_p1);
	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x8, &XorValue1_p2);
	XorValue1 = XorValue1_p1 ^ XorValue1_p2;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x10, &XorValue2_p1);
	XorValue2 = XorValue2_p1 ^ g_MultiplayerKey;
	m_pMem->Start(XorValue2)->Get<DWORD64>(0x8*id, &XorValue2_p2);
	ClientPlayer = XorValue2_p2 ^ XorValue1;

	if (!ValidPointer(ClientPlayer))
		return NULL;

	return ClientPlayer;
}

DWORD64 ObfuscationMgr::GetSpectatorById(DWORD id)
{
	DWORD64 ClientPlayerManager = 0x0;
	DWORD64 LocalPlayerListXorValue1 = 0x0;
	DWORD64 LocalPlayerListXorValue2 = 0x0;
	DWORD64 LocalPlayerListKey = 0x0;
	DWORD64 hashtable = m_pGameObfMgr + 0x10;
	DWORD64 EncryptedPlayerManager = 0x0;
	DWORD MaxPlayerCount = 0;
	DWORD64 XorValue1_p1 = 0x0;
	DWORD64 XorValue1_p2 = 0x0;
	DWORD64 XorValue1 = 0x0;
	DWORD64 XorValue2_p1 = 0x0;
	DWORD64 XorValue2_p2 = 0x0;
	DWORD64 XorValue2 = 0x0;
	DWORD64 SpectatorPlayer = 0x0;

	CheckKeyStatus();

	m_pMem->Read(CLIENT_GAME_CONTEXT)->Get<DWORD64>(0x60, &ClientPlayerManager);
	if (!ValidPointer(ClientPlayerManager))
		return NULL;

	m_pMem->Start(ClientPlayerManager)->Get<DWORD64>(0xF0, &LocalPlayerListXorValue1);
	m_pMem->Start(m_pGameObfMgr)->Get<DWORD64>(0xE0, &LocalPlayerListXorValue2);

	LocalPlayerListKey = LocalPlayerListXorValue1 ^ LocalPlayerListXorValue2;

	EncryptedPlayerManager = hashtable_find(hashtable, LocalPlayerListKey);

	if (!ValidPointer(EncryptedPlayerManager))
		return NULL;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD>(0x18, &MaxPlayerCount);
	if ((MaxPlayerCount == 0) || (id >= MaxPlayerCount))
		return NULL;


	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x20, &XorValue1_p1);
	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x8, &XorValue1_p2);
	XorValue1 = XorValue1_p1 ^ XorValue1_p2;

	m_pMem->Start(EncryptedPlayerManager)->Get<DWORD64>(0x10, &XorValue2_p1);
	XorValue2 = XorValue2_p1 ^ g_MultiplayerKey;
	m_pMem->Start(XorValue2)->Get<DWORD64>(0x8 * id, &XorValue2_p2);
	SpectatorPlayer = XorValue2_p2 ^ XorValue1;

	if (!ValidPointer(SpectatorPlayer))
		return NULL;

	return SpectatorPlayer;
}

DWORD64 ObfuscationMgr::GetEntityKey(DWORD64 TypeInfo)
{
	CheckKeyStatus();
	DWORD64 TypeInfo_ = m_pMem->Start(TypeInfo)->load(0x0);
	DWORD64 HashTableKey = TypeInfo_ ^ m_pMem->Start(m_pGameObfMgr)->load(0xE0);
	DWORD64 HashTable = m_pGameObfMgr + 0x78;
	DWORD64 EncryptionKey = hashtable_find(HashTable, HashTableKey);
	if (EncryptionKey == 0)
		return NULL;
	EncryptionKey ^= g_MultiplayerKey;
	return EncryptionKey;
}

DWORD64 ObfuscationMgr::DecryptPtr(DWORD64 EncryptedPtr, DWORD64 PointerKey)
{
	CheckKeyStatus();
	DWORD64 HashTableKey = PointerKey ^ m_pMem->Start(m_pGameObfMgr)->load(0xE0);
	DWORD64 HashTable = m_pGameObfMgr + 0x78;
	DWORD64 EncryptionKey = hashtable_find(HashTable, HashTableKey);
	if (EncryptionKey == 0)
		return NULL;
	EncryptionKey ^= g_MultiplayerKey;
	return decode_pointer(EncryptionKey, EncryptedPtr);
}