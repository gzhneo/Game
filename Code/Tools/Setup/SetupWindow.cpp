// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
// (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.

#include "SetupWindow.h"

#include "DataCopy.h"
#include "GameDataFinder.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

namespace gzhsetup
{

namespace
{

// User-visible texts; the source stays ASCII, umlauts as \u escapes.
struct Texts
{
	const wchar_t* title;
	const wchar_t* intro;
	const wchar_t* browse;
	const wchar_t* target;
	const wchar_t* space;
	const wchar_t* notFound;
	const wchar_t* unknownEdition;
	const wchar_t* start;
	const wchar_t* cancel;
	const wchar_t* close;
	const wchar_t* done;
	const wchar_t* failed;
	const wchar_t* cancelled;
	const wchar_t* wrongFolder;
	const wchar_t* askUnknown;
	const wchar_t* noSpace;
	const wchar_t* alreadySetUp;
	const wchar_t* choose;
	const wchar_t* copying;
	const wchar_t* copied;
};

const Texts GERMAN = {
	L"GZH Neo \u2013 Spieldaten einrichten",
	L"GZH Neo braucht die Spieldaten deiner eigenen Installation von Generals und Zero Hour (Steam, EA App, Origin oder "
	L"CD). Sie werden in den Ordner von GZH Neo kopiert; die Installation selbst bleibt unver\u00e4ndert.",
	L"Ordner w\u00e4hlen\u2026",
	L"Ziel:",
	L"Zu kopieren: %s, frei auf dem Laufwerk: %s",
	L"(nicht gefunden \u2013 bitte Ordner w\u00e4hlen)",
	L"unbekannte Fassung",
	L"Einrichten",
	L"Abbrechen",
	L"Schlie\u00dfen",
	L"Fertig: Generals und Zero Hour sind eingerichtet.",
	L"Fehler: ",
	L"Abgebrochen. Ein neuer Lauf setzt dort fort.",
	L"In diesem Ordner liegt keine Installation von %s.",
	L"Die gew\u00e4hlte Installation ist keine bekannte Fassung (erwartet: Generals 1.08, Zero Hour 1.04). Trotzdem einrichten?",
	L"Auf dem Ziellaufwerk ist zu wenig Platz frei.",
	L"Schon eingerichtet. \u201eEinrichten\u201c gleicht die Daten erneut ab.",
	L"Installation von %s w\u00e4hlen",
	L"Die Spieldaten werden kopiert. Ausf\u00fchrliches Protokoll: %s",
	L"%s kopiert.",
};

const Texts ENGLISH = {
	L"GZH Neo \u2013 Set up game data",
	L"GZH Neo needs the game data of your own installation of Generals and Zero Hour (Steam, EA app, Origin or disc). "
	L"It is copied into the folder of GZH Neo; the installation itself stays unchanged.",
	L"Choose folder\u2026",
	L"Target:",
	L"To copy: %s, free on the drive: %s",
	L"(not found \u2013 please choose the folder)",
	L"unknown release",
	L"Set up",
	L"Cancel",
	L"Close",
	L"Done: Generals and Zero Hour are set up.",
	L"Error: ",
	L"Cancelled. Running it again continues from there.",
	L"This folder holds no installation of %s.",
	L"The chosen installation is no known release (expected: Generals 1.08, Zero Hour 1.04). Set it up anyway?",
	L"Not enough free space on the target drive.",
	L"Already set up. \u201cSet up\u201d checks the data again.",
	L"Choose the installation of %s",
	L"Copying the game data. Detailed log: %s",
	L"%s copied.",
};

enum : UINT
{
	MSG_LOG = WM_APP + 1,
	MSG_PROGRESS,
	MSG_DONE,
};

enum : int
{
	ID_COMBO_GENERALS = 101,
	ID_COMBO_ZERO_HOUR,
	ID_BROWSE_GENERALS,
	ID_BROWSE_ZERO_HOUR,
	ID_START,
	ID_CANCEL,
};

enum DoneCode : WPARAM
{
	DONE_OK = 0,
	DONE_ERROR = 1,
	DONE_CANCELLED = 2,
};

std::wstring widen(const std::string& utf8)
{
	if (utf8.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring text(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text.data(), length);
	return text;
}

std::wstring formatSize(std::uint64_t bytes)
{
	wchar_t buffer[32];
	if (bytes < (1ull << 30))
		std::swprintf(buffer, 32, L"%llu MB", static_cast<unsigned long long>((bytes + (1ull << 19)) >> 20));
	else
		std::swprintf(buffer, 32, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
	return buffer;
}

std::wstring formatText(const wchar_t* pattern, const std::wstring& a, const std::wstring& b = std::wstring())
{
	wchar_t buffer[512];
	std::swprintf(buffer, 512, pattern, a.c_str(), b.c_str());
	return buffer;
}

class SetupWindow
{
public:
	SetupWindow(const fs::path& target, const Texts& texts) : m_target(target), m_texts(texts) {}

	int run()
	{
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass = {};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = &SetupWindow::windowProc;
		windowClass.hInstance = instance;
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
		windowClass.lpszClassName = L"GZHNeoEinrichtung";
		RegisterClassExW(&windowClass);

		m_scale = static_cast<double>(GetDpiForSystem()) / 96.0;
		RECT frame = { 0, 0, px(760), px(470) };
		const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		AdjustWindowRect(&frame, style, FALSE);
		m_window = CreateWindowExW(0, windowClass.lpszClassName, m_texts.title, style, CW_USEDEFAULT, CW_USEDEFAULT,
			frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, this);
		if (!m_window)
			return DONE_ERROR;
		createControls();
		detect();
		ShowWindow(m_window, SW_SHOWNORMAL);

		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			if (!IsDialogMessageW(m_window, &message))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		if (m_font)
			DeleteObject(m_font);
		return m_result;
	}

private:
	int px(int value) const { return static_cast<int>(value * m_scale + 0.5); }

	HWND control(const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0)
	{
		const HWND handle = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(width), px(height),
			m_window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
		return handle;
	}

	void createControls()
	{
		NONCLIENTMETRICSW metrics = {};
		metrics.cbSize = sizeof(metrics);
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
		m_font = CreateFontIndirectW(&metrics.lfMessageFont);

		control(L"STATIC", m_texts.intro, 0, 12, 12, 736, 48);
		control(L"STATIC", L"Generals:", 0, 12, 75, 90, 20);
		m_comboGenerals = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 105, 71, 515, 200, ID_COMBO_GENERALS);
		control(L"BUTTON", m_texts.browse, BS_PUSHBUTTON | WS_TABSTOP, 628, 70, 120, 26, ID_BROWSE_GENERALS);
		control(L"STATIC", L"Zero Hour:", 0, 12, 109, 90, 20);
		m_comboZeroHour = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 105, 105, 515, 200, ID_COMBO_ZERO_HOUR);
		control(L"BUTTON", m_texts.browse, BS_PUSHBUTTON | WS_TABSTOP, 628, 104, 120, 26, ID_BROWSE_ZERO_HOUR);
		control(L"STATIC", m_texts.target, 0, 12, 143, 90, 20);
		control(L"STATIC", m_target.wstring().c_str(), SS_PATHELLIPSIS, 105, 143, 643, 20);
		m_spaceText = control(L"STATIC", L"", 0, 105, 167, 643, 20);
		m_progress = control(PROGRESS_CLASSW, L"", 0, 12, 197, 736, 20);
		SendMessageW(m_progress, PBM_SETRANGE32, 0, 1000);
		m_log = control(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 12, 227, 736, 192);
		m_startButton = control(L"BUTTON", m_texts.start, BS_DEFPUSHBUTTON | WS_TABSTOP, 512, 432, 115, 28, ID_START);
		m_cancelButton = control(L"BUTTON", m_texts.close, BS_PUSHBUTTON | WS_TABSTOP, 633, 432, 115, 28, ID_CANCEL);
	}

	void appendLog(const std::wstring& line)
	{
		const LRESULT length = SendMessageW(m_log, WM_GETTEXTLENGTH, 0, 0);
		SendMessageW(m_log, EM_SETSEL, static_cast<WPARAM>(length), length);
		const std::wstring text = (length > 0 ? L"\r\n" : L"") + line;
		SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
	}

	std::wstring describe(const GameFolder& folder) const
	{
		std::wstring text = widen(folder.language.empty() ? std::string("?") : folder.language) + L"  \u00b7  ";
		text += folder.edition.empty() ? std::wstring(m_texts.unknownEdition) : widen(folder.edition);
		text += L"  \u00b7  " + widen(folder.origin) + L"  \u00b7  " + folder.path.wstring();
		return text;
	}

	void fillCombo(HWND combo, const std::vector<GameFolder>& folders, int selected)
	{
		SendMessageW(combo, CB_RESETCONTENT, 0, 0);
		if (folders.empty())
		{
			SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m_texts.notFound));
			SendMessageW(combo, CB_SETCURSEL, 0, 0);
			EnableWindow(combo, FALSE);
			return;
		}
		for (const GameFolder& folder : folders)
			SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(describe(folder).c_str()));
		SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
		EnableWindow(combo, TRUE);
	}

	static int indexOf(const std::vector<GameFolder>& folders, const std::optional<GameFolder>& folder)
	{
		for (size_t i = 0; folder && i < folders.size(); ++i)
		{
			if (folders[i].path == folder->path)
				return static_cast<int>(i);
		}
		return 0;
	}

	void detect()
	{
		const Detection detection = chooseGameFolders(findGameFolders(collectSystemCandidates()));
		for (const GameFolder& folder : detection.found)
			(folder.game == Game::ZeroHour ? m_zeroHourFolders : m_generalsFolders).push_back(folder);
		fillCombo(m_comboGenerals, m_generalsFolders, indexOf(m_generalsFolders, detection.generals));
		fillCombo(m_comboZeroHour, m_zeroHourFolders, indexOf(m_zeroHourFolders, detection.zeroHour));
		std::error_code ec;
		if (fs::exists(m_target / "Generals" / MANIFEST_NAME, ec) && fs::exists(m_target / "ZeroHour" / MANIFEST_NAME, ec))
		{
			appendLog(m_texts.alreadySetUp);
			// Closing without a new run leaves a complete setup: that counts as success.
			m_result = DONE_OK;
		}
		updateEstimate();
	}

	const GameFolder* selected(HWND combo, const std::vector<GameFolder>& folders) const
	{
		const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
		if (folders.empty() || index < 0 || static_cast<size_t>(index) >= folders.size())
			return nullptr;
		return &folders[static_cast<size_t>(index)];
	}

	void updateEstimate()
	{
		const GameFolder* generals = selected(m_comboGenerals, m_generalsFolders);
		const GameFolder* zeroHour = selected(m_comboZeroHour, m_zeroHourFolders);
		bool enough = false;
		if (generals && zeroHour)
		{
			const CopyEstimate estimate = estimateCopy(*generals, *zeroHour, m_target);
			m_totalBytes = estimate.totalBytes;
			ULARGE_INTEGER freeBytes = {};
			fs::path existing = m_target;
			std::error_code ec;
			while (!existing.empty() && !fs::exists(existing, ec) && existing.has_parent_path() && existing.parent_path() != existing)
				existing = existing.parent_path();
			GetDiskFreeSpaceExW(existing.c_str(), &freeBytes, nullptr, nullptr);
			// Room for the data plus a little for the loose files and the manifest.
			enough = freeBytes.QuadPart > estimate.missingBytes + (64ull << 20);
			std::wstring text = formatText(m_texts.space, formatSize(estimate.missingBytes), formatSize(freeBytes.QuadPart));
			if (!enough)
				text += L"  \u2013  " + std::wstring(m_texts.noSpace);
			SetWindowTextW(m_spaceText, text.c_str());
		}
		else
			SetWindowTextW(m_spaceText, L"");
		EnableWindow(m_startButton, generals && zeroHour && enough && !m_running);
	}

	void browse(Game game)
	{
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return;
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		const std::wstring title = formatText(m_texts.choose, widen(gameName(game)));
		dialog->SetTitle(title.c_str());
		fs::path chosen;
		if (SUCCEEDED(dialog->Show(m_window)))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					chosen = path;
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		dialog->Release();
		if (chosen.empty())
			return;

		std::vector<GameFolder>& folders = game == Game::ZeroHour ? m_zeroHourFolders : m_generalsFolders;
		const HWND combo = game == Game::ZeroHour ? m_comboZeroHour : m_comboGenerals;
		std::optional<GameFolder> best;
		for (const GameFolder& folder : chooseGameFolders(findGameFolders({ { chosen, "manual" } })).found)
		{
			if (folder.game == game && (!best || (best->baseFolderOfZeroHour && !folder.baseFolderOfZeroHour)))
				best = folder;
		}
		if (!best)
		{
			const std::wstring text = formatText(m_texts.wrongFolder, widen(gameName(game)));
			MessageBoxW(m_window, text.c_str(), m_texts.title, MB_OK | MB_ICONWARNING);
			return;
		}
		int index = -1;
		for (size_t i = 0; i < folders.size(); ++i)
		{
			if (folders[i].path == best->path)
				index = static_cast<int>(i);
		}
		if (index < 0)
		{
			folders.push_back(*best);
			index = static_cast<int>(folders.size()) - 1;
		}
		fillCombo(combo, folders, index);
		updateEstimate();
	}

	void setRunning(bool running)
	{
		m_running = running;
		for (const HWND handle : { m_comboGenerals, m_comboZeroHour, GetDlgItem(m_window, ID_BROWSE_GENERALS),
				 GetDlgItem(m_window, ID_BROWSE_ZERO_HOUR) })
		{
			EnableWindow(handle, !running);
		}
		if (!running)
		{
			EnableWindow(m_comboGenerals, !m_generalsFolders.empty());
			EnableWindow(m_comboZeroHour, !m_zeroHourFolders.empty());
		}
		SetWindowTextW(m_cancelButton, running ? m_texts.cancel : m_texts.close);
		updateEstimate();
	}

	void start()
	{
		const GameFolder* generals = selected(m_comboGenerals, m_generalsFolders);
		const GameFolder* zeroHour = selected(m_comboZeroHour, m_zeroHourFolders);
		if (!generals || !zeroHour || m_running)
			return;
		if ((generals->edition.empty() || zeroHour->edition.empty())
			&& MessageBoxW(m_window, m_texts.askUnknown, m_texts.title, MB_YESNO | MB_ICONQUESTION) != IDYES)
		{
			return;
		}
		if (m_worker.joinable())
			m_worker.join();
		m_cancelRequested = false;
		setRunning(true);
		SendMessageW(m_progress, PBM_SETPOS, 0, 0);
		const HWND window = m_window;
		const std::uint64_t total = m_totalBytes;
		m_worker = std::thread([this, window, total, generalsFolder = *generals, zeroHourFolder = *zeroHour, target = m_target]() {
			const auto post = [window](const std::wstring& line) {
				PostMessageW(window, MSG_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(line)));
			};
			const fs::path logPath = target / "GZHNeoEinrichtung.log";
			std::error_code ec;
			fs::create_directories(target, ec);
			std::ofstream logFile(logPath, std::ios::trunc);
			post(formatText(m_texts.copying, logPath.wstring()));
			WPARAM code = DONE_OK;
			try
			{
				const CopyReport report = setUpGameData(generalsFolder, zeroHourFolder, target,
					[&logFile](const std::string& line) { logFile << line << '\n'; },
					[this, window, total](std::uint64_t done) {
						const WPARAM permille = total ? static_cast<WPARAM>(std::min<std::uint64_t>(1000, done * 1000 / total)) : 0;
						PostMessageW(window, MSG_PROGRESS, permille, 0);
						return !m_cancelRequested.load();
					});
				logFile << "done: " << report.filesCopied << " files copied, " << report.bytesCopied << " bytes, "
						<< report.filesLinked << " linked\n";
				post(formatText(m_texts.copied, formatSize(report.bytesCopied)));
			}
			catch (const CopyCancelled&)
			{
				logFile << "cancelled\n";
				code = DONE_CANCELLED;
			}
			catch (const std::exception& e)
			{
				logFile << "error: " << e.what() << '\n';
				post(std::wstring(m_texts.failed) + widen(e.what()));
				code = DONE_ERROR;
			}
			PostMessageW(window, MSG_DONE, code, 0);
		});
	}

	void finished(WPARAM code)
	{
		if (m_worker.joinable())
			m_worker.join();
		if (code == DONE_OK)
		{
			SendMessageW(m_progress, PBM_SETPOS, 1000, 0);
			appendLog(m_texts.done);
			m_result = DONE_OK;
		}
		else if (code == DONE_CANCELLED)
		{
			appendLog(m_texts.cancelled);
			m_result = DONE_CANCELLED;
		}
		else
			m_result = DONE_ERROR;
		setRunning(false);
		if (m_closeWhenDone)
			DestroyWindow(m_window);
		else if (code == DONE_OK)
			MessageBoxW(m_window, m_texts.done, m_texts.title, MB_OK | MB_ICONINFORMATION);
	}

	LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case ID_COMBO_GENERALS:
			case ID_COMBO_ZERO_HOUR:
				if (HIWORD(wParam) == CBN_SELCHANGE)
					updateEstimate();
				return 0;
			case ID_BROWSE_GENERALS:
				browse(Game::Generals);
				return 0;
			case ID_BROWSE_ZERO_HOUR:
				browse(Game::ZeroHour);
				return 0;
			case ID_START:
				start();
				return 0;
			case ID_CANCEL:
			case IDCANCEL:
				if (m_running)
					m_cancelRequested = true;
				else
					DestroyWindow(m_window);
				return 0;
			default:
				break;
			}
			break;
		case WM_CLOSE:
			// Closing during the copy cancels it and closes once the worker has stopped.
			if (m_running)
			{
				m_cancelRequested = true;
				m_closeWhenDone = true;
			}
			else
				DestroyWindow(m_window);
			return 0;
		case MSG_LOG:
		{
			const std::wstring* line = reinterpret_cast<const std::wstring*>(lParam);
			appendLog(*line);
			delete line;
			return 0;
		}
		case MSG_PROGRESS:
			SendMessageW(m_progress, PBM_SETPOS, wParam, 0);
			return 0;
		case MSG_DONE:
			finished(wParam);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(m_window, message, wParam, lParam);
	}

	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_NCCREATE)
		{
			auto* self = static_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			self->m_window = window;
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		auto* self = reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		return self ? self->handle(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
	}

	fs::path m_target;
	const Texts& m_texts;
	double m_scale = 1.0;
	HWND m_window = nullptr;
	HFONT m_font = nullptr;
	HWND m_comboGenerals = nullptr;
	HWND m_comboZeroHour = nullptr;
	HWND m_spaceText = nullptr;
	HWND m_progress = nullptr;
	HWND m_log = nullptr;
	HWND m_startButton = nullptr;
	HWND m_cancelButton = nullptr;
	std::vector<GameFolder> m_generalsFolders;
	std::vector<GameFolder> m_zeroHourFolders;
	std::uint64_t m_totalBytes = 0;
	std::thread m_worker;
	std::atomic<bool> m_cancelRequested = false;
	bool m_running = false;
	bool m_closeWhenDone = false;
	int m_result = static_cast<int>(DONE_ERROR);
};

} // namespace

int runSetupWindow(const fs::path& target)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
	INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&controls);
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool german = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
	SetupWindow window(target, german ? GERMAN : ENGLISH);
	const int result = window.run();
	if (SUCCEEDED(com))
		CoUninitialize();
	return result;
}

} // namespace gzhsetup
