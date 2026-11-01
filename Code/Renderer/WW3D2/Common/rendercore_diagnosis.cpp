/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
**	Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// rendercore_diagnosis.cpp -- upload diagnosis: content hashes of everything the core uploads, one line per upload
// and draw for chosen frames, and a fill byte for memory the core hands out unwritten. Two runs of the same scene
// can then be compared draw by draw; the first differing line names the writer.

#include "rendercore_internal.h"
#include <stdarg.h>
#include <stdint.h>
#include <unordered_map>

namespace {

bool		s_track = false;
int			s_fill = -1;
void		(*s_sink)(const char* line) = nullptr;
bool		s_detail = false;
unsigned	s_frame = 0;
unsigned	s_uploads = 0;
unsigned	s_draws = 0;
uint64_t	s_bytes = 0;
uint64_t	s_upload_hash = 0;
uint64_t	s_draw_hash = 0;
uint64_t	s_since_draw = 0;		///< uploads since the previous draw
unsigned	s_arg_count = 0, s_arg_start = 0;
int			s_arg_base = 0;
const char*	s_arg_kind = nullptr;

/// Content per resource: hash over its uploads since creation (a discarding upload starts it again).
std::unordered_map<const void*, uint64_t> s_content;

struct LockedLevel
{
	const unsigned char*	data;
	unsigned				pitch;
};
std::unordered_map<uint64_t, LockedLevel> s_locked;

const uint64_t HASH_SEED = 0xCBF29CE484222325ull;

uint64_t Hash(const void* data, size_t bytes, uint64_t h)
{
	const unsigned char* p = static_cast<const unsigned char*>(data);
	while (bytes >= 8) {
		uint64_t v;
		::memcpy(&v, p, 8);
		h = (h ^ v) * 0x100000001B3ull;
		h ^= h >> 29;
		p += 8;
		bytes -= 8;
	}
	while (bytes--) h = (h ^ *p++) * 0x100000001B3ull;
	return h;
}

uint64_t Mix(uint64_t h, uint64_t v)
{
	return Hash(&v, sizeof(v), h);
}

/// 32 bits are enough to compare two runs and keep the lines short.
unsigned Short(uint64_t h)
{
	return static_cast<unsigned>(h ^ (h >> 32));
}

void Line(const char* format, ...)
{
	char text[512];
	const int head = ::snprintf(text, sizeof(text), "UPLOADS %u ", s_frame);
	va_list args;
	va_start(args, format);
	::vsnprintf(text + head, sizeof(text) - head, format, args);
	va_end(args);
	s_sink(text);
}

uint64_t Content(const void* resource)
{
	if (!resource) return 0;
	const auto it = s_content.find(resource);
	return (it != s_content.end()) ? it->second : HASH_SEED;
}

uint64_t Level_Key(const void* texture, unsigned level)
{
	return Mix(reinterpret_cast<uintptr_t>(texture), level);
}

}

bool Ud_Tracking()
{
	return s_track;
}

int Ud_Fill()
{
	return s_fill;
}

void Ud_Created(const void* resource)
{
	if (s_track) s_content.erase(resource);
}

void Ud_Upload_Rows(const char* kind, const void* resource, unsigned offset, const void* data, unsigned row_bytes,
					unsigned rows, unsigned pitch, bool replace)
{
	if (!s_track || !data || !row_bytes || !rows) return;
	uint64_t h = HASH_SEED;
	for (unsigned r = 0; r < rows; ++r) h = Hash(static_cast<const unsigned char*>(data) + static_cast<size_t>(r) * pitch, row_bytes, h);
	uint64_t& content = s_content.emplace(resource, HASH_SEED).first->second;
	content = Mix(Mix(replace ? HASH_SEED : content, offset), h);
	if (!s_sink) return;
	++s_uploads;
	s_bytes += static_cast<uint64_t>(row_bytes) * rows;
	s_upload_hash = Mix(s_upload_hash, h);
	s_since_draw = Mix(s_since_draw, h);
	if (s_detail) {
		Line("U %u %s bytes=%u at=%u data=%08x content=%08x", s_uploads, kind, row_bytes * rows, offset, Short(h),
			 Short(content));
	}
}

void Ud_Upload(const char* kind, const void* resource, unsigned offset, const void* data, unsigned bytes, bool replace)
{
	Ud_Upload_Rows(kind, resource, offset, data, bytes, 1, bytes, replace);
}

void Ud_Copy(const char* kind, const void* dest, unsigned offset, const void* source)
{
	if (!s_track || !dest) return;
	const uint64_t from = Content(source);
	uint64_t& content = s_content.emplace(dest, HASH_SEED).first->second;
	content = Mix(Mix(content, offset), from);
	if (s_sink && s_detail) Line("C %s at=%u from=%08x content=%08x", kind, offset, Short(from), Short(content));
}

void Ud_Texture_Locked(const void* texture, unsigned level, const void* data, unsigned pitch)
{
	if (!s_track || !data) return;
	LockedLevel& l = s_locked[Level_Key(texture, level)];
	l.data = static_cast<const unsigned char*>(data);
	l.pitch = pitch;
}

void Ud_Texture_Unlocked(const char* kind, const void* texture, unsigned level, unsigned row_bytes, unsigned rows)
{
	if (!s_track) return;
	const auto it = s_locked.find(Level_Key(texture, level));
	if (it == s_locked.end()) return;
	Ud_Upload_Rows(kind, texture, level, it->second.data, row_bytes, rows, it->second.pitch, false);
	s_locked.erase(it);
}

void Ud_Draw_Args(const char* kind, unsigned count, unsigned start, int base)
{
	s_arg_kind = kind;
	s_arg_count = count;
	s_arg_start = start;
	s_arg_base = base;
}

void RenderCore::Ud_Draw()
{
	const char* kind = s_arg_kind ? s_arg_kind : "draw";
	const unsigned count = s_arg_count, start = s_arg_start;
	const int base = s_arg_base;
	s_arg_kind = nullptr;
	s_arg_count = s_arg_start = 0;
	s_arg_base = 0;
	if (!s_sink) return;

	++s_draws;
	unsigned tex[8];
	uint64_t textures = HASH_SEED;
	for (unsigned slot = 0; slot < 8; ++slot) {
		const WW3DTextureHandle t = M->textures[slot];
		// Render targets are written by the GPU; only their size is known here.
		tex[slot] = !t ? 0u : t->rtv ? Short(Mix(Mix(HASH_SEED, t->width), t->height)) | 1u : Short(Content(t));
		textures = Mix(textures, tex[slot]);
	}
	const uint64_t samplers = Hash(M->sampler_states, sizeof(M->sampler_states), HASH_SEED);
	const uint64_t states = Hash(M->stage_states, sizeof(M->stage_states), Hash(M->render_states, sizeof(M->render_states), HASH_SEED));
	const uint64_t vs = Hash(M->vs_constants, FFP_PIECE_VS_REGISTERS * 4 * sizeof(float), HASH_SEED);
	const uint64_t ps = Hash(M->ps_constants, FFP_PIECE_PS_REGISTERS * 4 * sizeof(float), HASH_SEED);
	const uint64_t object = Hash(&M->object_cb, sizeof(M->object_cb), HASH_SEED);
	const uint64_t frame = Hash(&M->frame_cb, sizeof(M->frame_cb), HASH_SEED);
	const uint64_t vb = Content(M->stream_vb);
	const uint64_t ib = Content(M->indices);

	uint64_t h = Mix(Mix(Mix(HASH_SEED, count), start), static_cast<uint64_t>(static_cast<int64_t>(base)));
	h = Mix(Mix(Mix(Mix(h, textures), samplers), states), Mix(Mix(vs, ps), Mix(object, frame)));
	h = Mix(Mix(Mix(h, vb), ib), Mix(M->stream_offset, M->stream_stride));
	h = Mix(h, s_since_draw);
	s_draw_hash = Mix(s_draw_hash, h);

	if (s_detail) {
		Line("D %u %s n=%u start=%u base=%d vb=%08x+%u/%u ib=%08x tex=%08x.%08x.%08x.%08x smp=%08x rs=%08x vs=%08x ps=%08x "
			 "obj=%08x frm=%08x up=%08x all=%08x",
			 s_draws, kind, count, start, base, Short(vb), M->stream_offset, M->stream_stride, Short(ib), tex[0], tex[1],
			 tex[2], tex[3], Short(samplers), Short(states), Short(vs), Short(ps), Short(object), Short(frame),
			 Short(s_since_draw), Short(h));
	}
	s_since_draw = HASH_SEED;
}

// --- RenderDevice: switches and frame log --------------------------------------------------------------------------

void RenderDevice::Set_Upload_Diagnosis(bool track, int fill)
{
	s_track = track;
	s_fill = (fill >= 0 && fill <= 255) ? fill : -1;
	if (!track) {
		s_content.clear();
		s_locked.clear();
	}
}

void RenderDevice::Begin_Upload_Log(void (*sink)(const char* line), unsigned frame, bool detail)
{
	if (!s_track) return;
	s_sink = sink;
	s_frame = frame;
	s_detail = detail;
	s_uploads = s_draws = 0;
	s_bytes = 0;
	s_upload_hash = s_draw_hash = s_since_draw = HASH_SEED;
}

void RenderDevice::End_Upload_Log()
{
	if (!s_sink) return;
	char text[256];
	::snprintf(text, sizeof(text), "UPLOADS %u SUMME uploads=%u bytes=%u upload=%08x draws=%u draw=%08x", s_frame, s_uploads,
			   static_cast<unsigned>(s_bytes), Short(s_upload_hash), s_draws, Short(s_draw_hash));
	s_sink(text);
	s_sink = nullptr;
}
