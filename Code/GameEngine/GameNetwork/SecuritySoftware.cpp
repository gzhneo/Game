/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SecuritySoftware.cpp -- see SecuritySoftware.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/SecuritySoftware.h"

#include <objbase.h>
#include <oleauto.h>
#include <netfw.h>
#include <wbemidl.h>

#include <cwchar>

namespace
{
	// COM entry points loaded at run time, so that no target needs another import library
	struct Com
	{
		HRESULT (WINAPI *initialize)(LPVOID, DWORD) = nullptr;
		void (WINAPI *uninitialize)() = nullptr;
		HRESULT (WINAPI *createInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *) = nullptr;
		HRESULT (WINAPI *setProxyBlanket)(IUnknown *, DWORD, DWORD, OLECHAR *, DWORD, DWORD, RPC_AUTH_IDENTITY_HANDLE, DWORD) = nullptr;
		BSTR (WINAPI *allocString)(const OLECHAR *) = nullptr;
		void (WINAPI *freeString)(BSTR) = nullptr;
		HRESULT (WINAPI *variantClear)(VARIANTARG *) = nullptr;

		bool load()
		{
			HMODULE ole32 = ::LoadLibraryA("ole32.dll");
			HMODULE oleaut32 = ::LoadLibraryA("oleaut32.dll");
			if (ole32 == nullptr || oleaut32 == nullptr)
				return false;
			initialize = reinterpret_cast<decltype(initialize)>(reinterpret_cast<void *>(::GetProcAddress(ole32, "CoInitializeEx")));
			uninitialize = reinterpret_cast<decltype(uninitialize)>(reinterpret_cast<void *>(::GetProcAddress(ole32, "CoUninitialize")));
			createInstance = reinterpret_cast<decltype(createInstance)>(reinterpret_cast<void *>(::GetProcAddress(ole32, "CoCreateInstance")));
			setProxyBlanket = reinterpret_cast<decltype(setProxyBlanket)>(reinterpret_cast<void *>(::GetProcAddress(ole32, "CoSetProxyBlanket")));
			allocString = reinterpret_cast<decltype(allocString)>(reinterpret_cast<void *>(::GetProcAddress(oleaut32, "SysAllocString")));
			freeString = reinterpret_cast<decltype(freeString)>(reinterpret_cast<void *>(::GetProcAddress(oleaut32, "SysFreeString")));
			variantClear = reinterpret_cast<decltype(variantClear)>(reinterpret_cast<void *>(::GetProcAddress(oleaut32, "VariantClear")));
			return initialize && uninitialize && createInstance && setProxyBlanket && allocString && freeString && variantClear;
		}
	};

	// The identifiers spelled out: no uuid import library needed
	const CLSID CLSID_FW_POLICY2 = { 0xE2B3C97F, 0x6AE1, 0x41AC, { 0x81, 0x7A, 0xF6, 0xF9, 0x21, 0x66, 0xD7, 0xDD } };
	const IID IID_FW_POLICY2 = { 0x98325047, 0xC671, 0x4174, { 0x8D, 0x81, 0xDE, 0xFC, 0xD3, 0xF0, 0x31, 0x86 } };
	const IID IID_FW_RULE = { 0xAF230D27, 0xBABA, 0x4E42, { 0xAC, 0xED, 0xF5, 0x24, 0xF2, 0x2C, 0xFC, 0xE2 } };
	const IID IID_ENUM_VARIANT = { 0x00020404, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
	const CLSID CLSID_WBEM_LOCATOR = { 0x4590F811, 0x1D3A, 0x11D0, { 0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24 } };
	const IID IID_WBEM_LOCATOR = { 0xDC12A687, 0x737F, 0x11CF, { 0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24 } };

	template <typename T>
	void release(T *&p)
	{
		if (p != nullptr)
			p->Release();
		p = nullptr;
	}

	std::string narrow(const wchar_t *text)
	{
		if (text == nullptr)
			return std::string();
		char buffer[512];
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr);
		return n > 0 ? std::string(buffer) : std::string();
	}

	const char *profileName(long profiles)
	{
		if (profiles & NET_FW_PROFILE2_PUBLIC)
			return "public";
		if (profiles & NET_FW_PROFILE2_PRIVATE)
			return "private";
		if (profiles & NET_FW_PROFILE2_DOMAIN)
			return "domain";
		return "unknown";
	}

	// IANA protocol numbers as the firewall rules hold them; 256 stands for any
	constexpr long PROTOCOL_TCP = 6;
	constexpr long PROTOCOL_UDP = 17;
	constexpr long PROTOCOL_ANY = 256;

	const char *protocolName(long protocol)
	{
		switch (protocol)
		{
			case PROTOCOL_UDP: return "UDP";
			case PROTOCOL_TCP: return "TCP";
			case PROTOCOL_ANY: return "any protocol";
			default: return "another protocol";
		}
	}

	bool samePath(const wchar_t *rulePath, const wchar_t *program)
	{
		if (rulePath == nullptr || program == nullptr)
			return false;
		wchar_t expanded[MAX_PATH];
		const DWORD n = ::ExpandEnvironmentStringsW(rulePath, expanded, MAX_PATH);
		return n > 0 && n <= MAX_PATH && ::lstrcmpiW(expanded, program) == 0;
	}

	void describeWindowsFirewall(const Com &com, std::vector<std::string> &out)
	{
		INetFwPolicy2 *policy = nullptr;
		if (FAILED(com.createInstance(CLSID_FW_POLICY2, nullptr, CLSCTX_INPROC_SERVER, IID_FW_POLICY2, reinterpret_cast<void **>(&policy)))
			|| policy == nullptr)
		{
			out.push_back("Windows Firewall: state not readable");
			return;
		}
		long profiles = 0;
		VARIANT_BOOL enabled = VARIANT_FALSE;
		VARIANT_BOOL blockAll = VARIANT_FALSE;
		if (SUCCEEDED(policy->get_CurrentProfileTypes(&profiles)))
		{
			const NET_FW_PROFILE_TYPE2 profile = (profiles & NET_FW_PROFILE2_PUBLIC) ? NET_FW_PROFILE2_PUBLIC
				: (profiles & NET_FW_PROFILE2_PRIVATE) ? NET_FW_PROFILE2_PRIVATE : NET_FW_PROFILE2_DOMAIN;
			policy->get_FirewallEnabled(profile, &enabled);
			policy->get_BlockAllInboundTraffic(profile, &blockAll);
			out.push_back(std::string("Windows Firewall: ") + (enabled ? "on" : "off") + " in the current network profile (" + profileName(profiles)
				+ ")" + (enabled && blockAll ? ", blocks all incoming connections" : ""));
		}

		wchar_t program[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, program, MAX_PATH);
		INetFwRules *rules = nullptr;
		IUnknown *enumerator = nullptr;
		IEnumVARIANT *items = nullptr;
		int found = 0;
		if (SUCCEEDED(policy->get_Rules(&rules)) && rules != nullptr && SUCCEEDED(rules->get__NewEnum(&enumerator)) && enumerator != nullptr
			&& SUCCEEDED(enumerator->QueryInterface(IID_ENUM_VARIANT, reinterpret_cast<void **>(&items))) && items != nullptr)
		{
			VARIANT item;
			ULONG fetched = 0;
			int examined = 0;
			while (examined++ < 5000 && items->Next(1, &item, &fetched) == S_OK && fetched == 1)
			{
				INetFwRule *rule = nullptr;
				if (item.vt == VT_DISPATCH && item.pdispVal != nullptr
					&& SUCCEEDED(item.pdispVal->QueryInterface(IID_FW_RULE, reinterpret_cast<void **>(&rule))) && rule != nullptr)
				{
					BSTR application = nullptr;
					if (SUCCEEDED(rule->get_ApplicationName(&application)) && samePath(application, program) && found < 8)
					{
						BSTR name = nullptr;
						NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_IN;
						NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
						VARIANT_BOOL ruleEnabled = VARIANT_FALSE;
						long protocol = PROTOCOL_ANY;
						long ruleProfiles = 0;
						rule->get_Name(&name);
						rule->get_Direction(&direction);
						rule->get_Action(&action);
						rule->get_Enabled(&ruleEnabled);
						rule->get_Protocol(&protocol);
						rule->get_Profiles(&ruleProfiles);
						if (direction == NET_FW_RULE_DIR_IN)
						{
							++found;
							const bool coversProfile = (ruleProfiles & profiles) != 0;
							out.push_back(std::string("Windows Firewall rule \"") + narrow(name) + "\": " + (action == NET_FW_ACTION_ALLOW ? "allows" : "BLOCKS")
								+ " incoming " + protocolName(protocol) + " for this program" + (ruleEnabled ? "" : " (switched off)")
								+ (coversProfile ? "" : " (not for the current profile)"));
						}
						com.freeString(name);
					}
					com.freeString(application);
					release(rule);
				}
				com.variantClear(&item);
			}
		}
		if (found == 0)
			out.push_back("Windows Firewall: no incoming rule for this program; with the firewall on, incoming traffic is dropped");
		release(items);
		release(enumerator);
		release(rules);
		release(policy);
	}

	void describeSecurityCenter(const Com &com, std::vector<std::string> &out)
	{
		IWbemLocator *locator = nullptr;
		IWbemServices *services = nullptr;
		if (FAILED(com.createInstance(CLSID_WBEM_LOCATOR, nullptr, CLSCTX_INPROC_SERVER, IID_WBEM_LOCATOR, reinterpret_cast<void **>(&locator)))
			|| locator == nullptr)
		{
			out.push_back("Security Center: not readable");
			return;
		}
		BSTR space = com.allocString(L"ROOT\\SecurityCenter2");
		const HRESULT connected = locator->ConnectServer(space, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
		com.freeString(space);
		if (FAILED(connected) || services == nullptr)
		{
			out.push_back("Security Center: not available (server edition or no access)");
			release(locator);
			return;
		}
		com.setProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
			RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
		static const struct { const wchar_t *query; const char *kind; } QUERIES[] = {
			{ L"SELECT displayName FROM FirewallProduct", "firewall" },
			{ L"SELECT displayName FROM AntiVirusProduct", "virus scanner" },
		};
		for (const auto &q : QUERIES)
		{
			BSTR language = com.allocString(L"WQL");
			BSTR query = com.allocString(q.query);
			IEnumWbemClassObject *results = nullptr;
			const HRESULT asked = services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &results);
			com.freeString(language);
			com.freeString(query);
			int listed = 0;
			while (SUCCEEDED(asked) && results != nullptr && listed < 8)
			{
				IWbemClassObject *product = nullptr;
				ULONG returned = 0;
				if (results->Next(2000, 1, &product, &returned) != WBEM_S_NO_ERROR || returned == 0 || product == nullptr)
					break;
				VARIANT name;
				::memset(&name, 0, sizeof(name));
				if (SUCCEEDED(product->Get(L"displayName", 0, &name, nullptr, nullptr)) && name.vt == VT_BSTR)
				{
					out.push_back(std::string("Security Center: ") + q.kind + " \"" + narrow(name.bstrVal) + "\"");
					++listed;
				}
				com.variantClear(&name);
				release(product);
			}
			if (listed == 0 && SUCCEEDED(asked))
				out.push_back(std::string("Security Center: no ") + q.kind + " registered besides Windows");
			release(results);
		}
		release(services);
		release(locator);
	}
}

std::vector<std::string> DescribeSecuritySoftware()
{
	std::vector<std::string> out;
	Com com;
	if (!com.load())
	{
		out.push_back("security software: COM not available");
		return out;
	}
	const HRESULT initialised = com.initialize(nullptr, COINIT_MULTITHREADED);
	describeWindowsFirewall(com, out);
	describeSecurityCenter(com, out);
	if (SUCCEEDED(initialised))
		com.uninitialize();
	return out;
}
