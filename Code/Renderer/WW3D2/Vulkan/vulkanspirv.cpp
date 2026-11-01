/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanspirv.cpp -- see vulkanspirv.h.

#include "vulkanspirv.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

// Opcodes, decorations and storage classes of the SPIR-V specification.
enum : uint32_t
{
	SPV_MAGIC = 0x07230203u,

	OP_EXTENSION = 10,
	OP_VARIABLE = 59,
	OP_DECORATE = 71,
	OP_DECORATE_ID = 332,
	OP_DECORATE_STRING = 5632,
	OP_MEMBER_DECORATE_STRING = 5633,

	DEC_BUILTIN = 11,
	DEC_LOCATION = 30,
	DEC_BINDING = 33,
	DEC_DESCRIPTOR_SET = 34,
	DEC_COUNTER_BUFFER = 5634,
	DEC_USER_SEMANTIC = 5635,
	DEC_USER_TYPE = 5636,

	STORAGE_UNIFORM_CONSTANT = 0,
	STORAGE_INPUT = 1,
	STORAGE_UNIFORM = 2,
	STORAGE_OUTPUT = 3,
};

/// The literal string of an instruction starting at word `first` (null terminated inside the words).
const char* Literal(const uint32_t* words, unsigned count, unsigned first)
{
	if (first >= count) return "";
	const char* text = reinterpret_cast<const char*>(words + first);
	const size_t bytes = (count - first) * sizeof(uint32_t);
	return (memchr(text, 0, bytes) != nullptr) ? text : "";
}

bool Is_Reflection_Extension(const char* name)
{
	return strcmp(name, "SPV_GOOGLE_hlsl_functionality1") == 0 || strcmp(name, "SPV_GOOGLE_user_type") == 0 ||
		   strcmp(name, "SPV_GOOGLE_decorate_string") == 0;
}

struct VariableInfo
{
	uint32_t	id;
	uint32_t	storage;
	uint32_t	location;
	uint32_t	binding;
	uint32_t	set;
	bool		has_location;
	bool		has_binding;
	bool		builtin;
	char		semantic[40];
};

VariableInfo* Find(std::vector<VariableInfo>& list, uint32_t id)
{
	for (VariableInfo& v : list) {
		if (v.id == id) return &v;
	}
	return nullptr;
}

void Fail(char* error, unsigned size, const char* text, unsigned value)
{
	if (error && size) std::snprintf(error, size, text, value);
}

}  // namespace

void Spirv_Split_Semantic(const char* text, char* name, unsigned name_size, unsigned* index)
{
	size_t length = strlen(text);
	size_t digits = length;
	while (digits > 0 && isdigit(static_cast<unsigned char>(text[digits - 1]))) --digits;
	unsigned value = 0;
	for (size_t i = digits; i < length; ++i) value = value * 10 + static_cast<unsigned>(text[i] - '0');
	if (index) *index = value;
	if (!name || name_size == 0) return;
	size_t n = (digits < name_size - 1) ? digits : name_size - 1;
	for (size_t i = 0; i < n; ++i) name[i] = static_cast<char>(toupper(static_cast<unsigned char>(text[i])));
	name[n] = 0;
}

bool Spirv_Prepare(std::vector<uint32_t>& words, unsigned set, SpirvReflection& reflection, char* error, unsigned error_size)
{
	memset(&reflection, 0, sizeof(reflection));
	if (error && error_size) error[0] = 0;
	if (words.size() < 5 || words[0] != SPV_MAGIC) {
		Fail(error, error_size, "not a SPIR-V module (%u words)", static_cast<unsigned>(words.size()));
		return false;
	}

	// Pass 1: variables and their decorations.
	std::vector<VariableInfo> vars;
	const unsigned count = static_cast<unsigned>(words.size());
	for (unsigned i = 5; i < count;) {
		const uint32_t op = words[i] & 0xFFFFu, n = words[i] >> 16;
		if (n == 0 || i + n > count) {
			Fail(error, error_size, "broken instruction at word %u", i);
			return false;
		}
		if (op == OP_VARIABLE && n >= 4) {
			VariableInfo v;
			memset(&v, 0, sizeof(v));
			v.id = words[i + 2];
			v.storage = words[i + 3];
			vars.push_back(v);
		}
		i += n;
	}
	for (unsigned i = 5; i < count;) {
		const uint32_t op = words[i] & 0xFFFFu, n = words[i] >> 16;
		if (op == OP_DECORATE && n >= 3) {
			VariableInfo* v = Find(vars, words[i + 1]);
			if (v) {
				const uint32_t dec = words[i + 2];
				if (dec == DEC_BUILTIN) v->builtin = true;
				else if (dec == DEC_LOCATION && n >= 4) { v->location = words[i + 3]; v->has_location = true; }
				else if (dec == DEC_BINDING && n >= 4) { v->binding = words[i + 3]; v->has_binding = true; }
				else if (dec == DEC_DESCRIPTOR_SET && n >= 4) v->set = words[i + 3];
			}
		}
		else if (op == OP_DECORATE_STRING && n >= 4 && words[i + 2] == DEC_USER_SEMANTIC) {
			VariableInfo* v = Find(vars, words[i + 1]);
			if (v) std::snprintf(v->semantic, sizeof(v->semantic), "%s", Literal(&words[i], n, 3));
		}
		i += n;
	}

	// Resources: class from the binding range, the set must be 0 as DXC places it.
	for (const VariableInfo& v : vars) {
		if (v.storage != STORAGE_UNIFORM_CONSTANT && v.storage != STORAGE_UNIFORM) continue;
		if (!v.has_binding) continue;
		if (v.set != 0) {
			Fail(error, error_size, "resource in descriptor set %u", v.set);
			return false;
		}
		if (v.binding < SPIRV_BINDING_SRV) reflection.cb_mask |= 1u << (v.binding - SPIRV_BINDING_CB);
		else if (v.binding < SPIRV_BINDING_SAMPLER) reflection.srv_mask |= 1u << (v.binding - SPIRV_BINDING_SRV);
		else if (v.binding < SPIRV_BINDING_END) reflection.sampler_mask |= 1u << (v.binding - SPIRV_BINDING_SAMPLER);
		else {
			Fail(error, error_size, "resource at binding %u (only b, t and s registers are supported)", v.binding);
			return false;
		}
	}

	// Stage inputs and outputs.
	for (const VariableInfo& v : vars) {
		if (v.storage != STORAGE_INPUT && v.storage != STORAGE_OUTPUT) continue;
		const bool input = v.storage == STORAGE_INPUT;
		unsigned& n = input ? reflection.input_count : reflection.output_count;
		if (n >= SPIRV_MAX_IO) {
			Fail(error, error_size, "more than %u stage inputs or outputs", SPIRV_MAX_IO);
			return false;
		}
		SpirvIo& io = input ? reflection.inputs[n] : reflection.outputs[n];
		Spirv_Split_Semantic(v.semantic, io.semantic, sizeof(io.semantic), &io.index);
		io.location = (v.builtin || !v.has_location) ? ~0u : v.location;
		io.id = v.id;
		++n;
	}

	// Pass 2: new set numbers, reflection decorations and extensions removed.
	std::vector<uint32_t> out;
	out.reserve(words.size());
	out.insert(out.end(), words.begin(), words.begin() + 5);
	for (unsigned i = 5; i < count;) {
		const uint32_t op = words[i] & 0xFFFFu, n = words[i] >> 16;
		bool keep = true;
		if (op == OP_EXTENSION && Is_Reflection_Extension(Literal(&words[i], n, 1))) keep = false;
		else if ((op == OP_DECORATE_STRING || op == OP_MEMBER_DECORATE_STRING) && n >= 3) {
			const uint32_t dec = words[i + (op == OP_DECORATE_STRING ? 2 : 3)];
			if (dec == DEC_USER_SEMANTIC || dec == DEC_USER_TYPE) keep = false;
		}
		else if (op == OP_DECORATE_ID && n >= 3 && words[i + 2] == DEC_COUNTER_BUFFER) keep = false;
		if (keep) {
			const size_t at = out.size();
			out.insert(out.end(), words.begin() + i, words.begin() + i + n);
			if (op == OP_DECORATE && n >= 4 && words[i + 2] == DEC_DESCRIPTOR_SET) out[at + 3] = set;
		}
		i += n;
	}
	words.swap(out);
	return true;
}

bool Spirv_Link_Inputs(std::vector<uint32_t>& words, const SpirvReflection& self, const SpirvReflection& previous)
{
	bool changed = false;
	const unsigned count = static_cast<unsigned>(words.size());
	for (unsigned k = 0; k < self.input_count; ++k) {
		const SpirvIo& in = self.inputs[k];
		if (in.location == ~0u) continue;
		unsigned wanted = ~0u;
		for (unsigned j = 0; j < previous.output_count; ++j) {
			const SpirvIo& out = previous.outputs[j];
			if (out.location != ~0u && out.index == in.index && strcmp(out.semantic, in.semantic) == 0) {
				wanted = out.location;
				break;
			}
		}
		if (wanted == ~0u || wanted == in.location) continue;
		for (unsigned i = 5; i < count;) {
			const uint32_t op = words[i] & 0xFFFFu, n = words[i] >> 16;
			if (n == 0) break;
			if (op == OP_DECORATE && n >= 4 && words[i + 1] == in.id && words[i + 2] == DEC_LOCATION) {
				words[i + 3] = wanted;
				changed = true;
			}
			i += n;
		}
	}
	return changed;
}
