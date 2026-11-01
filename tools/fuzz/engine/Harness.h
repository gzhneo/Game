/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Common part of the engine harnesses (IniFuzz, MapFuzz, ...): the data stores of the game logic,
// crash and hang reporting, file helpers. Built with MinGW as a Debug configuration and run under
// Wine (tools/fuzz/engine/run.sh).

#pragma once

#include "HarnessHooks.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class SubsystemInterface;

namespace Harness
{

/// Creates the stores INI files and maps reach (objects and modules, weapons, armour, locomotors,
/// OCL, FX, particles, upgrades, sciences, special powers, AI, crates, terrain, roads, ...).
void createSubsystems();

/// Registers a store created by a harness itself; init() is called here, reset() by resetAll().
void addSubsystem( SubsystemInterface *subsystem );

template <typename T>
void create( T *&global, T *instance )
{
	global = instance;
	addSubsystem( instance );
}

void postProcessAll();

/// End of a game: the stores drop what the map brought.
void resetAll();

/// Loads <seed dir>/base/*.ini as the game data (INI_LOAD_OVERWRITE), then postProcessAll().
void loadBase( const std::filesystem::path &seedDir );

std::string readFile( const std::filesystem::path &p );
void writeFile( const std::filesystem::path &p, const std::string &data );

struct Rng
{
	std::uint64_t state;
	std::uint64_t next()
	{
		std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}
	std::size_t below( std::size_t n ) { return n ? static_cast<std::size_t>(next() % n) : 0; }
};

/// Dictionary: the engine is also built with -fsanitize-coverage=trace-cmp; every comparison with a
/// constant and every switch case records the constant (chunk versions, message types, limits).
/// dictionarySize() values so far; dictionaryValue() one of them at random.
std::size_t dictionarySize();
/// Empties the dictionary: called before the fuzzing loop, so it fills with the constants of the
/// readers under test, not of loading the base data.
void resetDictionary();
std::uint64_t dictionaryValue( Rng &rng );

/// A value for a 32-bit field: from the dictionary every second time, else from the fixed table.
template <std::size_t N>
std::uint32_t pickInt( Rng &rng, const std::uint32_t (&table)[N] )
{
	if ( dictionarySize() > 0 && rng.below( 2 ) == 0 )
		return static_cast<std::uint32_t>(dictionaryValue( rng ));
	return table[rng.below( N )];
}

/// Crash, abort and hang reporting. The input under test is always written to <work dir>/<current>;
/// a crash copies it to crash-<n>.<ext> (exit code 3), a hang of 20 s to hang-<n>.<ext> (exit code 4).
void installCrashHandlers( const char *current );
void startWatchdog();

/// Time limit of a fuzzing run: HARNESS_SECONDS=<n> ends the loop after n seconds even if the
/// requested number of inputs is not reached (night runs: a huge count plus a time limit).
bool timeUp();

/// Coverage feedback: the engine is built with -fsanitize-coverage=trace-pc; every basic block
/// counts the edge from the previous one. resetCoverage() before an input, newCoverage() after it:
/// true if the input reached an edge or a hit-count class (1, 2, 3, 4-7, 8-15, 16-31, 32-127, 128+)
/// no input reached before.
void resetCoverage();
bool newCoverage();
std::size_t coveredEdges();

extern std::filesystem::path workDir;		///< working directory of the harness
extern std::atomic<long long> iteration;	///< input number, for the names of kept inputs
extern std::atomic<long long> progress;		///< counted up after each input; the watchdog checks it

}	// namespace Harness
