/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandxc.cpp -- see vulkandxc.h.
//
// The COM interfaces of dxcompiler.dll are declared here with only the methods used (in vtable
// order of dxcapi.h): the SDK header needs __declspec(uuid), which MinGW does not have, and the
// interfaces have been stable since DXC 1.6.

#include "vulkandxc.h"
#include "vulkanspirv.h"
#include "wwdebug.h"

#include <unknwn.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The interfaces need external linkage: in an anonymous namespace the compiler may assume it sees every
// implementation, finds none (they live in dxcompiler.dll) and treats the virtual calls as unreachable.
namespace dxc {

enum DXC_OUT_KIND
{
	DXC_OUT_NONE = 0,
	DXC_OUT_OBJECT = 1,
	DXC_OUT_ERRORS = 2,
	DXC_OUT_HLSL = 6,
	DXC_OUT_FORCE_DWORD = 0xFFFFFFFF
};

struct DxcBuffer
{
	LPCVOID	Ptr;
	SIZE_T	Size;
	UINT	Encoding;
};

struct IDxcBlob : public IUnknown
{
	virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
	virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};

struct IDxcBlobEncoding : public IDxcBlob
{
	virtual HRESULT STDMETHODCALLTYPE GetEncoding(BOOL* known, UINT32* code_page) = 0;
};

struct IDxcBlobUtf8 : public IDxcBlobEncoding
{
	virtual LPCSTR STDMETHODCALLTYPE GetStringPointer() = 0;
	virtual SIZE_T STDMETHODCALLTYPE GetStringLength() = 0;
};

struct IDxcIncludeHandler : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR file_name, IDxcBlob** source) = 0;
};

struct IDxcOperationResult : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetStatus(HRESULT* status) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetResult(IDxcBlob** result) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetErrorBuffer(IDxcBlobEncoding** errors) = 0;
};

struct IDxcResult : public IDxcOperationResult
{
	virtual BOOL STDMETHODCALLTYPE HasOutput(DXC_OUT_KIND kind) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOutput(DXC_OUT_KIND kind, REFIID iid, void** object, IUnknown** name) = 0;
	virtual UINT32 GetNumOutputs() = 0;
	virtual DXC_OUT_KIND GetOutputByIndex(UINT32 index) = 0;
	virtual DXC_OUT_KIND PrimaryOutput() = 0;
};

struct IDxcUtils : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateBlobFromBlob(IDxcBlob* blob, UINT32 offset, UINT32 length, IDxcBlob** result) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateBlobFromPinned(LPCVOID data, UINT32 size, UINT32 code_page, IDxcBlobEncoding** blob) = 0;
	virtual HRESULT STDMETHODCALLTYPE MoveToBlob(LPCVOID data, IMalloc* malloc, UINT32 size, UINT32 code_page, IDxcBlobEncoding** blob) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateBlob(LPCVOID data, UINT32 size, UINT32 code_page, IDxcBlobEncoding** blob) = 0;
};

struct IDxcCompiler3 : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Compile(const DxcBuffer* source, LPCWSTR* arguments, UINT32 count,
											  IDxcIncludeHandler* include, REFIID iid, LPVOID* result) = 0;
};

struct IDxcVersionInfo : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetVersion(UINT32* major, UINT32* minor) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFlags(UINT32* flags) = 0;
};

typedef HRESULT (__stdcall *DxcCreateInstanceProc)(REFCLSID clsid, REFIID iid, LPVOID* object);

}  // namespace dxc

namespace {

using namespace dxc;

const GUID CLSID_DxcCompiler    = { 0x73e22d93, 0xe6ce, 0x47f3, { 0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0 } };
const GUID CLSID_DxcUtils       = { 0x6245d6af, 0x66e0, 0x48fd, { 0x80, 0xb4, 0x4d, 0x27, 0x17, 0x96, 0x74, 0x8c } };
const GUID IID_IDxcCompiler3    = { 0x228b4687, 0x5a6a, 0x4730, { 0x90, 0x0c, 0x97, 0x02, 0xb2, 0x20, 0x3f, 0x54 } };
const GUID IID_IDxcUtils        = { 0x4605c4cb, 0x2019, 0x492a, { 0xad, 0xa4, 0x65, 0xf2, 0x0b, 0xb7, 0xd6, 0x7f } };
const GUID IID_IDxcResult       = { 0x58346cda, 0xdde7, 0x4497, { 0x94, 0x61, 0x6f, 0x87, 0xaf, 0x5e, 0x06, 0x59 } };
const GUID IID_IDxcBlob         = { 0x8ba5fb08, 0x5195, 0x40e2, { 0xac, 0x58, 0x0d, 0x98, 0x9c, 0x3a, 0x01, 0x02 } };
const GUID IID_IDxcBlobUtf8     = { 0x3da636c9, 0xba71, 0x4024, { 0xa3, 0x01, 0x30, 0xcb, 0xf1, 0x25, 0x30, 0x5b } };
const GUID IID_IDxcIncludeHandler = { 0x7f61fc7d, 0x950d, 0x467f, { 0xb3, 0xe3, 0x3c, 0x02, 0xfb, 0x49, 0x18, 0x7c } };
const GUID IID_IDxcVersionInfo  = { 0xb04f5b50, 0x2059, 0x4f12, { 0xa8, 0xff, 0xa1, 0xe0, 0xcd, 0xe1, 0xcc, 0x7e } };
// Own copy of IID_IUnknown, so the game needs no uuid.lib for it.
const GUID IID_Unknown          = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

/// Raised when the arguments change, so cached SPIR-V of older arguments is not used.
const UINT ARGUMENT_REVISION = 1;

HMODULE				Library = nullptr;
IDxcCompiler3*		Compiler = nullptr;
IDxcUtils*			Utils = nullptr;
UINT				Version = 0;
CRITICAL_SECTION	Lock;
bool				Lock_Ready = false;

template <class T> void Release(T*& p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

std::wstring Widen(const char* text)
{
	std::wstring result;
	if (!text) return result;
	while (*text) result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*text++)));
	return result;
}

/// Bridges DXC include requests to the engine's file system (GpuShaderInclude).
class IncludeBridge final : public IDxcIncludeHandler
{
public:
	/// main_name: the name the source was compiled under; DXC asks for includes relative to its folder.
	IncludeBridge(GpuShaderInclude* include, const char* main_name) : m_include(include), m_folder_length(0)
	{
		if (!main_name) return;
		for (size_t i = 0; main_name[i] && i + 1 < sizeof(m_folder); ++i) {
			m_folder[i] = main_name[i];
			if (main_name[i] == '/' || main_name[i] == '\\') m_folder_length = i + 1;
		}
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (!object) return E_POINTER;
		if (IsEqualGUID(iid, IID_Unknown) || IsEqualGUID(iid, IID_IDxcIncludeHandler)) {
			*object = this;
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	// Lives on the stack for one compilation.
	ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }

	HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR file_name, IDxcBlob** source) override
	{
		if (!source) return E_POINTER;
		*source = nullptr;
		if (!m_include || !file_name) return E_FAIL;
		// DXC asks relative to the including file: "./common.hlsli".
		char name[MAX_PATH];
		size_t n = 0;
		for (const wchar_t* c = file_name; *c && n + 1 < sizeof(name); ++c) name[n++] = static_cast<char>(*c);
		name[n] = 0;
		// Direct3D passes the name as written in #include; the engine's handler resolves it.
		const char* plain = name;
		while ((plain[0] == '.' && (plain[1] == '/' || plain[1] == '\\'))) plain += 2;
		if (m_folder_length && Same_Folder(plain)) plain += m_folder_length;
		const void* data = nullptr;
		UINT bytes = 0;
		if (FAILED(m_include->Open(plain, &data, &bytes)) || !data) return E_FAIL;
		IDxcBlobEncoding* blob = nullptr;
		const HRESULT hr = Utils->CreateBlob(data, bytes, 0, &blob);
		m_include->Close(data);
		if (FAILED(hr)) return hr;
		*source = blob;
		return S_OK;
	}

private:
	bool Same_Folder(const char* path) const
	{
		for (size_t i = 0; i < m_folder_length; ++i) {
			const char a = path[i], b = m_folder[i];
			if (!a) return false;
			const bool slash_a = a == '/' || a == '\\', slash_b = b == '/' || b == '\\';
			if (slash_a != slash_b || (!slash_a && tolower(static_cast<unsigned char>(a)) != tolower(static_cast<unsigned char>(b)))) return false;
		}
		return true;
	}

	GpuShaderInclude*	m_include;
	char				m_folder[MAX_PATH] = {};
	size_t				m_folder_length;
};

GpuBlob* Copy_Blob(const void* data, size_t size, bool terminate)
{
	GpuBlob* blob = Gpu_Create_Blob(size + (terminate ? 1 : 0));
	if (!blob) return nullptr;
	if (size) memcpy(blob->GetBufferPointer(), data, size);
	if (terminate) static_cast<char*>(blob->GetBufferPointer())[size] = 0;
	return blob;
}

/// "vs_4_0" -> L"vs_6_0"; empty for stages the device does not run.
std::wstring Target(const char* profile)
{
	if (!profile || strlen(profile) < 2) return std::wstring();
	const char a = profile[0], b = profile[1];
	if (b != 's' || (a != 'v' && a != 'p' && a != 'h' && a != 'd')) return std::wstring();
	std::wstring target;
	target.push_back(static_cast<wchar_t>(a));
	target += L"s_6_0";
	return target;
}

void Push(std::vector<std::wstring>& args, const wchar_t* text) { args.push_back(text); }

void Push_Common(std::vector<std::wstring>& args, const char* name, const GPU_SHADER_MACRO* macros)
{
	Push(args, L"-HV");
	Push(args, L"2018");
	for (const GPU_SHADER_MACRO* m = macros; m && m->Name; ++m) {
		Push(args, L"-D");
		std::wstring define = Widen(m->Name);
		if (m->Definition && *m->Definition) define += L"=" + Widen(m->Definition);
		args.push_back(define);
	}
	args.push_back(Widen(name && *name ? name : "shader.hlsl"));
}

/// Runs DXC; the result holds status, outputs and messages.
HRESULT Run(const char* source, SIZE_T length, const char* name, const std::vector<std::wstring>& args, GpuShaderInclude* include,
			IDxcResult** result)
{
	std::vector<LPCWSTR> pointers;
	for (const std::wstring& a : args) pointers.push_back(a.c_str());
	DxcBuffer buffer;
	buffer.Ptr = source;
	buffer.Size = length;
	buffer.Encoding = 0;
	IncludeBridge bridge(include, name);
	return Compiler->Compile(&buffer, pointers.data(), static_cast<UINT32>(pointers.size()), &bridge, IID_IDxcResult,
							 reinterpret_cast<LPVOID*>(result));
}

void Take_Errors(IDxcResult* result, GpuBlob** errors)
{
	if (!errors) return;
	*errors = nullptr;
	IDxcBlobUtf8* text = nullptr;
	if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_IDxcBlobUtf8, reinterpret_cast<void**>(&text), nullptr)) && text) {
		if (text->GetStringLength() > 0) *errors = Copy_Blob(text->GetStringPointer(), text->GetStringLength(), true);
		text->Release();
	}
}

class Locked
{
public:
	Locked() { EnterCriticalSection(&Lock); }
	~Locked() { LeaveCriticalSection(&Lock); }
};

}  // namespace

bool Dxc_Load(char* reason, unsigned reason_size)
{
	if (!Lock_Ready) {
		InitializeCriticalSection(&Lock);
		Lock_Ready = true;
	}
	Locked lock;
	if (Compiler) return true;
	// Only the copy next to the game: a dxcompiler.dll elsewhere in the search path can be any version.
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(nullptr, path, sizeof(path));
	while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') --n;
	if (n == 0 || n + 16 >= sizeof(path)) {
		if (reason && reason_size) std::snprintf(reason, reason_size, "game directory not found");
		return false;
	}
	strcpy(path + n, "dxcompiler.dll");
	Library = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!Library) {
		if (reason && reason_size) std::snprintf(reason, reason_size, "shader compiler dxcompiler.dll not found next to the game");
		return false;
	}
	DxcCreateInstanceProc create = reinterpret_cast<DxcCreateInstanceProc>(
		reinterpret_cast<void*>(GetProcAddress(Library, "DxcCreateInstance")));
	if (!create || FAILED(create(CLSID_DxcCompiler, IID_IDxcCompiler3, reinterpret_cast<LPVOID*>(&Compiler))) ||
		FAILED(create(CLSID_DxcUtils, IID_IDxcUtils, reinterpret_cast<LPVOID*>(&Utils)))) {
		Release(Compiler);
		Release(Utils);
		FreeLibrary(Library);
		Library = nullptr;
		if (reason && reason_size) std::snprintf(reason, reason_size, "dxcompiler.dll has no usable DxcCreateInstance");
		return false;
	}
	UINT32 major = 0, minor = 0;
	IDxcVersionInfo* info = nullptr;
	if (SUCCEEDED(Compiler->QueryInterface(IID_IDxcVersionInfo, reinterpret_cast<void**>(&info))) && info) {
		info->GetVersion(&major, &minor);
		info->Release();
	}
	Version = ((major & 0xFFu) << 24) | ((minor & 0xFFFFu) << 8) | (ARGUMENT_REVISION & 0xFFu);
	WWDEBUG_SAY(("Vulkan: shader compiler dxcompiler.dll %u.%u\n", major, minor));
	return true;
}

void Dxc_Unload()
{
	if (!Lock_Ready) return;
	Locked lock;
	Release(Compiler);
	Release(Utils);
	if (Library) FreeLibrary(Library);
	Library = nullptr;
}

UINT Dxc_Version() { return Version; }

HRESULT Dxc_Compile(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
					GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
					GpuBlob** code, GpuBlob** errors)
{
	if (code) *code = nullptr;
	if (errors) *errors = nullptr;
	if (!Lock_Ready || !code) return E_FAIL;
	const std::wstring target = Target(profile);
	if (target.empty()) return E_INVALIDARG;
	Locked lock;
	if (!Compiler) return E_FAIL;

	wchar_t shift[3][8];
	std::swprintf(shift[0], 8, L"%u", static_cast<unsigned>(SPIRV_BINDING_CB));
	std::swprintf(shift[1], 8, L"%u", static_cast<unsigned>(SPIRV_BINDING_SRV));
	std::swprintf(shift[2], 8, L"%u", static_cast<unsigned>(SPIRV_BINDING_SAMPLER));
	std::vector<std::wstring> args;
	Push(args, L"-spirv");
	Push(args, L"-fspv-target-env=vulkan1.3");
	Push(args, L"-fspv-reflect");				// semantics on stage inputs and outputs (vulkanspirv.h)
	Push(args, L"-fvk-use-dx-layout");			// constant buffers as rendercb.h mirrors them
	Push(args, L"-fvk-use-dx-position-w");		// SV_Position.w in pixel shaders as in Direct3D
	Push(args, L"-fvk-b-shift"); args.push_back(shift[0]); Push(args, L"all");
	Push(args, L"-fvk-t-shift"); args.push_back(shift[1]); Push(args, L"all");
	Push(args, L"-fvk-s-shift"); args.push_back(shift[2]); Push(args, L"all");
	Push(args, L"-T");
	args.push_back(target);
	Push(args, L"-E");
	args.push_back(Widen(entry && *entry ? entry : "main"));
	Push(args, (flags & GPU_COMPILE_OPTIMIZATION_LEVEL3) ? L"-O3" : L"-O1");
	Push_Common(args, name, macros);

	IDxcResult* result = nullptr;
	HRESULT hr = Run(source, length, name, args, include, &result);
	if (FAILED(hr) || !result) return FAILED(hr) ? hr : E_FAIL;
	HRESULT status = E_FAIL;
	result->GetStatus(&status);
	Take_Errors(result, errors);
	if (SUCCEEDED(status)) {
		IDxcBlob* object = nullptr;
		if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_IDxcBlob, reinterpret_cast<void**>(&object), nullptr)) && object) {
			*code = Copy_Blob(object->GetBufferPointer(), object->GetBufferSize(), false);
			object->Release();
		}
		if (!*code) status = E_FAIL;
	}
	result->Release();
	return status;
}

HRESULT Dxc_Preprocess(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
					   GpuShaderInclude* include, GpuBlob** text)
{
	if (text) *text = nullptr;
	if (!Lock_Ready || !text) return E_FAIL;
	Locked lock;
	if (!Compiler) return E_FAIL;
	std::vector<std::wstring> args;
	Push(args, L"-P");
	Push_Common(args, name, macros);
	IDxcResult* result = nullptr;
	HRESULT hr = Run(source, length, name, args, include, &result);
	if (FAILED(hr) || !result) return FAILED(hr) ? hr : E_FAIL;
	HRESULT status = E_FAIL;
	result->GetStatus(&status);
	if (SUCCEEDED(status)) {
		IDxcBlobUtf8* hlsl = nullptr;
		if (SUCCEEDED(result->GetOutput(DXC_OUT_HLSL, IID_IDxcBlobUtf8, reinterpret_cast<void**>(&hlsl), nullptr)) && hlsl) {
			*text = Copy_Blob(hlsl->GetStringPointer(), hlsl->GetStringLength(), true);
			hlsl->Release();
		}
		if (!*text) status = E_FAIL;
	}
	result->Release();
	return status;
}
