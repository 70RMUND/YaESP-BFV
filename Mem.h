#pragma once
#include <Windows.h>
#include "ObfuscationMgr.h"

typedef struct stringblob { char x[1024]; } stringblob;

class Mem
{
private:
	DWORD pid;
	HANDLE Handle;
	DWORD64 value;
public:

	inline DWORD64 val() { return this->value; }

	Mem()
	{
		HWND Hwnd = FindWindowA(NULL, "Battlefield™ V");
		if (!Hwnd) return;
		GetWindowThreadProcessId(Hwnd, &pid);
		Handle = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
		return;
	}

	~Mem()
	{
		if (Handle)
			CloseHandle(Handle);
	}

	__forceinline HANDLE GetHandle()
	{
		return Handle;
	}

	__forceinline Mem *Start(DWORD64 address)
	{
		this->value = address;
		return (this);
	}

	__forceinline Mem *Read(DWORD64 address)
	{
		this->value = address;
		return (this->r(0));
	}

	__forceinline Mem *r(DWORD64 ofs)
	{
		if (!this || !value)
			return this;
		TRY;
		if (!ReadProcessMemory(Handle, (void*)(value + ofs), &value, sizeof(DWORD64), 0))
			return this;
		TRY_END;

		return this;
	}

	__forceinline Mem *r_decrypt(DWORD64 ofs)
	{
		if (!this || !value)
			return this;

		DWORD64 key = value;

		TRY;
		if (!ReadProcessMemory(Handle, (void*)(value + ofs), &value, sizeof(DWORD64), 0))
			return this;
		TRY_END;

		ObfuscationMgr* OM = ObfuscationMgr::GetInstance();

		if ((value & 0x8000000000000000) == 0)
			return this;

		value = OM->DecryptPtr(value, key);

		return this;
	}

	template <typename T = DWORD64>
	__forceinline T load(DWORD64 ofs)
	{
		T buffer;
		memset(&buffer, 0, sizeof(buffer));
		TRY;
		ReadProcessMemory(Handle, (void*)(value + ofs), (LPVOID)(&buffer), sizeof(T), 0);
		TRY_END;
		return buffer;
	}

	template <typename T>
	__forceinline bool Get(DWORD64 ofs, LPVOID buffer)
	{
		if (Handle == 0 || !value)
			return false;

		TRY;
		if (!ReadProcessMemory(Handle, (void*)(value + ofs), (LPVOID)(buffer), sizeof(T), 0))
			return false;
		TRY_END;

		return true;
	}


	template <typename T>
	__forceinline bool Write(DWORD64 ofs, T buffer)
	{
		if (Handle == 0 || !value)
			return false;

		TRY;
		if (!WriteProcessMemory(Handle, (void*)(value + ofs), &buffer, sizeof(T), 0))
			return false;
		TRY_END;

		return true;
	}
};

extern Mem * m_pMem;