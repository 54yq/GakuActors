#pragma once

#include <string>

namespace GkmsResourceUpdate {
	void saveProgramConfig();
    bool CreateProgramConfigIfMissing();
	// Serialization and the file write are separate so a caller on the Unity
	// main thread can capture the mutable program settings and let the plugin
	// worker perform only the I/O.
	std::string SerializeProgramConfig();
	bool WriteProgramConfig(const std::string& content);

	std::string GetCurrentResourceVersion(bool useCache);
	std::string GetCurrentTextureVersion(bool useCache);

	// These capture the settings they need on the calling thread and run the
	// network/extract work on a plugin-owned task. They are called from the GUI
	// callback and from nothing else, so the capture always happens on the
	// thread that owns those values.
	void CheckUpdateFromAPI(bool isManual);
	void CheckTextureUpdateFromAPI(bool isManual);
	void checkUpdateFromURL(const std::string& downloadUrl);

    // Restores only proven interrupted resource transactions before readers
    // start. It does not migrate directories or import upstream user data.
    bool RecoverInterruptedUpdates();
    bool SelectResourceSet(bool texture, const std::string& source, const std::string& id);

	// Shutdown contract: refuse new tasks, cancel the running ones at their next
	// checkpoint, then wait a bounded time for them to leave. Long work
	// (download, extraction, copying) only writes a staging directory; a task
	// that is still running after the deadline is reported and detached, and
	// its later commit is refused because the closed state is serialized with
	// the live-tree commit gate. No task can therefore write installed
	// resources after BeginShutdown returns.
	void BeginShutdown();
	void WaitForJobs(unsigned timeoutMs);
}
