if (SOLOUD_BUILD_TESTS)
	find_package (Threads REQUIRED)

	add_executable (playback_waveform ../tests/playback_waveform.cpp)
	target_link_libraries (playback_waveform ${TARGET_NAME} Threads::Threads)
	add_test (NAME playback_waveform COMMAND playback_waveform)
endif ()
