#include "pch.h"
#include "StackAccess.h"
#include "Mem.h"



StackAccess::StackAccess(DWORD threadid)
{
	THREAD_BASIC_INFORMATION tbi;
	tNtQueryInformationThread NtQueryInformationThread;

	buffer = NULL;
	
	h_thread = OpenThread(THREAD_ALL_ACCESS, 0, threadid);
	if (h_thread == NULL)
		return;

	NtQueryInformationThread = (tNtQueryInformationThread)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "NtQueryInformationThread");
	if (NtQueryInformationThread == NULL)
		return;
	
	
	
	NTSTATUS result = NtQueryInformationThread(h_thread, (THREADINFOCLASS)0, &tbi, sizeof(THREAD_BASIC_INFORMATION), NULL);
	CloseHandle(h_thread);

	if (result == 0) // Status Success
	{
		teb_base = (DWORD64)tbi.TebBaseAddress;
		m_pMem->Start(teb_base)->Get<DWORD>(0x8, &stack_start);
		m_pMem->Start(teb_base)->Get<DWORD>(0x10, &stack_end);
		stack_size = stack_start - stack_end;
		buffer = malloc(stack_size);
	}
}

DWORD StackAccess::size()
{
	return stack_size;
}

void* StackAccess::read()
{
	HANDLE h = m_pMem->GetHandle();
	auto res = ReadProcessMemory(h, (LPCVOID)stack_end, buffer, stack_size, NULL);
	if (res == 0)
		return NULL;
	return buffer;
}

StackAccess::~StackAccess()
{
	if (buffer != NULL)
		delete buffer;
}
