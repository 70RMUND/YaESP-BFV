#include "pch.h"
#include "EntityMgr.h"
#include "Mem.h"
#include "ObfuscationMgr.h"
#include <algorithm>

EntityMgr* EntityMgr::m_pInstance = NULL;

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

// If Object is a typeinfo object, this function retrieves the typeinfo
DWORD64 GetType(DWORD64 Object)
{
	// Dereference vtable pointer
	DWORD64 vtable = m_pMem->Start(Object)->load(0x0);
	if (!ValidPointer(vtable))
		return NULL;

	// Load the first function (should be GetType)
	DWORD64 GetTypeFunc1 = m_pMem->Start(vtable)->load(0x0);
	if (!ValidPointer(GetTypeFunc1))
		return NULL;

	// There should only be a jmp instruction to the literal function
	BYTE jmpbyte = m_pMem->Start(GetTypeFunc1)->load<byte>(0x0);
	if (jmpbyte != 0xE9)
		return NULL;

	// load the offset to the literal function
	int offset1 = m_pMem->Start(GetTypeFunc1)->load<int>(0x1);
	DWORD64 GetFuncType2 = GetTypeFunc1 + 0x5 + offset1;
	if (!ValidPointer(GetFuncType2))
		return NULL;

	// at the literal function load the offset to the typeinfo
	int offset2 = m_pMem->Start(GetFuncType2)->load<int>(0x3);

	// grab that typeinfo and return it
	DWORD64 typeinfo = GetFuncType2 + 0x7 + offset2;
	if (!ValidPointer(typeinfo))
		return NULL;
	return typeinfo;
}

// Grab single instance object 
EntityMgr* EntityMgr::GetInstance()
{
	if (!m_pInstance)
		m_pInstance = new EntityMgr();
	return m_pInstance;
}

// This function registers your typeinfo to the class object
void EntityMgr::assign_type(DWORD64 typeinfo)
{
	DWORD offset = 0;
	// The instance variables are first cleared
	entitylist.clear();
	currTypeinfo = 0;
	currKey = 0;
	currOffset = 0;

	// load the first encoded pointer to the entity list if it exists
	DWORD64 flink = m_pMem->Start(typeinfo)->load(0x88);
	if (flink == NULL)
		return;

	// Check to see if the entity key parameters have already been cached
	if (keyvault.find(typeinfo) != keyvault.end())
	{
		// if its in the keyvault grab the parameters and bail (they shouldn't change)
		currTypeinfo = typeinfo;
		currKey = keyvault[typeinfo].first;
		currOffset = keyvault[typeinfo].second;
		return;
	}

	// Otherwise lets grab the entitykey from the obfuscation manager and decode the flink
	DWORD64 key = ObfuscationMgr::GetInstance()->GetEntityKey(typeinfo);
	DWORD64 ptr = decode_pointer(key, flink);
	if (!ValidPointer(ptr))
		return;

	// Flink does not point to the beginning of the entity, it points to another forward link at an offset inside the entity.
	// Rather than hardcoding these flink offsets per entity type, this for-loop searches for the start of the entity object
	// by going QWORD by QWORD toward the top of the object to find its vtable and extract its type. Once it hits the vtable
	// the returned type should be equivalent to the typeinfo pointer. (Max search depth is 100 QWORDS)
	for (int i = 0; i < 100; i++)
	{
		if (GetType(ptr - (i * 8)) == typeinfo)
		{
			offset = i * 8;
			break;
		}
	}

	// if the type wasn't found then bail
	if (offset == 0)
		return;

	// place the entity key/offset pair into a hash table key'd by the typeinfo pointer for caching
	std::pair<DWORD64, DWORD> p(key, offset);
	keyvault[typeinfo] = p;

	// Set the parameters for this type assign
	currTypeinfo = typeinfo;
	currKey = key;
	currOffset = offset;

}

// This function returns a vector list entity objects
std::vector<DWORD64>* EntityMgr::get()
{
	DWORD64 flink;
	DWORD64 ptr;
	// clear the entity list
	entitylist.clear();

	// if there exists a list
	if (currTypeinfo)
	{
		// grab the first pointer from the typeinfo
		flink = m_pMem->Start(currTypeinfo)->load(0x88);

		// while there exists a foward link
		while (flink)
		{
			// decode the forward link
			// and store the instance into the vector list
			// then load the next foward link
			ptr = decode_pointer(currKey, flink);
			entitylist.push_back(ptr - currOffset);
			flink = m_pMem->Start(ptr)->load(0);
		}
	}

	sort(entitylist.begin(), entitylist.end());
	entitylist.erase(unique(entitylist.begin(), entitylist.end()), entitylist.end());

	// Always return the instance vector list (empty or otherwise)
	return &entitylist;
}


EntityMgr::EntityMgr()
{
}


EntityMgr::~EntityMgr()
{
}
