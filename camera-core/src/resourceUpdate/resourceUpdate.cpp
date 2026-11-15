#include "stdinclude.hpp"
#include "cpprest/http_client.h"
#include "cpprest/filestream.h"
#include "nlohmann/json.hpp"
#include "GakumasLocalify/Log.h"
#include "gkmsGUI/GUII18n.hpp"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>
#include "unzip.hpp"
#include "ResourceCommit.hpp"
#include "UpdateJob.hpp"
#include "ResourceInstall.hpp"
#include "data/LocalifyData.hpp"
#include "LayeredUpdate.hpp"
#include "UpdateProgress.hpp"
#include "VersionCache.hpp"

extern std::filesystem::path gakumasLocalPath;
extern std::filesystem::path ProgramConfigJson;
extern std::atomic_bool downloading;
extern std::function<void()> g_reload_all_data;

namespace GkmsResourceUpdate {
	std::atomic_bool updateJobRunning = false;

	namespace {
	// The version caches are read by the panel every frame and rewritten by a
	// resource update task, so they need the same guard as any shared mutable
	// state.
	std::mutex g_versionMutex;
    VersionCache resourceVersionCache, textureVersionCache;

	// Resource updates used to be detached: shutdown could not refuse, cancel or
	// wait for them, so a download or extraction could keep rewriting the
	// resource tree and post a Reload into a queue that no longer had a reader.
	// The plugin now owns every task, cancels it at a checkpoint and joins it
	// with a deadline.
	using Job = UpdateJob;

	constexpr size_t kMaxJobs = 4;
	std::mutex g_jobMutex;
	std::vector<Job> g_jobs;
	std::atomic_bool g_acceptingJobs{true};

	// Live-tree commit gate (ResourceCommit.hpp): long work (download,
	// extraction, copying) writes staging only; this gate serializes the short
	// per-file commit with BeginShutdown.
	CommitGate g_commitGate;

	bool Cancelled(const std::atomic_bool& cancel) {
		return cancel.load(std::memory_order_acquire);
	}

	// Returns a locked guard when a commit may proceed, and an unlocked guard
	// when shutdown already began or the caller was cancelled. The caller must
	// hold the guard across every write into the live tree.
	std::unique_lock<std::mutex> AcquireCommit(const std::atomic_bool* cancel, const char* what) {
		auto guard = g_commitGate.Acquire(cancel);
		if (!guard.owns_lock()) {
			GakumasLocal::Log::InfoFmt(
				"Not committing %s: the plugin is shutting down or the task was cancelled.", what);
		}
		return guard;
	}

	void ReapFinishedJobsLocked() {
		for (auto it = g_jobs.begin(); it != g_jobs.end();) {
			if (WaitForSingleObject(it->thread.native_handle(), 0) != WAIT_OBJECT_0) {
				++it;
				continue;
			}
			it->thread.join();
			it = g_jobs.erase(it);
		}
	}

	bool StartJob(const char* name, std::function<void(const std::atomic_bool&)> body) {
		std::lock_guard<std::mutex> lock(g_jobMutex);
		if (!g_acceptingJobs.load(std::memory_order_acquire)) {
			GakumasLocal::Log::InfoFmt("Refusing %s: the plugin is shutting down.", name);
			return false;
		}
		ReapFinishedJobsLocked();
		if (g_jobs.size() >= kMaxJobs) {
			GakumasLocal::Log::InfoFmt("Refusing %s: %zu update task(s) still running.", name, g_jobs.size());
			return false;
		}
		auto cancel = std::make_shared<std::atomic_bool>(false);
		const std::string jobName = name;
		// Own the thread before inserting into the vector, so even an allocation
		// failure retires it safely. The callback and owner copy the same flag.
		Job job{std::thread([cancel, jobName, body = std::move(body)]() {
				try {
					body(*cancel);
				}
				catch (const std::exception& error) {
					GakumasLocal::Log::ErrorFmt("Exception occurred in %s: %s", jobName.c_str(), error.what());
				}
				catch (...) {
					GakumasLocal::Log::ErrorFmt("Unknown exception occurred in %s", jobName.c_str());
				}
			}), cancel};
		g_jobs.push_back(std::move(job));
		return true;
	}
	}

	class UpdateJobGuard {
	public:
		explicit UpdateJobGuard(const char* jobName) : active(false) {
			bool expected = false;
			if (!updateJobRunning.compare_exchange_strong(expected, true)) {
				GakumasLocal::Log::InfoFmt("Skip %s: another resource update job is running.", jobName);
				return;
			}
			active = true;
			downloading = true;
            progress.Begin(UpdatePhase::Checking);
		}

		~UpdateJobGuard() {
			if (!active) return;
            progress.Begin(UpdatePhase::Idle);
			downloading = false;
			updateJobRunning = false;
		}

		explicit operator bool() const {
			return active;
		}

	private:
		bool active;
	};

	std::string trimString(std::string content) {
		auto is_not_space = [](unsigned char ch) {
			return !std::isspace(ch);
		};
		content.erase(content.begin(), std::find_if(content.begin(), content.end(), is_not_space));
		content.erase(std::find_if(content.rbegin(), content.rend(), is_not_space).base(), content.end());
		return content;
	}

	std::string readTrimmedFile(const std::filesystem::path& filePath) {
		std::ifstream file(SafeResource::ExtendedAbsolute(filePath));
		if (!file) {
			return "Unknown";
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		return trimString(buffer.str());
	}

	std::filesystem::path getTextureResourceRoot() {
		return LocalifyData::Capture()->ActiveBase(LocalifyData::Kind::Texture) / "texture2d";
	}

	std::string SerializeProgramConfig() {
		nlohmann::json config;
#ifndef GKMS_HOSTED
		config["enableConsole"] = g_enable_console;
#endif // Hosted keeps the legacy console preference as inactive preserved data.
		config["useRemoteAssets"] = g_useRemoteAssets;
		config["transRemoteZipUrl"] = g_remoteResourceUrl;
		config["useAPIAssets"] = g_useAPIAssets;
		config["useAPIAssetsURL"] = g_useAPIAssetsURL;
		config["useAPITextureAssets"] = g_useAPITextureAssets;
		config["useAPITextureAssetsURL"] = g_useAPITextureAssetsURL;
		config["delTextureRemoteAfterUpdate"] = g_delTextureRemoteAfterUpdate;
		return config.dump(4);
	}

	bool WriteProgramConfig(const std::string& content) {
        try {
            LocalifyData::SaveConfig(ProgramConfigJson, content);
            GakumasLocal::Log::Info("SaveProgramConfig success");
            return true;
        } catch (const std::exception& error) {
            GakumasLocal::Log::ErrorFmt("SaveProgramConfig refused; existing data preserved: %s", error.what());
            return false;
        }
	}
    bool CreateProgramConfigIfMissing() {
        return LocalifyData::SaveConfigNew(ProgramConfigJson, SerializeProgramConfig());
    }

	void saveProgramConfig() {
		WriteProgramConfig(SerializeProgramConfig());
	}

	web::http::http_response send_get(std::string url, int timeout) {
		web::http::client::http_client_config cfg;
		cfg.set_timeout(utility::seconds(30));
		web::http::client::http_client client(utility::conversions::to_utf16string(url), cfg);
		return client.request(web::http::methods::GET).get();
	}

	bool DownloadFile(const std::string& url, const std::filesystem::path& outputPath,
		const std::atomic_bool* cancel = nullptr) {
		using namespace utility;
		using namespace web;
		using namespace web::http;
		using namespace web::http::client;
		using namespace concurrency::streams;

		try {
			if (cancel && cancel->load(std::memory_order_acquire)) return false;
			// 打开输出文件流（同步方式）
			auto outTask = fstream::open_ostream(outputPath.wstring());
			outTask.wait();
			auto fileStream = outTask.get();

			// 创建 HTTP 客户端，注意：如果 url 包含完整路径，cpprestsdk 会自动解析
			http_client client(conversions::to_string_t(url));

            progress.Begin(UpdatePhase::Downloading);

			// 发起 GET 请求
			auto responseTask = client.request(methods::GET);
			responseTask.wait();
			http_response response = responseTask.get();
			if (response.status_code() != status_codes::OK) {
				GakumasLocal::Log::ErrorFmt("DownloadFile error: %d", response.status_code());
				return false;
			}

			// 获取响应头中的文件大小（如果存在）
			uint64_t contentLength = 0;
			if (response.headers().has(L"Content-Length"))
				contentLength = std::stoull(conversions::to_utf8string(response.headers().find(L"Content-Length")->second));

			// 读取响应体，逐块写入文件，同时更新进度
			auto inStream = response.body();
			const size_t bufferSize = 8192;
			// std::vector<unsigned char> buffer(bufferSize);
			size_t totalDownloaded = 0;

			while (true) {
				// Shutdown cancellation checkpoint: a chunk boundary is the only
				// place this transfer can stop without leaving a half-written file
				// that the caller cannot distinguish from a complete one.
				if (cancel && cancel->load(std::memory_order_acquire)) {
					fileStream.close().wait();
					GakumasLocal::Log::Info("Download cancelled by plugin shutdown.");
					return false;
				}
				auto readTask = inStream.read(fileStream.streambuf(), bufferSize);
				readTask.wait();
				size_t bytesRead = readTask.get();
				if (bytesRead == 0)
					break;
				totalDownloaded += bytesRead;
                progress.Downloaded(totalDownloaded, contentLength);
			}
			fileStream.close().wait();
            progress.Begin(UpdatePhase::Preparing);
			return true;
		}
		catch (const std::exception& e) {
			GakumasLocal::Log::ErrorFmt("DownloadFile error: %s", e.what());
			return false;
		}
	}

	std::string GetCurrentResourceVersion(bool useCache) {
        std::lock_guard<std::mutex> lock(g_versionMutex);
        const auto path = LocalifyData::Capture()->ActiveBase(LocalifyData::Kind::Translation) / "version.txt";
        return resourceVersionCache.Read(path, useCache, readTrimmedFile);
    }
    std::string GetCurrentTextureVersion(bool useCache) {
        std::lock_guard<std::mutex> lock(g_versionMutex);
        const auto path = getTextureResourceRoot() / "texture_version.txt";
        return textureVersionCache.Read(path, useCache, readTrimmedFile);
    }


    bool RecoverInterruptedUpdates() {
        try {
            if (LocalifyData::Capture()->migrated) return true; // Old recovery snapshots are protected, never replayed into a new root.
            const auto state = gakumasLocalPath / ".localify-updates";
            SafeResource::CheckNoReparse(state);
            if (!std::filesystem::exists(state)) return true;
            SafeResource::ResourceInstaller installer(gakumasLocalPath);
            const auto count = installer.RecoverInterrupted(&g_commitGate);
            if (count) {
                GakumasLocal::Log::InfoFmt("Recovered %zu interrupted resource transaction(s); uncommitted candidates retained.", count);
            }
            return true;
        } catch (const std::exception& error) {
            GakumasLocal::Log::ErrorFmt("Resource update recovery needs attention; originals and journals retained: %s", error.what());
            return false;
        }
    }
    bool SelectResourceSet(bool texture, const std::string& source, const std::string& id) {
        try {
            {
                auto guard = AcquireCommit(nullptr, "resource-set selection");
                if (!guard.owns_lock()) return false;
                LocalifyData::SelectSet(texture ? LocalifyData::Kind::Texture : LocalifyData::Kind::Translation, source, id);
            }
            { std::lock_guard<std::mutex> lock(g_versionMutex); resourceVersionCache.Clear(); textureVersionCache.Clear(); }
            g_reload_all_data();
            return true;
        } catch (const std::exception& error) {
            GakumasLocal::Log::ErrorFmt("Resource-set selection refused; previous selection retained: %s", error.what());
            return false;
        }
    }

    bool installLayeredPackage(const std::string& downloadUrl, SafeResource::PackageKind kind,
        const std::string& expectedVersion, bool deleteDownload, const std::atomic_bool* cancel) {
        using namespace SafeResource;
        try {
            const auto view = LocalifyData::ReadDisk();
            const auto beforeDownload = LocalifyData::CaptureSelection(*view, kind);
            const auto transaction = view->pluginRoot / "updates" / NativePath(UniqueId());
            EnsureDirectory(transaction);
            const auto archive = transaction / "download.zip";
            if (!DownloadFile(downloadUrl, archive, cancel) || (cancel && Cancelled(*cancel))) return false;
            std::string error;
            if (!ExtractResourcePackage(archive, transaction / "incoming", kind, error))
                throw std::runtime_error("Resource package refused: " + error);
            const auto versionFile = transaction / "incoming" /
                (kind == PackageKind::Texture ? "texture2d/texture_version.txt" : "version.txt");
            const auto version = readTrimmedFile(versionFile);
            if (kind == PackageKind::Texture && (version == "Unknown" || version.empty() ||
                (!expectedVersion.empty() && expectedVersion != version)))
                throw std::runtime_error("Texture package version differs from the selected release");
            if (kind == PackageKind::Translation) {
                if (!HasUsablePackageVersion(version))
                    throw std::runtime_error("Translation package version.txt is missing or empty");
                const auto localization = transaction / NativePath("incoming/local-files/localization.json");
                if (!std::filesystem::is_regular_file(localization) || !ReadJournal(localization).is_object())
                    throw std::runtime_error("Translation package has no valid localization.json");
            }
            if (cancel && Cancelled(*cancel)) return false;
            const auto installed = CompleteDownloadedPack(transaction / "incoming", kind, version, beforeDownload,
                [] {
                    progress.Begin(UpdatePhase::AwaitingChoice);
                    return MessageBoxW(nullptr,
                        utility::conversions::to_string_t(GkmsGUII18n::ts("resource_legacy_switch")).c_str(),
                        L"Resource Update", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
                },
                [&](auto select) {
                    auto guard = AcquireCommit(cancel, "resource-set selection");
                    return guard.owns_lock() && select();
                });
            if (deleteDownload) {
                CheckNoReparse(archive); std::error_code ec; std::filesystem::remove(archive, ec);
            }
            { std::lock_guard<std::mutex> lock(g_versionMutex); resourceVersionCache.Clear(); textureVersionCache.Clear(); }
            GakumasLocal::Log::InfoFmt("Managed resource pack %s stored; selected=%d; imported legacy and user overrides retained.", installed.id.c_str(), installed.selected ? 1 : 0);
            return true;
        } catch (const std::exception& error) {
            GakumasLocal::Log::ErrorFmt("Layered resource update failed; active set and user overrides retained: %s", error.what());
            return false;
        }
    }

    bool installResourcePackage(const std::string& downloadUrl, SafeResource::PackageKind kind,
        const std::string& expectedVersion, bool deleteDownload, const std::atomic_bool* cancel) {
        if (LocalifyData::Capture()->migrated)
            return installLayeredPackage(downloadUrl, kind, expectedVersion, deleteDownload, cancel);
        using namespace SafeResource;
        try {
            ResourceInstaller installer(gakumasLocalPath);
            installer.RecoverInterrupted(&g_commitGate);
            const auto transaction = installer.NewTransaction();
            const auto archive = transaction / "download.zip";
            if (!DownloadFile(downloadUrl, archive, cancel)) return false;
            if (cancel && Cancelled(*cancel)) return false;

            std::string extractionError;
            if (!ExtractResourcePackage(archive, transaction / "incoming", kind, extractionError)) {
                GakumasLocal::Log::ErrorFmt("Resource package refused; installed files untouched: %s", extractionError.c_str());
                return false;
            }
            if (kind == PackageKind::Texture) {
                const auto version = readTrimmedFile(transaction / "incoming" / "texture2d" / "texture_version.txt");
                if (version.empty() || version == "Unknown" || (!expectedVersion.empty() && version != expectedVersion)) {
                    GakumasLocal::Log::Error("Texture package version is missing or differs from the selected release; installed files untouched.");
                    return false;
                }
            }
            if (cancel && Cancelled(*cancel)) return false;
            const auto conflicts = installer.Prepare(transaction, kind);
            auto choice = ConflictChoice::PreserveCurrent;
            if (conflicts) {
                progress.Begin(UpdatePhase::AwaitingChoice);
                const auto text = std::format(L"{}\n\n{}: {}",
                    utility::conversions::to_string_t(GkmsGUII18n::ts("resource_conflict_choice")),
                    utility::conversions::to_string_t(GkmsGUII18n::ts("resource_conflict_count")), conflicts);
                const int selected = MessageBoxW(nullptr, text.c_str(), L"Resource Update",
                    MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
                if (selected == IDCANCEL || selected == 0) return false;
                if (selected == IDNO) choice = ConflictChoice::UsePackageWithBackup;
            }
            if (cancel && Cancelled(*cancel)) return false;
            progress.Begin(UpdatePhase::Preparing);
            const auto installed = installer.Apply(transaction, choice, cancel, {}, &g_commitGate);
            if (deleteDownload) {
                CheckNoReparse(archive);
                std::error_code ec;
                std::filesystem::remove(archive, ec); // Only this transaction's own download.
            }
            {
                std::lock_guard<std::mutex> lock(g_versionMutex);
                resourceVersionCache.Clear();
                textureVersionCache.Clear();
            }
            GakumasLocal::Log::InfoFmt(
                "Resource transaction finished: %zu installed, %zu unchanged, %zu preserved conflicts. Recovery: %s",
                installed.installed, installed.unchanged, installed.conflicts, Utf8(transaction).c_str());
            if (conflicts || installed.conflicts) {
                const auto message = std::format(L"{}\n\n{}\n\n{}: {}",
                    utility::conversions::to_string_t(GkmsGUII18n::ts("resource_update_preserved")),
                    utility::conversions::to_string_t(installed.conflicts ?
                        GkmsGUII18n::ts("resource_update_partial") : GkmsGUII18n::ts("resource_update_applied")),
                    utility::conversions::to_string_t(GkmsGUII18n::ts("resource_recovery_directory")),
                    transaction.wstring());
                MessageBoxW(nullptr, message.c_str(), L"Resource Update", MB_OK | MB_ICONINFORMATION);
            }
            return true;
        } catch (const std::exception& error) {
            GakumasLocal::Log::ErrorFmt("Resource update interrupted; current files and recovery copies retained: %s", error.what());
            return false;
        }
    }

	void CheckUpdateFromAPI(bool isManual) {
		// Capture the GUI-owned settings here, on the calling thread. The task
		// must not read them later: the player can keep editing the URL while
		// this request is in flight.
		const bool enabled = g_useAPIAssets;
		const std::string apiUrl = g_useAPIAssetsURL;
		if (!enabled) return;
		StartJob("resource API update", [isManual, apiUrl](const std::atomic_bool& cancel) {
			UpdateJobGuard job("resource API update");
			if (!job) return;
			if (Cancelled(cancel)) return;

			GakumasLocal::Log::Info("Checking update from API...");

			auto response = send_get(apiUrl, 30);
			if (response.status_code() != 200) {
				GakumasLocal::Log::ErrorFmt("Failed to check update from API: %d\n", response.status_code());
				return;
			}

			auto data = nlohmann::json::parse(response.extract_utf8string().get());

			std::string remoteVersion = data["tag_name"];
			const auto localVersion = GetCurrentResourceVersion(false);

			if (localVersion == remoteVersion) {
				if (isManual) {
					auto check = MessageBoxW(NULL, utility::conversions::to_string_t(GkmsGUII18n::ts("local_file_already_latest")).c_str(),
						L"Check Update", MB_OKCANCEL);
					if (check != IDOK) {
						return;
					}
				}
				else {
					return;
				}
			}

			std::string description = data["body"];

            progress.Begin(UpdatePhase::AwaitingChoice);
			auto check = MessageBoxW(NULL, std::format(L"{} -> {}\n\n{}", utility::conversions::to_string_t(localVersion),
				utility::conversions::to_string_t(remoteVersion), utility::conversions::to_string_t(description)).c_str(),
				L"Resource Update", MB_OKCANCEL);
			if (check != IDOK) {
				return;
			}
			if (Cancelled(cancel)) return;

			if (!data.contains("assets") || !data["assets"].is_array()) {
				GakumasLocal::Log::Error("API response doesn't contain assets array.");
				return;
			}
			for (const auto& asset : data["assets"]) {
				if (!asset.contains("name") || !asset.contains("browser_download_url"))
					continue;
				std::string name = asset["name"];
				if (name.ends_with(".zip")) {
					std::string downloadUrl = asset["browser_download_url"];

					if (installResourcePackage(downloadUrl, SafeResource::PackageKind::Translation, "", false, &cancel)) {
						if (Cancelled(cancel)) return;
						g_reload_all_data();
						GakumasLocal::Log::Info("Resource update finished; see the transaction summary for preserved conflicts.");
					}
					// 仅解压一个文件
					return;
				}
			}
			GakumasLocal::Log::Error("No .zip file found.");
		});
	}

	void CheckTextureUpdateFromAPI(bool isManual) {
		const bool enabled = g_useAPITextureAssets;
		const std::string apiUrl = g_useAPITextureAssetsURL;
		const bool deleteRemoteAfter = g_delTextureRemoteAfterUpdate;
		if (!enabled) return;
		StartJob("texture API update", [isManual, apiUrl, deleteRemoteAfter](const std::atomic_bool& cancel) {
			UpdateJobGuard job("texture API update");
			if (!job) return;
			if (Cancelled(cancel)) return;

			GakumasLocal::Log::Info("Checking texture update from API...");

			auto response = send_get(apiUrl, 30);
			if (response.status_code() != 200) {
				GakumasLocal::Log::ErrorFmt("Failed to check texture update from API: %d\n", response.status_code());
				return;
			}

			auto data = nlohmann::json::parse(response.extract_utf8string().get());
			if (!data.contains("tag_name") || !data["tag_name"].is_string()) {
				GakumasLocal::Log::Error("Texture API response doesn't contain tag_name.");
				return;
			}

			std::string remoteVersion = data["tag_name"];
			const auto localVersion = GetCurrentTextureVersion(false);

			if (localVersion == remoteVersion) {
				if (isManual) {
					auto check = MessageBoxW(NULL, utility::conversions::to_string_t(GkmsGUII18n::ts("local_file_already_latest")).c_str(),
						L"Texture Resource Update", MB_OKCANCEL);
					if (check != IDOK) {
						return;
					}
				}
				else {
					return;
				}
			}

			std::string description = "";
			if (data.contains("body") && data["body"].is_string()) {
				description = data["body"];
			}

            progress.Begin(UpdatePhase::AwaitingChoice);
			auto check = MessageBoxW(NULL, std::format(L"{} -> {}\n\n{}", utility::conversions::to_string_t(localVersion),
				utility::conversions::to_string_t(remoteVersion), utility::conversions::to_string_t(description)).c_str(),
				L"Texture Resource Update", MB_OKCANCEL);
			if (check != IDOK) {
				return;
			}
			if (Cancelled(cancel)) return;

			if (!data.contains("assets") || !data["assets"].is_array()) {
				GakumasLocal::Log::Error("Texture API response doesn't contain assets array.");
				return;
			}

			std::string downloadUrl = "";
			for (const auto& asset : data["assets"]) {
				if (!asset.contains("name") || !asset.contains("browser_download_url") || !asset["name"].is_string() || !asset["browser_download_url"].is_string()) {
					continue;
				}
				std::string name = asset["name"];
				std::string lowerName = name;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
				if (lowerName.ends_with(".zip")) {
					downloadUrl = asset["browser_download_url"];
					if (lowerName == "texture2d.zip") {
						break;
					}
				}
			}

			if (downloadUrl.empty()) {
				GakumasLocal::Log::Error("No texture .zip file found.");
				return;
			}

			if (installResourcePackage(downloadUrl, SafeResource::PackageKind::Texture, remoteVersion, deleteRemoteAfter, &cancel)) {
				if (Cancelled(cancel)) return;
				g_reload_all_data();
				GakumasLocal::Log::Info("Texture update finished; see the transaction summary for preserved conflicts.");
			}
		});
	}

	void checkUpdateFromURL(const std::string& downloadUrl) {
		// The URL is already a value; nothing else is read from the GUI.
		StartJob("remote zip update", [downloadUrl](const std::atomic_bool& cancel) {
			UpdateJobGuard job("remote zip update");
			if (!job) return;
			if (Cancelled(cancel)) return;
			if (installResourcePackage(downloadUrl, SafeResource::PackageKind::Translation, "", false, &cancel)) {
				if (Cancelled(cancel)) return;
				g_reload_all_data();
				GakumasLocal::Log::Info("Resource update finished; see the transaction summary for preserved conflicts.");
			}
		});
	}

	void BeginShutdown() {
		{
			std::lock_guard<std::mutex> lock(g_jobMutex);
			g_acceptingJobs.store(false, std::memory_order_release);
			for (auto& job : g_jobs) {
				job.cancel->store(true, std::memory_order_release);
			}
			GakumasLocal::Log::InfoFmt("Resource updates closed: %zu task(s) marked for cancellation.", g_jobs.size());
		}
		// Doors closed; now wait for a commit that was already past its check.
		// It only moves staged files into place, so this wait is bounded by the
		// file count, not by the size of the download or extraction. Once it
		// returns, no task can write the installed resources any more — even one
		// that WaitForJobs has to detach.
		g_commitGate.Close();
	}

	void WaitForJobs(unsigned timeoutMs) {
		std::vector<Job> pending;
		{
			std::lock_guard<std::mutex> lock(g_jobMutex);
			pending.swap(g_jobs);
		}
		if (pending.empty()) return;
		const ULONGLONG started = GetTickCount64();
		const ULONGLONG deadline = started + timeoutMs;
		size_t detached = 0;
		for (auto& job : pending) {
			const ULONGLONG now = GetTickCount64();
			const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
			if (WaitForSingleObject(job.thread.native_handle(), remaining) == WAIT_OBJECT_0) {
				job.thread.join();
				continue;
			}
			// Bounded wait expired. The module stays loaded until process exit,
			// so the thread cannot lose its code, but every apply step checks the
			// cancel flag first, so it will not publish a result after this point.
			job.thread.detach();
			++detached;
		}
		GakumasLocal::Log::InfoFmt("Resource updates stopped in %llu ms (%zu task(s)%s).",
			static_cast<unsigned long long>(GetTickCount64() - started), pending.size(),
			detached ? ", at least one detached after the deadline" : "");
	}
}
