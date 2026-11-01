# Minimal test target without an external framework (math: Vector4, Matrix3D::Get_Inverse,
# BezierSegment; WAV parser, compression round trip, CRC). Built once for both games from the root
# CMakeLists.txt.
#
# BezierSegment.cpp/BezFwdIterator.cpp are compiled in directly instead of linking
# GameEngineCommon: that library is part of a monolith with cross references through
# GameClient/GameLogic/App/WinMain.cpp (Keyboard, Mouse, GameText, UserSettings, ...), so
# linking it would pull in almost the whole engine including WinMain globals. Both files only need
# PreRTS.h and WWMath (Coord3D/Vector4/Matrix4x4), no engine singletons.
# WavParser.cpp is compiled in directly for the same reason. It includes ForceOrder.h itself
# (see the comment there), so no /FI is needed here.
add_executable(UnitTests
	UnitTests.cpp
	UnitTestStubs.cpp
	${SHARED_GAMEENGINE_DIR}/Common/Bezier/BezierSegment.cpp
	${SHARED_GAMEENGINE_DIR}/Common/Bezier/BezFwdIterator.cpp
	${SHARED_PLATFORM_DIR}/XAudio2Device/WavParser.cpp
	# crc.cpp: the checksum the replay test uses. Compiled in directly like the files above; in
	# Release it contributes nothing because the header has an inline version there.
	${SHARED_GAMEENGINE_DIR}/Common/Replay/crc.cpp
)

target_include_directories(UnitTests PRIVATE
	${SHARED_GAMEENGINE_DIR}
	${SHARED_ENGINE_DIR}
	${SHARED_PLATFORM_DIR}
	${SHARED_CORE_DIR}
	${SHARED_RENDERER_DIR}
	${SHARED_CORE_DIR}/WWLib
	${SHARED_LIB_INCLUDE}
)
# Compression is a small separate target without PreRTS.h dependency, so it is linked directly.
# winmm: timeGetTime/timeBeginPeriod from WWLib (systimer/thread).
target_link_libraries(UnitTests PRIVATE WWMath WWLib WWDebug WWSaveLoad Compression winmm)

# /W4 /WX for the whole target: the engine sources compiled in are already clean in their libraries.
target_compile_options(UnitTests PRIVATE ${ENGINE_STRICT_OPTIONS} /WX)

add_test(NAME UnitTests COMMAND UnitTests)
