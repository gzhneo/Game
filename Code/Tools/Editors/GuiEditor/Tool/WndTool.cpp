/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndTool.cpp -- window layouts (.wnd) on the command line, without the engine.
//
//   WndTool [--check] <file or folder>...   problems the game's reader would have; 1 if there are errors
//   WndTool --dump <file>...                the windows as a tree: type, name, rectangle, status
//   WndTool --roundtrip <file or folder>... read and written unchanged, every file must stay the same

#include "Model/WndCheck.h"
#include "Model/WndLayout.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

std::vector<std::filesystem::path> layoutFiles(const char *arg)
{
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	const std::filesystem::path p(arg);
	if (std::filesystem::is_directory(p, ec))
	{
		for (const auto &e : std::filesystem::recursive_directory_iterator(p, ec))
		{
			std::string ext = e.path().extension().string();
			for (char &c : ext)
				c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
			if (e.is_regular_file(ec) && ext == ".wnd")
				out.push_back(e.path());
		}
	}
	else
		out.push_back(p);
	return out;
}

bool readFile(const std::filesystem::path &path, std::string &text)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

void dump(const WndModel::Layout &layout)
{
	std::printf("FILE_VERSION %d, init %s, update %s, shutdown %s\n", layout.version(), layout.layoutCallback("LAYOUTINIT").c_str(),
		layout.layoutCallback("LAYOUTUPDATE").c_str(), layout.layoutCallback("LAYOUTSHUTDOWN").c_str());
	WndModel::forEachWindow(layout, [](const WndModel::Window &w, int depth) {
		const std::optional<WndModel::Rect> r = w.rect();
		std::string status;
		for (const std::string &s : w.status())
			status += (status.empty() ? "" : "+") + s;
		std::printf("%*s%s %s (%d,%d)-(%d,%d) %s\n", depth * 2, "", w.type().c_str(), w.name().c_str(), r ? r->left : 0,
			r ? r->top : 0, r ? r->right : 0, r ? r->bottom : 0, status.c_str());
	});
}

} // namespace

int main(int argc, char **argv)
{
	enum class Mode { Check, Dump, Roundtrip } mode = Mode::Check;
	int first = 1;
	if (argc > 1 && std::strcmp(argv[1], "--dump") == 0)
		mode = Mode::Dump, first = 2;
	else if (argc > 1 && std::strcmp(argv[1], "--roundtrip") == 0)
		mode = Mode::Roundtrip, first = 2;
	else if (argc > 1 && std::strcmp(argv[1], "--check") == 0)
		first = 2;
	if (first >= argc)
	{
		std::fprintf(stderr, "WndTool [--check | --dump | --roundtrip] <file or folder>...\n");
		return 2;
	}
	int files = 0, failed = 0, errors = 0, warnings = 0;
	for (int i = first; i < argc; ++i)
	{
		for (const std::filesystem::path &path : layoutFiles(argv[i]))
		{
			++files;
			std::string text;
			if (!readFile(path, text))
			{
				std::printf("%s: cannot be read\n", path.string().c_str());
				++failed;
				continue;
			}
			const WndModel::Layout layout = WndModel::read(text);
			if (mode == Mode::Dump)
			{
				std::printf("%s\n", path.string().c_str());
				dump(layout);
			}
			else if (mode == Mode::Roundtrip)
			{
				if (WndModel::write(layout) != text)
				{
					std::printf("%s: written differently\n", path.string().c_str());
					++failed;
				}
			}
			else
			{
				for (const WndModel::Problem &p : WndModel::check(layout))
				{
					const bool error = p.severity == WndModel::Problem::Severity::Error;
					(error ? errors : warnings)++;
					std::printf("%s: %s%s%s%s\n", path.string().c_str(), error ? "error: " : "warning: ", p.window.c_str(),
						p.window.empty() ? "" : ": ", p.text.c_str());
				}
			}
		}
	}
	if (mode == Mode::Check)
		std::printf("%d files, %d errors, %d warnings\n", files, errors, warnings);
	else if (mode == Mode::Roundtrip)
		std::printf("%d files, %d written differently\n", files, failed);
	return failed > 0 || errors > 0 ? 1 : 0;
}
