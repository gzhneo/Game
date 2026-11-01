# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# gzh_machine_path(<out-var> <NAME>): value of a machine path (GZH_ASSETS_GENERALS, ...). The environment variable
# wins, then the machine file (same file as tools/pfade.py: %LOCALAPPDATA%/GZHNeo/pfade.json, otherwise
# $XDG_CONFIG_HOME or ~/.config/gzhneo/pfade.json); empty if neither is set.

function(gzh_machine_path out name)
	set(_value "$ENV{${name}}")
	if(NOT _value)
		if(WIN32)
			set(_base "$ENV{LOCALAPPDATA}")
			set(_file "${_base}/GZHNeo/pfade.json")
		elseif(DEFINED ENV{XDG_CONFIG_HOME})
			set(_file "$ENV{XDG_CONFIG_HOME}/gzhneo/pfade.json")
		else()
			set(_file "$ENV{HOME}/.config/gzhneo/pfade.json")
		endif()
		if(EXISTS "${_file}")
			file(READ "${_file}" _json)
			string(JSON _value ERROR_VARIABLE _err GET "${_json}" "${name}")
			if(_err)
				set(_value "")
			endif()
		endif()
	endif()
	set(${out} "${_value}" PARENT_SCOPE)
endfunction()
