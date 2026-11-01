/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Smoke test of Direct3D 11 under Wine (tools/wine/smoke_d3d11.sh): hardware device, flip-model swap chain
// like the engine's backend, clear and read-back of one pixel, and D3DCompile of every shader in a folder.
// Exit code 0 when device, swap chain and read-back work; shader compile failures are reported and counted
// in the last line.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	// Resolves #include "name" against the shader folder, like the backend does.
	class FolderInclude : public ID3DInclude
	{
	public:
		explicit FolderInclude(std::string dir) : m_dir(std::move(dir)) {}
		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID *data, UINT *bytes) override
		{
			FILE *f = std::fopen((m_dir + "\\" + name).c_str(), "rb");
			if (!f)
				return E_FAIL;
			std::fseek(f, 0, SEEK_END);
			long n = std::ftell(f);
			std::fseek(f, 0, SEEK_SET);
			char *buf = new char[n > 0 ? n : 1];
			*bytes = static_cast<UINT>(std::fread(buf, 1, n, f));
			std::fclose(f);
			*data = buf;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override { delete[] static_cast<const char *>(data); return S_OK; }
	private:
		std::string m_dir;
	};

	int compileShaders(const std::string &dir)
	{
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*.hlsl").c_str(), &fd);
		int failed = 0, total = 0;
		if (h == INVALID_HANDLE_VALUE)
		{
			std::printf("no shaders found in %s\n", dir.c_str());
			return 1;
		}
		FolderInclude inc(dir);
		do
		{
			const std::string name = fd.cFileName;
			const char *profile = name.find(".vs.") != std::string::npos ? "vs_4_0" : "ps_4_0";
			FILE *f = std::fopen((dir + "\\" + name).c_str(), "rb");
			if (!f)
				continue;
			std::fseek(f, 0, SEEK_END);
			long n = std::ftell(f);
			std::fseek(f, 0, SEEK_SET);
			std::vector<char> src(n);
			std::fread(src.data(), 1, n, f);
			std::fclose(f);
			// Without macros, and for shadow.vs.hlsl also the variants the backend compiles (Sh_Vertex_Shader).
			static const D3D_SHADER_MACRO none[] = { { nullptr, nullptr } };
			static const D3D_SHADER_MACRO uv[] = { { "SHADOW_UV", "1" }, { nullptr, nullptr } };
			static const D3D_SHADER_MACRO trees[] = { { "SHADOW_TREES", "1" }, { nullptr, nullptr } };
			static const D3D_SHADER_MACRO skin[] = { { "SHADOW_SKIN", "1" }, { nullptr, nullptr } };
			static const D3D_SHADER_MACRO inst[] = { { "SHADOW_INSTANCED", "1" }, { nullptr, nullptr } };
			static const D3D_SHADER_MACRO inst_uv[] = { { "SHADOW_UV", "1" }, { "SHADOW_INSTANCED", "1" }, { nullptr, nullptr } };
			const D3D_SHADER_MACRO *shadow_variants[] = { none, uv, trees, skin, inst, inst_uv };
			const D3D_SHADER_MACRO *plain_variants[] = { none };
			const bool shadow = name == "shadow.vs.hlsl";
			const D3D_SHADER_MACRO **variants = shadow ? shadow_variants : plain_variants;
			const int variant_count = shadow ? 6 : 1;
			for (int v = 0; v < variant_count; ++v)
			{
				ID3DBlob *code = nullptr, *err = nullptr;
				HRESULT hr = D3DCompile(src.data(), src.size(), name.c_str(), variants[v], &inc, "main", profile, 0, 0, &code, &err);
				++total;
				if (FAILED(hr))
				{
					++failed;
					std::printf("shader FAIL %s (%s, variant %d) 0x%08lX %s\n", name.c_str(), profile, v, static_cast<unsigned long>(hr),
						err ? static_cast<const char *>(err->GetBufferPointer()) : "");
				}
				if (code) code->Release();
				if (err) err->Release();
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
		std::printf("shaders: %d compiled, %d failed\n", total - failed, failed);
		return failed;
	}
}

static LRESULT CALLBACK wndProc(HWND w, UINT m, WPARAM a, LPARAM l) { return DefWindowProcA(w, m, a, l); }

int main(int argc, char **argv)
{
	WNDCLASSA wc = {};
	wc.lpfnWndProc = wndProc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "GzhSmoke";
	RegisterClassA(&wc);
	HWND hwnd = CreateWindowA("GzhSmoke", "d3d11 smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
	if (!hwnd) { std::printf("CreateWindow failed\n"); return 1; }

	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	D3D_FEATURE_LEVEL level = {}, want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 4, D3D11_SDK_VERSION, &dev, &level, &ctx);
	if (FAILED(hr)) { std::printf("D3D11CreateDevice failed 0x%08lX\n", static_cast<unsigned long>(hr)); return 1; }
	std::printf("device ok, feature level 0x%X\n", static_cast<unsigned>(level));
	// Direct3D 11.1 options the backend relies on (ClearView with rectangles, constant buffer offsets).
	D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
	if (SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))))
		std::printf("d3d11.1: ClearView %d ConstantBufferOffsetting %d ConstantBufferPartialUpdate %d MapNoOverwriteOnDynamicConstantBuffer %d\n",
			options.ClearView, options.ConstantBufferOffsetting, options.ConstantBufferPartialUpdate, options.MapNoOverwriteOnDynamicConstantBuffer);
	else
		std::printf("d3d11.1: options not available\n");

	IDXGIDevice *dxgiDev = nullptr;
	IDXGIAdapter *adapter = nullptr;
	IDXGIFactory2 *factory = nullptr;
	dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDev));
	dxgiDev->GetAdapter(&adapter);
	adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory));
	DXGI_ADAPTER_DESC ad = {};
	adapter->GetDesc(&ad);
	std::printf("adapter: %ls\n", ad.Description);

	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = 640; sd.Height = 360;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	IDXGISwapChain1 *sc = nullptr;
	hr = factory->CreateSwapChainForHwnd(dev, hwnd, &sd, nullptr, nullptr, &sc);
	if (FAILED(hr)) { std::printf("CreateSwapChainForHwnd failed 0x%08lX\n", static_cast<unsigned long>(hr)); return 1; }

	ID3D11Texture2D *back = nullptr;
	sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back));
	ID3D11RenderTargetView *rtv = nullptr;
	dev->CreateRenderTargetView(back, nullptr, &rtv);
	const float color[4] = { 0.0f, 0.5f, 1.0f, 1.0f };	// R G B A
	ctx->ClearRenderTargetView(rtv, color);

	D3D11_TEXTURE2D_DESC td = {};
	back->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	td.MiscFlags = 0;
	ID3D11Texture2D *stage = nullptr;
	hr = dev->CreateTexture2D(&td, nullptr, &stage);
	if (FAILED(hr)) { std::printf("staging texture failed 0x%08lX\n", static_cast<unsigned long>(hr)); return 1; }
	ctx->CopyResource(stage, back);
	D3D11_MAPPED_SUBRESOURCE mp = {};
	hr = ctx->Map(stage, 0, D3D11_MAP_READ, 0, &mp);
	if (FAILED(hr)) { std::printf("Map failed 0x%08lX\n", static_cast<unsigned long>(hr)); return 1; }
	const unsigned char *px = static_cast<const unsigned char *>(mp.pData) + 100 * mp.RowPitch + 100 * 4;	// B G R A
	std::printf("pixel (100,100): B=%u G=%u R=%u A=%u\n", px[0], px[1], px[2], px[3]);
	const bool pixelOk = px[0] == 255 && px[1] >= 127 && px[1] <= 128 && px[2] == 0;
	ctx->Unmap(stage, 0);
	hr = sc->Present(0, 0);
	std::printf("present 0x%08lX, read-back %s\n", static_cast<unsigned long>(hr), pixelOk ? "ok" : "WRONG");

	int shaderFailures = 0;
	if (argc > 1)
		shaderFailures = compileShaders(argv[1]);
	std::printf("smoke done, shader failures: %d\n", shaderFailures);
	return (pixelOk && SUCCEEDED(hr)) ? 0 : 1;
}
