#pragma once
#include <Windows.h>
#include <winternl.h>


typedef struct _THREAD_BASIC_INFORMATION { 
typedef DWORD KPRIORITY; 
NTSTATUS ExitStatus; 
PVOID TebBaseAddress; 
CLIENT_ID ClientId; 
KAFFINITY AffinityMask; 
KPRIORITY Priority; 
KPRIORITY BasePriority; } THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;
typedef NTSTATUS(*__stdcall tNtQueryInformationThread)(HANDLE,THREADINFOCLASS,PVOID,ULONG,PULONG);

class StackAccess
{
private:
	HANDLE h_thread;
	DWORD64 teb_base;
	DWORD stack_start;
	DWORD stack_end;
	DWORD stack_size;
	void* buffer;
public:
	StackAccess(DWORD threadid);
	~StackAccess();
	void* read();
	DWORD size();
};

