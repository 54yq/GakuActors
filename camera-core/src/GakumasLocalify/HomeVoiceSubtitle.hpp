#pragma once

#include "Il2cppUtils.hpp"
#include "Plugin.h"
#include "data/LocalifyData.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#ifndef GKMS_WINDOWS
#include <unistd.h>
#endif

// Shared DMM/Android implementation: managed calls use runtime_invoke so value types never
// depend on ARM64 / Windows x64 structure-return conventions.
namespace GakumasLocal::HomeVoiceSubtitle {
    using String = UnityResolve::UnityType::String;
    struct V2 { float x, y; };
    struct V3 { float x, y, z; };
    struct Color { float r, g, b, a; };
    struct Rect { float x, y, width, height; };
    struct ArrayHeader { void* klass; void* monitor; void* bounds; uintptr_t length; };

    inline HookInstaller*& RuntimeInstaller() { static HookInstaller* installer = nullptr; return installer; }
    inline unsigned long CurrentThreadId() {
#ifdef GKMS_WINDOWS
        return static_cast<unsigned long>(GetCurrentThreadId());
#else
        return static_cast<unsigned long>(gettid());
#endif
    }

    inline void* Export(const char* name) {
        static std::mutex mutex;
        static std::unordered_map<std::string, void*> exports;
        std::lock_guard lock(mutex);
        auto found = exports.find(name);
        if (found != exports.end()) return found->second;
        auto installer = RuntimeInstaller();
        if (!installer) throw std::runtime_error("home voice runtime installer not bound");
        auto address = reinterpret_cast<void*>(installer->LookupSymbol(name));
        if (!address) throw std::runtime_error(std::string("IL2CPP export missing: ") + name);
        return exports.emplace(name, address).first->second;
    }

    template<class Return, class... Args> inline Return Api(const char* name, Args... args) {
        // Release the address-cache lock before calling into IL2CPP. Its managed
        // callbacks may reenter native hooks or request another IL2CPP export.
        auto function = reinterpret_cast<Return (*)(Args...)>(Export(name));
        return function(args...);
    }

    // The Unity 6 DMM and Android integration use pointer-sized GC handles. A uint32_t
    // truncates the native handle address before get_target/free dereference it.
    using GcHandle = uintptr_t;
    struct GcFunctions {
        GcHandle (*create)(void*, bool) = nullptr;
        void* (*target)(GcHandle) = nullptr;
        void (*release)(GcHandle) = nullptr;
    };
    inline GcFunctions& GcRuntime() { static GcFunctions functions; return functions; }
    inline void BindGcRuntime() {
        auto& functions = GcRuntime();
        functions.create = reinterpret_cast<decltype(functions.create)>(Export("il2cpp_gchandle_new"));
        functions.target = reinterpret_cast<decltype(functions.target)>(Export("il2cpp_gchandle_get_target"));
        functions.release = reinterpret_cast<decltype(functions.release)>(Export("il2cpp_gchandle_free"));
    }

    class GcRef {
        GcHandle handle = 0;
    public:
        GcRef() = default;
        GcRef(const GcRef&) = delete;
        GcRef& operator=(const GcRef&) = delete;
        GcRef(GcRef&& other) noexcept : handle(std::exchange(other.handle, 0)) {}
        GcRef& operator=(GcRef&& other) noexcept {
            std::swap(handle, other.handle); return *this;
        }
        ~GcRef() { Set(); }
        void* Get() const {
            return handle ? GcRuntime().target(handle) : nullptr;
        }
        void Set(void* object = nullptr) {
            // Acquire the replacement before releasing the old reference.
            const GcHandle next = object ? GcRuntime().create(object, false) : 0;
            if (handle) GcRuntime().release(handle);
            handle = next;
        }
    };

    inline void* Class(const char* assembly, const char* ns, const char* name) {
        auto domain = Api<void*>("il2cpp_domain_get");
        auto targetAssembly = Api<void*>("il2cpp_domain_assembly_open", domain, assembly);
        if (!targetAssembly) throw std::runtime_error(std::string("assembly missing: ") + assembly);
        auto image = Api<void*>("il2cpp_assembly_get_image", targetAssembly);
        auto klass = Api<void*>("il2cpp_class_from_name", image, ns, name);
        if (!klass) throw std::runtime_error(std::string("class missing: ") + ns + "." + name);
        return klass;
    }

    inline void* Method(void* klass, const char* name, int count) {
        for (auto current = klass; current; current = Api<void*>("il2cpp_class_get_parent", current)) {
            if (auto method = Api<void*>("il2cpp_class_get_method_from_name", current, name, count))
                return method;
        }
        throw std::runtime_error(std::string("method missing: ") + name + "/" + std::to_string(count));
    }

    inline void* TypeArgumentMethod(void* klass, const char* name) {
        void* iterator = nullptr;
        while (auto method = Api<void*>("il2cpp_class_get_methods", klass, &iterator)) {
            if (std::string_view(Api<const char*>("il2cpp_method_get_name", method)) != name ||
                Api<uint32_t>("il2cpp_method_get_param_count", method) != 1) continue;
            auto parameter = Api<void*>("il2cpp_method_get_param", method, uint32_t(0));
            auto typeName = Api<char*>("il2cpp_type_get_name", parameter);
            const bool matches = typeName && std::string_view(typeName) == "System.Type";
            if (typeName) Api<void>("il2cpp_free", typeName);
            if (matches) return method;
        }
        throw std::runtime_error(std::string("method missing: ") + name + "(System.Type)");
    }

    inline void* Overload(void* klass, const char* name, std::initializer_list<const char*> types) {
        for (auto current = klass; current; current = Api<void*>("il2cpp_class_get_parent", current)) {
            void* iterator = nullptr;
            while (auto method = Api<void*>("il2cpp_class_get_methods", current, &iterator)) {
                if (std::string_view(Api<const char*>("il2cpp_method_get_name", method)) != name ||
                    Api<uint32_t>("il2cpp_method_get_param_count", method) != types.size() ||
                    Api<bool>("il2cpp_method_is_generic", method)) continue;
                bool matches = true; uint32_t index = 0;
                for (auto expected : types) {
                    auto parameter = Api<void*>("il2cpp_method_get_param", method, index++);
                    auto typeName = Api<char*>("il2cpp_type_get_name", parameter);
                    matches = matches && typeName && std::string_view(typeName) == expected;
                    if (typeName) Api<void>("il2cpp_free", typeName);
                }
                if (matches) return method;
            }
        }
        throw std::runtime_error(std::string("overload missing: ") + name);
    }

    inline void* Invoke(void* method, void* object, std::initializer_list<void*> parameters = {}) {
        void* exception = nullptr;
        auto result = Api<void*>("il2cpp_runtime_invoke", method, object,
            parameters.size() ? const_cast<void**>(parameters.begin()) : nullptr, &exception);
        if (exception) {
            auto klass = *static_cast<Il2cppUtils::Il2CppClassHead**>(exception);
            throw std::runtime_error(std::string("managed exception: ") + klass->namespaze + "." + klass->name);
        }
        return result;
    }

    // Only used by the main-thread UI tick. Hook callbacks use pre-resolved methods.
    inline void* Call(void* object, const char* name, std::initializer_list<void*> parameters = {}) {
        if (!object) throw std::runtime_error(std::string("null object: ") + name);
        auto klass = *static_cast<void**>(object);
        static std::map<std::tuple<void*, std::string, size_t>, void*> methods;
        auto key = std::make_tuple(klass, std::string(name), parameters.size());
        auto found = methods.find(key);
        if (found == methods.end()) found = methods.emplace(key, Method(klass, name, static_cast<int>(parameters.size()))).first;
        return Invoke(found->second, object, parameters);
    }

    template<class T> inline T Unbox(void* object) {
        if (!object) throw std::runtime_error("null boxed result");
        return *Api<T*>("il2cpp_object_unbox", object);
    }

    template<class T> inline T Field(void* object, const char* name) {
        if (!object) return T{};
        for (auto klass = *static_cast<void**>(object); klass; klass = Api<void*>("il2cpp_class_get_parent", klass)) {
            if (auto field = Api<void*>("il2cpp_class_get_field_from_name", klass, name)) {
                T value{};
                Api<void>("il2cpp_field_get_value", object, field, &value);
                return value;
            }
        }
        throw std::runtime_error(std::string("field missing: ") + name);
    }

    inline void* Type(void* klass) {
        return Api<void*>("il2cpp_type_get_object", Api<void*>("il2cpp_class_get_type", klass));
    }
    inline String* Text(const std::string& value) {
        return Api<String*>("il2cpp_string_new", value.c_str());
    }
    inline void* Core(const char* name) { return Class("UnityEngine.CoreModule.dll", "UnityEngine", name); }
    inline void* Ui(const char* name) { return Class("UnityEngine.UI.dll", "UnityEngine.UI", name); }
    inline bool Alive(void* object) {
        if (!object) return false;
        static auto method = Method(Core("Object"), "IsNativeObjectAlive", 1);
        using IsAlive = bool (*)(void*, void*);
        return reinterpret_cast<IsAlive>(*static_cast<void**>(method))(object, method);
    }
    inline void Destroy(void* object) {
        if (Alive(object)) Invoke(Method(Core("Object"), "Destroy", 1), nullptr, {object});
    }

    // Internal slots, independent of the numeric HomePageType enum in DMM.
    inline constexpr int HomeSlot = 2;
    inline const char* PageName(int page) {
        constexpr const char* names[]{"Story", "Idol", "Home", "Pvp"};
        return page >= 0 && page < 4 ? names[page] : "Outside";
    }
    inline int PageSlot(std::string_view name) {
        if (name == "HomeStoryPageView" || name == "HomeStoryActor") return 0;
        if (name == "HomeIdolPageView" || name == "HomeIdolActor") return 1;
        if (name == "HomePageView" || name == "HomeTopActor") return HomeSlot;
        if (name == "HomePvpPageView" || name == "HomeContestActor") return 3;
        return -1;
    }
    inline int PageOf(void* object) {
        return object ? PageSlot(Api<const char*>("il2cpp_class_get_name", *static_cast<void**>(object))) : -1;
    }
    inline bool ActorMatches(void* expected, void* bound) { return !bound || (expected && bound == expected); }
    inline std::unordered_map<std::string, std::string> ParseSubtitles(const nlohmann::json& entries) {
        // Accept the editable voiceAssetId/text rows, while retaining compatibility
        // with existing cue-to-text object configurations.
        if (entries.is_object()) return entries.get<std::unordered_map<std::string, std::string>>();
        if (!entries.is_array()) throw std::runtime_error("subtitles must be an array of voiceAssetId/text entries");
        std::unordered_map<std::string, std::string> result;
        result.reserve(entries.size());
        for (const auto& entry : entries) {
            auto id = entry.at("voiceAssetId").get<std::string>();
            result.insert_or_assign(std::move(id), entry.value("text", std::string{}));
        }
        return result;
    }
    inline bool HasSubtitleText(std::string_view text) {
        return text.find_first_not_of(" \t\r\n") != std::string_view::npos;
    }
    inline std::string TestSubtitle(const std::string& cue) {
        const auto home = cue.find("_home_");
        return "语音字幕测试\n" + (home == std::string::npos ? cue : cue.substr(home + 1));
    }
    inline bool CanBuild(bool visible, bool failed, bool viewAlive, bool actorAlive, bool homeAlive) {
        // Load the dialogue display prefab in Build; no hint or story callback
        // is required before showing subtitles on the four home tabs.
        return visible && !failed && viewAlive && actorAlive && homeAlive;
    }
    inline float RebaseY(Rect parent, Rect content, float y) {
        return content.y + (y - parent.y) * content.height / parent.height;
    }
    inline float ButtonTop(Rect parent, Rect button, V2 anchorMin, V2 anchorMax, V2 pivot, V2 position) {
        const float anchorY = anchorMin.y + (anchorMax.y - anchorMin.y) * pivot.y;
        return parent.y + parent.height * anchorY + position.y + button.y + button.height;
    }
    inline float TextBubbleWidth(float textWidth, float horizontalPadding, float minimum, float maximum) {
        return std::clamp(std::ceil(textWidth + horizontalPadding), minimum, maximum);
    }
    inline Rect DialogueBox(float left, float levelBottom, float switcherTop, float width,
        float textHeight, float fontSize, float verticalPadding) {
        const float height = std::ceil((std::max)(fontSize, textHeight) + verticalPadding);
        return {left, (levelBottom + switcherTop - height) * 0.5f, width, height};
    }

    struct State {
        bool enabled = true, pageVisible = false, rebuild = false, uiFailed = false;
        bool pending = false, stopRequested = false, shown = false, showCueWhenMissing = false;
        bool buildDeferred = false, uiHidden = false;
        uint64_t sequence = 0;
        int page = HomeSlot;
        // Use the native header's design units for the left offset and the
        // adapted home content's design units for the dialogue width limits.
        float leftOffset = 40, minBubbleWidth = 424, bubbleWidth = 520;
        float currentBubbleWidth = 424;
        float fontSize = 32, textHeight = 0;
        V2 textPadding{40, 40}; // Total horizontal/vertical inset: 20 per side.
        std::array<GcRef, 4> views, actors;
        GcRef nativeTemplate, player, root, label, bubble, opacity;
        GcRef levelTransform, switcherTransform, headerCanvas, contentCanvas;
        GcRef headerFade;
        Rect positionedBox{};
        V2 positionedOrigin{};
        bool hasPosition = false;
        std::chrono::steady_clock::time_point nextLayoutLookup{};
        std::string cue;
        std::unordered_map<std::string, std::string> subtitles;
        std::chrono::steady_clock::time_point nextTick{};
        void* getActor = nullptr;
        void* isEndOrStop = nullptr;
        void* getCurrentPage = nullptr;
    };
    // Keep native state alive until process exit; avoid Unity calls from DLL CRT teardown.
    inline State& Data() { static auto state = new State; return *state; }

    enum class EventKind { ViewReady, ActorReady, TemplateReady, PageShown, PageHidden, ScreenDestroyed, UiHidden, UiRestored, Played, Stopped };
    struct Event {
        EventKind kind;
        GcRef object, actor;
        std::string cue;
        int page;
        explicit Event(EventKind value, void* instance = nullptr, void* boundActor = nullptr, std::string id = {}, int slot = -1)
            : kind(value), cue(std::move(id)), page(slot) { object.Set(instance); actor.Set(boundActor); }
    };
    struct Mailbox {
        std::mutex mutex;
        std::deque<Event> events;
        std::atomic<unsigned long> mainThread{0};
    };
    inline Mailbox& Inbox() { static auto mailbox = new Mailbox; return *mailbox; }
    inline void Post(Event event) {
        auto& mailbox = Inbox();
        // Only native event moves under this lock. No IL2CPP, Unity or logs.
        std::lock_guard lock(mailbox.mutex); mailbox.events.emplace_back(std::move(event));
    }
    inline std::deque<Event> TakeEvents() {
        std::deque<Event> events; auto& mailbox = Inbox();
        { std::lock_guard lock(mailbox.mutex); events.swap(mailbox.events); }
        return events;
    }
    inline thread_local bool inTick = false;

    inline void Hide(State& state, const char* reason) {
        if (Alive(state.root.Get())) { bool active = false; Call(state.root.Get(), "SetActive", {&active}); }
        state.shown = state.pending = state.stopRequested = false;
        state.player.Set();
    }

    inline void UpdatePlaybackVisibility(State& state, bool ended, float nativeAlpha) {
        // Playback expiry is sticky even if the same sound player is reused.
        // During FreeLook, finish fading the old text before discarding it.
        state.stopRequested = state.stopRequested || ended;
        if (state.stopRequested && (!state.uiHidden || nativeAlpha <= 0.001f))
            Hide(state, "playback ended");
    }

    inline float FollowNativeUiFade(State& state) {
        if (!Alive(state.opacity.Get()) || !Alive(state.headerFade.Get())) return -1;
        float alpha = Unbox<float>(Call(state.headerFade.Get(), "get_alpha"));
        alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.0f, 1.0f) : 0.0f;
        Call(state.opacity.Get(), "set_alpha", {&alpha});
        return alpha;
    }

    inline void ReleaseUi(State& state) {
        // Rebuilding the view must preserve an already captured voice event.
        if (Alive(state.root.Get())) { bool active = false; Call(state.root.Get(), "SetActive", {&active}); }
        state.shown = false;
        Destroy(state.root.Get());
        state.root.Set(); state.label.Set(); state.bubble.Set(); state.opacity.Set();
        state.levelTransform.Set(); state.switcherTransform.Set();
        state.headerFade.Set();
        state.headerCanvas.Set(); state.contentCanvas.Set(); state.hasPosition = false;
    }

    inline void* NewObject(const char* name, void* parent) {
        auto objectClass = Core("GameObject");
        auto array = Api<ArrayHeader*>("il2cpp_array_new", Class("mscorlib.dll", "System", "Type"), uintptr_t(1));
        GcRef arrayRef; arrayRef.Set(array);
        auto slot = reinterpret_cast<void**>(array + 1);
        Api<void>("il2cpp_gc_wbarrier_set_field", array, slot, Type(Core("RectTransform")));
        auto object = Api<void*>("il2cpp_object_new", objectClass);
        GcRef objectRef; objectRef.Set(object);
        try {
            Invoke(Method(objectClass, ".ctor", 2), object, {Text(name), array});
            bool active = false, worldPositionStays = false;
            Call(object, "SetActive", {&active});
            auto transform = Call(object, "get_transform");
            Call(transform, "SetParent", {parent, &worldPositionStays});
            V3 scale{1, 1, 1}; Call(transform, "set_localScale", {&scale});
            auto parentObject = Call(parent, "get_gameObject");
            int layer = Unbox<int>(Call(parentObject, "get_layer"));
            Call(object, "set_layer", {&layer});
        } catch (...) { objectRef.Set(); arrayRef.Set(); throw; }
        objectRef.Set(); arrayRef.Set();
        return object;
    }

    inline void* Add(void* object, void* klass) {
        // AddComponent(string) also has one parameter; match System.Type explicitly.
        static auto method = TypeArgumentMethod(Core("GameObject"), "AddComponent");
        return Invoke(method, object, {Type(klass)});
    }
    inline void Stretch(void* object, V2 inset = {0, 0}) {
        auto transform = Call(object, "get_transform");
        V2 zero{0, 0}, one{1, 1}, center{0.5f, 0.5f}, delta{-inset.x, -inset.y}; V3 scale{1, 1, 1};
        Call(transform, "set_anchorMin", {&zero}); Call(transform, "set_anchorMax", {&one});
        Call(transform, "set_pivot", {&center}); Call(transform, "set_sizeDelta", {&delta});
        Call(transform, "set_anchoredPosition", {&zero}); Call(transform, "set_localScale", {&scale});
    }
    inline void* ChildObject(void* transform, const char* path) {
        auto child = Call(transform, "Find", {Text(path)});
        return Alive(child) ? Call(child, "get_gameObject") : nullptr;
    }
    inline void* ChildComponent(void* object, void* klass) {
        bool includeInactive = true;
        return Invoke(Overload(Core("GameObject"), "GetComponentInChildren", {"System.Type", "System.Boolean"}),
            object, {Type(klass), &includeInactive});
    }
    inline void RoundedBackground(void* animationTransform) {
        // DMM's ResourceManager maps "commonatlas" to CommonAtlas. SpriteAtlas
        // belongs to CoreModule in this build, as verified in its metadata.
        auto atlas = Invoke(Overload(Core("Resources"), "Load", {"System.String", "System.Type"}), nullptr,
            {Text("commonatlas"), Type(Class("UnityEngine.CoreModule.dll", "UnityEngine.U2D", "SpriteAtlas"))});
        if (!Alive(atlas)) throw std::runtime_error("native CommonAtlas resource missing");
        GcRef atlasRef; atlasRef.Set(atlas);
        auto background = Call(atlas, "GetSprite", {Text("bg_round_r32_64px")});
        if (!Alive(background)) throw std::runtime_error("native rounded dialogue background missing");
        GcRef backgroundRef; backgroundRef.Set(background);
        auto frame = Call(atlas, "GetSprite", {Text("frame_round_r32_b4_64px")});
        if (!Alive(frame)) throw std::runtime_error("native rounded dialogue frame missing");
        GcRef frameRef; frameRef.Set(frame);
        auto backgroundObject = ChildObject(animationTransform, "Message Background/Vertical/Message Image");
        if (!Alive(backgroundObject)) throw std::runtime_error("native dialogue background object missing");
        auto frameObject = ChildObject(Call(backgroundObject, "get_transform"), "frame");
        if (!Alive(frameObject)) throw std::runtime_error("native dialogue frame object missing");
        auto backgroundImage = ChildComponent(backgroundObject, Ui("Image"));
        auto frameImage = ChildComponent(frameObject, Ui("Image"));
        if (!Alive(backgroundImage) || !Alive(frameImage)) throw std::runtime_error("native dialogue background/frame Image missing");
        // The sprites are white, but the ADV display samples the scene behind
        // it and adds a glass gradient. Disable only the clone's colour effects
        // before selecting the ordinary UI material, so all four tabs use an
        // opaque white bubble instead of inheriting the scene's colours.
        bool disabled = false;
        for (const char* effectName : {"LinearGradientGlass", "LinearGradient", "BackgroundBlur"}) {
            auto effect = ChildComponent(backgroundObject, Class("campus-submodule.Runtime.dll", "Campus.Common", effectName));
            if (Alive(effect)) Call(effect, "set_enabled", {&disabled});
        }
        auto material = Invoke(Method(Ui("Graphic"), "get_defaultGraphicMaterial", 0), nullptr);
        if (!Alive(material)) throw std::runtime_error("default native UI material missing");
        Color white{1, 1, 1, 1};
        for (auto image : {backgroundImage, frameImage}) {
            Call(image, "set_material", {material});
            Call(image, "set_color", {&white});
        }
        int sliced = 1; // UnityEngine.UI.Image.Type.Sliced; retain native borders.
        Call(backgroundImage, "set_overrideSprite", {nullptr});
        Call(backgroundImage, "set_sprite", {background});
        Call(backgroundImage, "set_type", {&sliced});
        Call(frameImage, "set_overrideSprite", {nullptr});
        Call(frameImage, "set_sprite", {frame});
        Call(frameImage, "set_type", {&sliced});
    }
    inline void* ContentParent(void* home) {
        // HomePage / Pages / UIContentArea. This shared parent receives the
        // game's safe-area/aspect adaptation, without the Pages animations.
        for (auto transform = Call(home, "get_transform"); transform; transform = Call(transform, "get_parent")) {
            auto name = static_cast<String*>(Call(transform, "get_name"))->ToString();
            if (name == "UIContentArea") return transform;
        }
        throw std::runtime_error("HomeTopScreen UIContentArea not found");
    }
    inline void* RootCanvas(void* transform) {
        static auto canvasClass = Class("UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
        static auto getComponent = TypeArgumentMethod(Core("GameObject"), "GetComponent");
        for (auto current = transform; Alive(current); current = Call(current, "get_parent")) {
            auto canvas = Invoke(getComponent, Call(current, "get_gameObject"), {Type(canvasClass)});
            if (Alive(canvas)) return Call(canvas, "get_rootCanvas");
        }
        return nullptr;
    }
    inline void* FindLevelTransform() {
        // Search scene components, not prefab assets. OutGameHeader has its own
        // canvas; do not assume its world coordinates match ScreenCanvas.
        static auto find = Overload(Core("Object"), "FindObjectsOfType", {"System.Type", "System.Boolean"});
        static auto levelClass = Class("Assembly-CSharp.dll", "Campus.Common", "ProduceLevelButtonView");
        bool includeInactive = false;
        auto array = static_cast<ArrayHeader*>(Invoke(find, nullptr, {Type(levelClass), &includeInactive}));
        if (!array) return nullptr;
        GcRef arrayRef; arrayRef.Set(array);
        auto entries = reinterpret_cast<void**>(array + 1);
        for (uintptr_t i = 0; i < array->length; ++i) {
            if (!Alive(entries[i])) continue;
            auto transform = Call(entries[i], "get_transform");
            for (auto current = transform; Alive(current); current = Call(current, "get_parent")) {
                auto name = static_cast<String*>(Call(current, "get_name"))->ToString();
                if (name.find("OutGameHeader") != std::string::npos) return transform;
            }
        }
        return nullptr;
    }
    inline bool BindLayout(State& state, void* parent, void* home) {
        if (Alive(state.levelTransform.Get()) && Alive(state.switcherTransform.Get()) &&
            Alive(state.headerCanvas.Get()) && Alive(state.contentCanvas.Get()) && Alive(state.headerFade.Get())) return true;
        auto now = std::chrono::steady_clock::now();
        if (now < state.nextLayoutLookup) return false;
        state.nextLayoutLookup = now + std::chrono::milliseconds(250);
        auto switcher = ChildObject(Call(home, "get_transform"), "PageRoot/ContentRoot/LeftRoot/CharacterSwitcherRoot");
        auto level = FindLevelTransform();
        if (!Alive(level) || !Alive(switcher)) return false;
        auto headerCanvas = RootCanvas(level), contentCanvas = RootCanvas(parent);
        if (!Alive(headerCanvas) || !Alive(contentCanvas)) return false;
        // All four tabs share OutGameHeaderView.ToggleContent's native fade.
        // Bind its actual serialized CanvasGroup, not the PLv button's own group.
        static auto headerClass = Class("Assembly-CSharp.dll", "Campus.OutGame", "OutGameHeaderView");
        static auto getComponent = TypeArgumentMethod(Core("GameObject"), "GetComponent");
        void* headerFade = nullptr;
        for (auto current = level; Alive(current); current = Call(current, "get_parent")) {
            auto header = Invoke(getComponent, Call(current, "get_gameObject"), {Type(headerClass)});
            if (Alive(header)) { headerFade = Field<void*>(header, "_contentFade"); break; }
        }
        if (!Alive(headerFade)) return false;
        state.levelTransform.Set(level); state.switcherTransform.Set(Call(switcher, "get_transform"));
        state.headerCanvas.Set(headerCanvas); state.contentCanvas.Set(contentCanvas);
        state.headerFade.Set(headerFade);
        return true;
    }
    inline bool CanvasCamera(void* canvas, void*& camera) {
        camera = nullptr;
        if (Unbox<int>(Call(canvas, "get_renderMode")) == 0) return true;
        camera = Call(canvas, "get_worldCamera");
        return Alive(camera);
    }
    inline bool HeaderPoint(State& state, void* source, V3 point, void* parent, V2& local) {
        void* sourceCamera = nullptr; void* targetCamera = nullptr;
        if (!CanvasCamera(state.headerCanvas.Get(), sourceCamera) ||
            !CanvasCamera(state.contentCanvas.Get(), targetCamera)) return false;
        auto world = Unbox<V3>(Call(source, "TransformPoint", {&point}));
        static auto utility = Class("UnityEngine.UIModule.dll", "UnityEngine", "RectTransformUtility");
        static auto toScreen = Method(utility, "WorldToScreenPoint", 2);
        static auto toLocal = Method(utility, "ScreenPointToLocalPointInRectangle", 4);
        auto screen = Unbox<V2>(Invoke(toScreen, nullptr, {sourceCamera, &world}));
        return Unbox<bool>(Invoke(toLocal, nullptr, {parent, &screen, targetCamera, &local}));
    }
    inline bool PositionBubble(State& state) {
        auto transform = Call(state.root.Get(), "get_transform");
        auto parent = Call(transform, "get_parent");
        auto home = state.views[HomeSlot].Get();
        if (!Alive(home) || !BindLayout(state, parent, home)) return false;
        auto content = Unbox<Rect>(Call(parent, "get_rect"));
        auto level = state.levelTransform.Get(), switcher = state.switcherTransform.Get();
        auto switcherParent = Unbox<Rect>(Call(Call(switcher, "get_parent"), "get_rect"));
        auto levelRect = Unbox<Rect>(Call(level, "get_rect"));
        auto headerRoot = Call(level, "get_parent");
        auto headerRect = Unbox<Rect>(Call(headerRoot, "get_rect"));
        auto switcherRect = Unbox<Rect>(Call(switcher, "get_rect"));
        if (content.width <= 0 || content.height <= 0 || switcherParent.height <= 0 ||
            headerRect.width <= 0 || levelRect.height <= 0 || switcherRect.height <= 0) return false;
        V2 levelBottom{}, headerLeft{};
        if (!HeaderPoint(state, level, {levelRect.x, levelRect.y, 0}, parent, levelBottom) ||
            !HeaderPoint(state, headerRoot, {headerRect.x + state.leftOffset, headerRect.y + headerRect.height, 0}, parent, headerLeft))
            return false;
        auto anchorMin = Unbox<V2>(Call(switcher, "get_anchorMin"));
        auto anchorMax = Unbox<V2>(Call(switcher, "get_anchorMax"));
        auto switcherPivot = Unbox<V2>(Call(switcher, "get_pivot"));
        auto switcherPosition = Unbox<V2>(Call(switcher, "get_anchoredPosition"));
        // Verified HomePage/PageRoot/ContentRoot/LeftRoot all fill the adapted
        // content rect. Read the button's own layout and rebase its parent rect,
        // excluding ancestor slide/scale animations even while Home is hidden.
        const float switcherTop = RebaseY(switcherParent, content,
            ButtonTop(switcherParent, switcherRect, anchorMin, anchorMax, switcherPivot, switcherPosition));
        auto box = DialogueBox(headerLeft.x, levelBottom.y, switcherTop, state.currentBubbleWidth,
            state.textHeight, state.fontSize, state.textPadding.y);
        if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.height)) return false;
        if (state.hasPosition && content.x == state.positionedOrigin.x && content.y == state.positionedOrigin.y &&
            std::abs(box.x - state.positionedBox.x) < 0.01f &&
            std::abs(box.y - state.positionedBox.y) < 0.01f && box.width == state.positionedBox.width &&
            box.height == state.positionedBox.height) return true;
        V2 anchor{0, 0}, pivot{0, 0.5f}, size{box.width, box.height};
        V2 position{box.x - content.x, box.y + box.height * 0.5f - content.y};
        Call(transform, "set_anchorMin", {&anchor}); Call(transform, "set_anchorMax", {&anchor});
        Call(transform, "set_pivot", {&pivot}); Call(transform, "set_sizeDelta", {&size});
        Call(transform, "set_anchoredPosition", {&position});
        state.positionedBox = box; state.positionedOrigin = {content.x, content.y}; state.hasPosition = true;
        return true;
    }
    inline void MeasureSubtitle(State& state, const std::string& subtitle) {
        float unlimited = std::numeric_limits<float>::infinity();
        // Natural width uses the longest explicit line, measured in the game's
        // actual font. Mixed full/half-width text is not estimated by character count.
        auto natural = Unbox<V2>(Call(state.label.Get(), "GetPreferredValues", {Text(subtitle), &unlimited, &unlimited}));
        if (!std::isfinite(natural.x) || natural.x < 0) throw std::runtime_error("invalid preferred subtitle width");
        state.currentBubbleWidth = TextBubbleWidth(natural.x, state.textPadding.x, state.minBubbleWidth, state.bubbleWidth);
        float width = state.currentBubbleWidth - state.textPadding.x;
        if (width <= 0) throw std::runtime_error("subtitle padding exceeds its available width");
        float height = std::numeric_limits<float>::infinity();
        // Measure height again at the final body width, preserving explicit
        // newlines and automatically wrapping when the maximum width is reached.
        auto preferred = Unbox<V2>(Call(state.label.Get(), "GetPreferredValues", {Text(subtitle), &width, &height}));
        if (!std::isfinite(preferred.y) || preferred.y < 0) throw std::runtime_error("invalid preferred subtitle height");
        state.textHeight = preferred.y;
    }

    inline void DiscoverTemplate(State& state) {
        if (Alive(state.nativeTemplate.Get())) return;
        // DMM ResourceManager maps "verticaladv" to VerticalAdv. Its presenter
        // references a VerticalADVEngine prefab containing MessageContainer.
        // Loading these assets does not instantiate or start the story engine.
        auto resources = Core("Resources");
        auto prefab = Invoke(Overload(resources, "Load", {"System.String", "System.Type"}), nullptr,
            {Text("verticaladv"), Type(Core("GameObject"))});
        if (!Alive(prefab)) throw std::runtime_error("native verticaladv resource missing");
        GcRef prefabRef; prefabRef.Set(prefab);
        auto presenter = ChildComponent(prefab, Class("Assembly-CSharp.dll", "Campus.Common", "VerticalAdvPresenter"));
        if (!Alive(presenter)) throw std::runtime_error("native VerticalAdvPresenter missing");
        auto engine = Field<void*>(presenter, "_enginePrefab");
        if (!Alive(engine)) throw std::runtime_error("native VerticalADVEngine prefab missing");
        auto candidate = ChildComponent(Call(engine, "get_gameObject"), Class("ADV.Runtime.dll", "Campus.ADV", "MessageContainer"));
        if (!Alive(candidate)) throw std::runtime_error("native dialogue MessageContainer missing, including inactive children");
        state.nativeTemplate.Set(candidate);
    }

    inline bool Build(State& state) {
        ReleaseUi(state);
        auto view = state.views[state.page].Get(), home = state.views[HomeSlot].Get();
        if (!Alive(view) || !Alive(home)) throw std::runtime_error("native home view no longer alive");
        auto parent = ContentParent(home);
        // Retry a loading-stage header/camera instead of disabling subtitles.
        if (!BindLayout(state, parent, home)) return false;
        DiscoverTemplate(state);
        auto source = Call(state.nativeTemplate.Get(), "get_gameObject");
        if (!Alive(source)) throw std::runtime_error("native dialogue display tree missing");
        auto root = NewObject("HomeVoiceSubtitleRoot", parent); state.root.Set(root);
        auto rootTransform = Call(root, "get_transform"); Call(rootTransform, "SetAsLastSibling");
        bool yes = true, no = false;
        auto layoutElement = Add(root, Ui("LayoutElement")); Call(layoutElement, "set_ignoreLayout", {&yes});
        auto group = Add(root, Class("UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup"));
        state.opacity.Set(group);
        Call(group, "set_blocksRaycasts", {&no}); Call(group, "set_interactable", {&no});
        // Let the original dialogue's TMP/Ruby Awake finish before adjusting text.
        float opacity = 0; Call(group, "set_alpha", {&opacity}); Call(root, "SetActive", {&yes});
        // Copy only the native MessageContainer and its display children.
        // The ADV engine and presenter remain uninstantiated prefab assets.
        auto instantiate = Overload(Core("Object"), "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform", "System.Boolean"});
        auto bubble = Invoke(instantiate, nullptr, {source, rootTransform, &no});
        Call(bubble, "set_name", {Text("HomeVoiceNativeDialogueBubble")});
        auto dialogue = ChildComponent(bubble, *static_cast<void**>(state.nativeTemplate.Get()));
        if (!Alive(dialogue)) throw std::runtime_error("cloned dialogue MessageContainer missing");
        state.bubble.Set(dialogue); Call(dialogue, "Show");
        auto bubbleTransform = Call(bubble, "get_transform");
        auto message = Field<void*>(dialogue, "_messageText");
        if (!Alive(message)) throw std::runtime_error("cloned dialogue MessageText missing");
        auto label = Call(message, "get_TargetTMP");
        if (!Alive(label)) throw std::runtime_error("native dialogue body TMP missing");
        state.label.Set(label);
        // Retain the story dialogue's font and text colour. The rounded native
        // background/frame use a plain white UI material. No arrow is added.
        Call(label, "set_enableAutoSizing", {&no}); Call(label, "set_fontSize", {&state.fontSize});
        Call(label, "set_enableWordWrapping", {&yes}); int overflow = 0; Call(label, "set_overflowMode", {&overflow});
        Call(label, "set_richText", {&no}); Call(label, "set_raycastTarget", {&no});
        // DMM TMPro.TextAlignmentOptions.Left = 513: horizontal Left, vertical Middle.
        int leftMiddle = 513; Call(label, "set_alignment", {&leftMiddle});
        Stretch(bubble);
        auto animationRoot = ChildObject(bubbleTransform, "Animation Root");
        if (!Alive(animationRoot)) throw std::runtime_error("native dialogue Animation Root missing");
        Stretch(animationRoot);
        auto animationTransform = Call(animationRoot, "get_transform");
        for (const char* path : {"Message Background", "Message Background/Vertical", "Message Background/Vertical/Message Image"}) {
            auto child = ChildObject(animationTransform, path);
            if (!Alive(child)) throw std::runtime_error(std::string("native dialogue display child missing: ") + path);
            Stretch(child);
        }
        RoundedBackground(animationTransform);
        for (const char* path : {"Speaker Image", "Name Container", "Message Background/Vertical/Break Icon Container"}) {
            auto child = ChildObject(animationTransform, path); if (Alive(child)) Call(child, "SetActive", {&no});
        }
        Stretch(Call(label, "get_gameObject"), state.textPadding);
        auto groups = static_cast<ArrayHeader*>(Invoke(Overload(Core("GameObject"), "GetComponentsInChildren", {"System.Type", "System.Boolean"}),
            bubble, {Type(Class("UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup")), &yes}));
        if (!groups || groups->length > 32) throw std::runtime_error("invalid native canvas group array");
        auto entries = reinterpret_cast<void**>(groups + 1); float visible = 1;
        for (uintptr_t i = 0; i < groups->length; ++i) {
            Call(entries[i], "set_alpha", {&visible}); Call(entries[i], "set_interactable", {&no}); Call(entries[i], "set_blocksRaycasts", {&no});
        }
        Call(bubble, "SetActive", {&yes});
        state.textHeight = 0; state.currentBubbleWidth = state.minBubbleWidth;
        PositionBubble(state);
        Call(root, "SetActive", {&no}); opacity = 1; Call(group, "set_alpha", {&opacity});
        return true;
    }

    inline void ApplyEvents(State& state, std::deque<Event> events) {
        for (auto& event : events) {
            switch (event.kind) {
            case EventKind::ViewReady: {
                const int page = event.page < 0 ? HomeSlot : event.page;
                if (page >= 4) break;
                if (state.views[page].Get() != event.object.Get()) {
                    if (page == state.page || page == HomeSlot) {
                        Hide(state, "view changed"); ReleaseUi(state);
                        state.rebuild = state.buildDeferred = true; state.uiFailed = false;
                    }
                    state.views[page] = std::move(event.object);
                }
                // A null loading-stage event must still defer all UI work.
                if (!state.root.Get()) state.rebuild = state.buildDeferred = true;
                break;
            }
            case EventKind::ActorReady: {
                if (event.page < 0 || event.page >= 4) break;
                auto& actor = state.actors[event.page];
                if (event.page == state.page && actor.Get() != event.object.Get()) Hide(state, "actor changed");
                actor = std::move(event.object); state.buildDeferred = true;
                break;
            }
            case EventKind::TemplateReady:
                if (state.nativeTemplate.Get() != event.object.Get()) {
                    Hide(state, "native template changed"); ReleaseUi(state);
                    state.nativeTemplate = std::move(event.object); state.rebuild = state.buildDeferred = true; state.uiFailed = false;
                }
                break;
            case EventKind::PageShown: {
                const int page = event.page < 0 ? HomeSlot : event.page;
                if (page >= 4) break;
                if (state.page != page) {
                    Hide(state, "page changed"); ReleaseUi(state);
                    state.page = page; state.rebuild = true; state.uiFailed = false;
                }
                state.pageVisible = true; state.buildDeferred = true;
                break;
            }
            case EventKind::PageHidden:
                // An outgoing tab can finish its animation after the new tab shows.
                if (event.page >= 0 && event.page != state.page) break;
                if (event.page < 0) state.uiHidden = false;
                state.pageVisible = false; Hide(state, "page hidden");
                break;
            case EventKind::ScreenDestroyed:
                state.pageVisible = state.uiHidden = false; Hide(state, "screen destroyed"); ReleaseUi(state);
                for (auto& view : state.views) view.Set();
                for (auto& actor : state.actors) actor.Set();
                state.nativeTemplate.Set(); state.rebuild = false;
                break;
            case EventKind::UiHidden:
                state.uiHidden = true; state.pending = false;
                // Retain the displayed text/player while temporarily transparent.
                // A queued, unshown voice must never appear in FreeLook.
                if (!state.shown) state.player.Set();
                break;
            case EventKind::UiRestored:
                // Tick checks whether this voice is still playing and follows
                // the same native alpha during the UI's return animation.
                state.uiHidden = false;
                break;
            case EventKind::Played: {
                auto expectedActor = state.actors[state.page].Get(); auto boundActor = event.actor.Get();
                bool matches = ActorMatches(expectedActor, boundActor);
                if (state.pageVisible && matches && event.object.Get()) {
                    if (state.uiHidden) {
                        // A reused player now represents another utterance;
                        // its playing state must not revive the previous text.
                        if (state.shown && state.player.Get() == event.object.Get()) state.stopRequested = true;
                        break;
                    }
                    state.player = std::move(event.object); state.cue = std::move(event.cue); ++state.sequence;
                    state.pending = true; state.stopRequested = false;
                }
                break;
            }
            case EventKind::Stopped:
                if (state.player.Get() == event.object.Get()) state.stopRequested = true;
                break;
            }
        }
    }

    inline void Tick() {
        auto& state = Data();
        if (!state.enabled || Inbox().mainThread != CurrentThreadId() || inTick) return;
        // Unity can pump a nested PlayerLoop during loading. Never run UI work
        // twice on the same stack, and never wait for a native lock inside it.
        inTick = true;
        struct ExitTick { ~ExitTick() { inTick = false; } } exitTick;
        auto now = std::chrono::steady_clock::now(); if (now < state.nextTick) return;
        state.nextTick = now + std::chrono::milliseconds(16);
        try {
            ApplyEvents(state, TakeEvents());
            // Wait for game scene readiness, not the optional navigation hint.
            if (!state.pageVisible) { if (state.shown || state.pending) Hide(state, "page hidden"); return; }
            if (state.uiHidden) {
                if (state.shown) {
                    if (!Alive(state.root.Get())) Hide(state, "hidden UI no longer alive");
                    else {
                        const float alpha = FollowNativeUiFade(state);
                        if (alpha < 0) Hide(state, "native UI no longer alive");
                        else UpdatePlaybackVisibility(state, !Alive(state.player.Get()) ||
                            Unbox<bool>(Invoke(state.isEndOrStop, state.player.Get())), alpha);
                    }
                }
                return;
            }
            if (state.rebuild && !state.uiFailed && Alive(state.views[state.page].Get())) {
                if (CanBuild(state.pageVisible, false, true, Alive(state.actors[state.page].Get()), Alive(state.views[HomeSlot].Get()))) {
                    if (state.buildDeferred) { state.buildDeferred = false; return; }
                    if (!Build(state)) return;
                    state.rebuild = false;
                }
            }
            if (!state.pageVisible || state.stopRequested) { if (state.shown || state.pending) Hide(state, "page hidden/stop"); return; }
            if (state.uiFailed || !Alive(state.root.Get())) return;
            const float alpha = (state.shown || state.pending) ? FollowNativeUiFade(state) : 1;
            if (alpha < 0) { Hide(state, "native UI no longer alive"); return; }
            if (state.pending) {
                auto found = state.subtitles.find(state.cue);
                if (found == state.subtitles.end() && !state.showCueWhenMissing) { Hide(state, "subtitle missing"); return; }
                auto subtitle = found == state.subtitles.end() ? TestSubtitle(state.cue) : found->second;
                if (!HasSubtitleText(subtitle)) { Hide(state, "subtitle empty"); return; }
                MeasureSubtitle(state, subtitle);
                if (!PositionBubble(state)) return;
                state.pending = false;
                bool active = true; Call(state.root.Get(), "SetActive", {&active});
                // Use the native dialogue text entry point, with all characters
                // visible immediately instead of starting its story typewriter.
                bool isUser = false;
                Call(state.bubble.Get(), "SetMessage", {Text(subtitle), &isUser});
                Call(Field<void*>(state.bubble.Get(), "_messageText"), "CompleteAnimation");
                state.shown = true;
                // One line per displayed cue: proves the subtitle path is live
                // and records which text the translation actually produced.
                Log::InfoFmt("[HomeVoice] subtitle shown cue=%s text=%s", state.cue.c_str(), subtitle.c_str());
            }
            if (state.shown) {
                UpdatePlaybackVisibility(state, !Alive(state.player.Get()) ||
                    Unbox<bool>(Invoke(state.isEndOrStop, state.player.Get())), alpha);
                if (state.shown) PositionBubble(state); // Follow native safe-area/size updates.
            }
        } catch (const std::exception& error) {
            Log::ErrorFmt("[HomeVoice] UI error: %s", error.what()); state.uiFailed = true;
            state.pending = state.shown = false; state.player.Set();
            try { if (Alive(state.root.Get())) { bool active = false; Call(state.root.Get(), "SetActive", {&active}); } } catch (...) {}
        }
    }

    using VoidFn = void (*)(void*, void*);
    using ToggleFn = void (*)(void*, bool, int, void*);
    using FreeLookFn = void (*)(void*, bool, void*);
    using PlayWithFn = void (*)(void*, void*, bool, void*);
    using PlayedFn = void (*)(void*, String*, void*);
    inline VoidFn viewInitializeOrig{}, actorLoadOrig{}, topActorLoadOrig{}, screenDestroyOrig{}, screenInactiveOrig{};
    inline VoidFn visitStartOrig{}, visitEndOrig{};
    inline ToggleFn pageToggleOrig{};
    inline FreeLookFn freeLookOrig{};
    inline VoidFn playOrig{}, stopOrig{}, resetOrig{};
    inline PlayWithFn playWithOrig{};
    inline PlayedFn playedOrig{};
    struct PlayContext { void* player; void* actor; };
    inline thread_local PlayContext playing{};
    inline PlayContext ContextForPlay(void* player, PlayContext previous) {
        return {player, previous.player == player ? previous.actor : nullptr};
    }

    inline void ViewInitializeHook(void* self, void* method) {
        viewInitializeOrig(self, method);
        const int page = PageOf(self); if (page < 0) return;
        Inbox().mainThread = CurrentThreadId(); Post(Event(EventKind::ViewReady, self, nullptr, {}, page));
    }
    inline void PageToggleHook(void* self, bool shown, int direction, void* method) {
        pageToggleOrig(self, shown, direction, method);
        const int page = PageOf(self);
        if (page >= 0) {
            if (shown) Post(Event(EventKind::ViewReady, self, nullptr, {}, page));
            Post(Event(shown ? EventKind::PageShown : EventKind::PageHidden, nullptr, nullptr, {}, page));
        } else if (shown) Post(Event(EventKind::PageHidden));
    }
    inline void CaptureVisit(void* self, const char* phase) {
        try {
            // Returning from Present/Idol detail may leave _isShow unchanged and
            // never fire OnToggle. Rebind the current page on every screen visit.
            auto presenter = Invoke(Data().getCurrentPage, self);
            auto view = Field<void*>(presenter, "_view"); const int page = PageOf(view);
            if (page >= 0) {
                Inbox().mainThread = CurrentThreadId();
                Post(Event(EventKind::ViewReady, view, nullptr, {}, page));
                Post(Event(EventKind::PageShown, nullptr, nullptr, {}, page));
            } else Post(Event(EventKind::PageHidden));
        } catch (const std::exception& error) { Log::ErrorFmt("[HomeVoice] screen visit error phase=%s: %s", phase, error.what()); }
    }
    inline void VisitStartHook(void* self, void* method) { visitStartOrig(self, method); CaptureVisit(self, "start"); }
    inline void VisitEndHook(void* self, void* method) {
        visitEndOrig(self, method);
        // OnScreenVisitEnd completes entry. HomePagePresenter uses it to show
        // navigation hints; it does not mean the user has left HomeTopScreen.
        CaptureVisit(self, "end");
    }
    inline void LeavePage(bool destroy) {
        Post(Event(destroy ? EventKind::ScreenDestroyed : EventKind::PageHidden));
    }
    inline void ScreenInactiveHook(void* self, void* method) { LeavePage(false); screenInactiveOrig(self, method); }
    inline void ScreenDestroyHook(void* self, void* method) { LeavePage(true); screenDestroyOrig(self, method); }
    inline void FreeLookHook(void* self, bool enabled, void* method) {
        // Post before the original so any synchronous voice callback observes
        // the mode change. Unity UI work remains on the normal PlayerLoop tick.
        Post(Event(enabled ? EventKind::UiHidden : EventKind::UiRestored));
        freeLookOrig(self, enabled, method);
    }
    inline void CaptureActor(void* self) {
        try {
            const int page = PageOf(self); if (page < 0) return;
            auto& state = Data(); auto actor = Invoke(state.getActor, self);
            Post(Event(EventKind::ActorReady, actor, nullptr, {}, page));
        } catch (const std::exception& error) { Log::ErrorFmt("[HomeVoice] actor error: %s", error.what()); }
    }
    inline void ActorLoadHook(void* self, void* method) { actorLoadOrig(self, method); CaptureActor(self); }
    inline void TopActorLoadHook(void* self, void* method) { topActorLoadOrig(self, method); CaptureActor(self); }
    inline void PlayWithHook(void* self, void* actor, bool lipSync, void* method) {
        auto previous = playing; playing = {self, actor};
        playWithOrig(self, actor, lipSync, method); playing = previous;
    }
    inline void PlayHook(void* self, void* method) {
        // DMM may call Play from PlayWith: retain the outer actor for this player.
        auto previous = playing; playing = ContextForPlay(self, previous); playOrig(self, method); playing = previous;
    }
    inline void PlayedHook(void* self, String* cueName, void* method) {
        if (cueName) {
            auto cue = cueName->ToString();
            if (cue.starts_with("sud_vo_") && cue.find("_home_") != std::string::npos) {
                Post(Event(EventKind::Played, playing.player, playing.actor, std::move(cue)));
            }
        }
        playedOrig(self, cueName, method);
    }
    inline void RequestStop(void* player) {
        Post(Event(EventKind::Stopped, player));
    }
    inline void StopHook(void* self, void* method) { RequestStop(self); stopOrig(self, method); }
    inline void ResetHook(void* self, void* method) { RequestStop(self); resetOrig(self, method); }

    template<class Fn> inline bool InstallOne(HookInstaller* installer, void* klass, const char* name, int count, Fn hook, Fn& original) {
        try {
            auto method = Method(klass, name, count); auto address = *static_cast<void**>(method);
            if (!address) throw std::runtime_error("method pointer is null");
            auto status = installer->InstallHook(address, reinterpret_cast<void*>(hook), reinterpret_cast<void**>(&original));
#ifdef GKMS_WINDOWS
#ifdef GKMS_HOSTED
            // The loader's hook service enables the detour on success; there is
            // no second enable step and no private MinHook instance here.
            if (status) throw std::runtime_error("host hook status " + std::to_string(static_cast<int>(reinterpret_cast<intptr_t>(status))));
#else
            if (status) throw std::runtime_error(std::string("MinHook: ") + MH_StatusToString(static_cast<MH_STATUS>(reinterpret_cast<uintptr_t>(status))));
            const auto enabled = MH_EnableHook(address);
            if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
                throw std::runtime_error(std::string("enable MinHook: ") + MH_StatusToString(enabled));
#endif
#else
            if (!status) {
                auto message = shadowhook_to_errmsg(shadowhook_get_errno());
                throw std::runtime_error(std::string("ShadowHook: ") + (message ? message : "unknown hook error"));
            }
#endif
            return true;
        } catch (const std::exception& error) { Log::ErrorFmt("[HomeVoice] hook failed %s: %s", name, error.what()); return false; }
    }

    inline void ReloadSource() {
        auto& state = Data();
        state.enabled = true; state.showCueWhenMissing = false;
        state.fontSize = 32; state.textPadding = {40, 40};
        state.leftOffset = 40; state.bubbleWidth = 520; state.minBubbleWidth = 424;
        state.subtitles.clear();
        try {
            const auto path = LocalifyData::Find("local-files/genericTrans/genericTrans/lyrics/home_voice_subtitles.json");
            std::ifstream stream(path);
            if (stream) {
                auto config = nlohmann::json::parse(stream);
                state.enabled = config.value("enabled", true);
                state.showCueWhenMissing = config.value("showCueWhenMissing", false);
                state.fontSize = config.value("fontSize", 32.0f);
                if (config.contains("textPadding")) {
                    const auto& padding = config.at("textPadding");
                    state.textPadding = {padding.at(0).get<float>(), padding.at(1).get<float>()};
                }
                if (!std::isfinite(state.fontSize) || state.fontSize <= 0 ||
                    !std::isfinite(state.textPadding.x) || !std::isfinite(state.textPadding.y) || state.textPadding.x < 0 || state.textPadding.y < 0)
                    throw std::runtime_error("invalid subtitle fontSize/textPadding");
                // Old screenRect values are ignored: placement now follows
                // native adapted UI rather than full-screen ratios.
                state.leftOffset = config.value("leftOffset", 40.0f);
                state.bubbleWidth = config.value("bubbleWidth", 520.0f); // Maximum outer width.
                // Older configs may specify a fixed width below the new default minimum.
                state.minBubbleWidth = config.value("minBubbleWidth", (std::min)(424.0f, state.bubbleWidth));
                if (!std::isfinite(state.leftOffset) || !std::isfinite(state.minBubbleWidth) || !std::isfinite(state.bubbleWidth) ||
                    state.minBubbleWidth <= state.textPadding.x || state.minBubbleWidth > state.bubbleWidth)
                    throw std::runtime_error("invalid subtitle leftOffset/minBubbleWidth/bubbleWidth");
                if (config.contains("subtitles")) state.subtitles = ParseSubtitles(config.at("subtitles"));
            }
        } catch (const std::exception& error) { state.enabled = false; Log::ErrorFmt("[HomeVoice] source reload error: %s", error.what()); }
    }

    inline void Install(HookInstaller* installer) {
        RuntimeInstaller() = installer;
        ReloadSource();
        auto& state = Data();
        try {
            if (!state.enabled) return;
            BindGcRuntime();
            auto actorBase = Class("Assembly-CSharp.dll", "Campus.OutGame", "HomeActorBase");
            auto player = Class("campus-submodule.Runtime.dll", "Campus.Common.Sound", "CampusSoundPlayer");
            state.getActor = Method(actorBase, "get_Actor", 0); state.isEndOrStop = Method(player, "IsEndOrStop", 0);
            auto pageGroup = Class("Assembly-CSharp.dll", "Campus.OutGame", "HomePageGroupPresenter");
            state.getCurrentPage = Method(pageGroup, "GetCurrentPage", 0);
            auto viewBase = Class("Assembly-CSharp.dll", "Campus.OutGame", "HomePageViewBase");
            InstallOne(installer, viewBase, "Initialize", 0, ViewInitializeHook, viewInitializeOrig);
            InstallOne(installer, viewBase, "OnToggle", 2, PageToggleHook, pageToggleOrig);
            InstallOne(installer, pageGroup, "OnScreenVisitStart", 0, VisitStartHook, visitStartOrig);
            InstallOne(installer, pageGroup, "OnScreenVisitEnd", 0, VisitEndHook, visitEndOrig);
            InstallOne(installer, actorBase, "OnLoadEnd", 0, ActorLoadHook, actorLoadOrig);
            InstallOne(installer, Class("Assembly-CSharp.dll", "Campus.OutGame", "HomeTopActor"), "OnLoadEnd", 0, TopActorLoadHook, topActorLoadOrig);
            auto screen = Class("Assembly-CSharp.dll", "Campus.OutGame", "HomeTopScreenPresenter");
            InstallOne(installer, screen, "OnDestroy", 0, ScreenDestroyHook, screenDestroyOrig);
            InstallOne(installer, screen, "OnOpenOutInactive", 0, ScreenInactiveHook, screenInactiveOrig);
            InstallOne(installer, Class("Assembly-CSharp.dll", "Campus.OutGame", "Home3DManager"), "SetFreeLookEnable", 1, FreeLookHook, freeLookOrig);
            InstallOne(installer, player, "PlayWith", 2, PlayWithHook, playWithOrig);
            InstallOne(installer, player, "Play", 0, PlayHook, playOrig);
            InstallOne(installer, player, "Stop", 0, StopHook, stopOrig);
            InstallOne(installer, player, "ResetState", 0, ResetHook, resetOrig);
            InstallOne(installer, Class("campus-submodule.Runtime.dll", "Campus.Common", "CampusSoundManager"), "OnSoundPlayed", 1, PlayedHook, playedOrig);
        } catch (const std::exception& error) { state.enabled = false; Log::ErrorFmt("[HomeVoice] initialization error: %s", error.what()); }
    }
}
