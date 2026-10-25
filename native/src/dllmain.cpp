#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <ios>
#include <locale>
#include <stdexcept>
#include <string>
#include <utility>
#include "ghost/core/protocol.hpp"
#include "ghost/core/interpolation.hpp"
#include "ghost/engine/ue4ss_abi.hpp"
#include "ghost/visual/ghost_visual.hpp"
#include "ghost/engine/game_tick.hpp"
#include "ghost/visual/ghost_navigation.hpp"
#include "ghost/visual/ghost_combat_sample.hpp"
#include "ghost/ui/ghost_menu.hpp"

#include "ghost/platform/shared_files.hpp"
#include "ghost/visual/pose_probe.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace
{
using Call = ue::Reflection::Call;
using ghost::State;
using ghost::Vec3;
using ghost::platform::read_shared;
using ghost::platform::write_atomic;
namespace fs = std::filesystem;

double clock_seconds()
{
    return static_cast<double>(GetTickCount64()) / 1000.0;
}
double precise_seconds()
{
    static const double frequency = []
    {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) / frequency;
}

enum Command : unsigned
{
    OnlineToggle = 8,
    Teleport = 32,
    MenuToggle = 128
};
class GhostMod final : public RC::CppUserModBase
{
    ue::Api api;
    ue::Reflection reflection{api};
    ghost::CombatSampler combat_sampler{api, reflection};
    ghost::Renderer renderer{api, reflection};
    ghost::Teleporter teleporter{api, reflection};
    ghost::ModMenu menu{api, reflection};
    GameTickHook game_tick;
    fs::path mod_dir, state_dir, log_path;
    std::atomic_bool initialized{false}, faulted{false};
    std::atomic_flag ticking = ATOMIC_FLAG_INIT;
    std::atomic<unsigned> commands{};
    std::atomic<double> teleport_requested{};
    std::atomic<DWORD> game_thread{};
    std::atomic<ULONGLONG> tick_heartbeat{};
    std::atomic<double> mean_tick_ms{}, max_tick_ms{}, tick_rate{};
    std::atomic<std::uint64_t> lookups_per_window{};
    double performance_since{}, tick_sum{}, tick_max{};
    std::uint64_t measured_ticks{}, last_lookup_count{};
    std::mutex exchange_mutex;
    ghost::RosterFeed incoming;
    std::string outgoing, status = "waiting for a game-thread tick", last_logged;
    std::uint64_t outgoing_revision{}, written_revision{};
    std::string command_session;
    std::uint64_t command_seq{};
    double last_io{}, last_status_io{}, last_command{};
    double last_frame{}, stable_since{}, last_publish{};
    std::string instance, world_key, peer_key;
    std::uint64_t world_generation{}, sample_seq{};
    ue::Weak current_world, current_pawn;
    ghost::Visual remote_visual;
    ghost::Track remote_track;
    bool online_enabled{true}, visual_failed{};
    ghost::PoseProbe pose_probe;
    std::atomic_bool pose_probe_enabled{};
    std::string pose_probe_pending;
    bool pose_probe_reset{};
    std::string action_message;
    double action_until{};

    void set_status(const std::string& message)
    {
        std::unique_lock lock(exchange_mutex, std::try_to_lock);
        if (lock.owns_lock())
            status = message;
    }
    void publish(const std::string& text)
    {
        std::unique_lock lock(exchange_mutex, std::try_to_lock);
        if (lock.owns_lock())
        {
            outgoing = text;
            ++outgoing_revision;
        }
    }
    struct Fault
    {
        DWORD code{};
        std::uintptr_t instruction{}, address{};
    };
    static int capture_exception(EXCEPTION_POINTERS* info, Fault* fault)
    {
        fault->code = info->ExceptionRecord->ExceptionCode;
        fault->instruction = info->ContextRecord->Rip;
        if (info->ExceptionRecord->NumberParameters > 1)
            fault->address = info->ExceptionRecord->ExceptionInformation[1];
        return EXCEPTION_EXECUTE_HANDLER;
    }
    static DWORD seh_guard(void (*function)(void*), void* context, Fault* fault)
    {
        __try
        {
            function(context);
            return 0;
        }
        __except (capture_exception(GetExceptionInformation(), fault))
        {
            return GetExceptionCode();
        }
    }
    struct Frame
    {
        GhostMod* self;
        ue::Object* context;
        bool engine;
    };
    void frame(ue::Object* context, bool engine = false)
    {
        if (!initialized.load(std::memory_order_acquire) || faulted.load() ||
            ticking.test_and_set())
            return;
        struct Release
        {
            std::atomic_flag& flag;
            ~Release()
            {
                flag.clear();
            }
        } release{ticking};
        DWORD expected = 0;
        game_thread.compare_exchange_strong(expected, GetCurrentThreadId());
        if (game_thread.load() != GetCurrentThreadId())
            return;
        tick_heartbeat = GetTickCount64();
        const double now = clock_seconds();
        if (now - last_frame < 0.008)
            return;
        last_frame = now;
        Frame data{this, context, engine};
        Fault fault;
        const double started = precise_seconds();
        DWORD exception = seh_guard(
            [](void* raw)
            {
                auto& f = *static_cast<Frame*>(raw);
                try
                {
                    auto* viewport = f.engine
                                         ? f.self->api.object_property(f.context, L"GameViewport")
                                         : f.context;
                    f.self->tick(viewport);
                }
                catch (const std::exception& error)
                {
                    try
                    {
                        f.self->menu.close();
                    }
                    catch (...)
                    {
                    }
                    f.self->faulted = true;
                    f.self->set_status(std::string("disabled: ") + error.what());
                    f.self->publish("");
                }
            },
            &data, &fault);
        const double elapsed = (precise_seconds() - started) * 1000.0;
        tick_sum += elapsed;
        tick_max = std::max(tick_max, elapsed);
        ++measured_ticks;
        if (!performance_since)
            performance_since = now;
        if (now - performance_since >= 2.0)
        {
            mean_tick_ms = tick_sum / static_cast<double>(measured_ticks);
            max_tick_ms = tick_max;
            tick_rate = static_cast<double>(measured_ticks) / (now - performance_since);
            lookups_per_window = api.slow_lookups - last_lookup_count;
            last_lookup_count = api.slow_lookups;
            measured_ticks = 0;
            tick_sum = 0;
            tick_max = 0;
            performance_since = now;
        }
        if (exception)
        {
            faulted = true;
            // After a native fault, no more Unreal calls or cleanup attempts in this session.
            MEMORY_BASIC_INFORMATION region{};
            VirtualQuery(reinterpret_cast<void*>(fault.instruction), &region, sizeof(region));
            char code[192];
            sprintf_s(
                code, "native fault 0x%08lX; instruction=%p; module_offset=0x%llX; address=%p",
                exception, reinterpret_cast<void*>(fault.instruction),
                static_cast<unsigned long long>(
                    fault.instruction - reinterpret_cast<std::uintptr_t>(region.AllocationBase)),
                reinterpret_cast<void*>(fault.address));
            set_status(std::string("disabled: ") + code + "; restart required; inspect native.log");
            publish("");
        }
    }
    ue::Object* player(ue::Object* world)
    {
        Call call(reflection, L"/Script/Engine.GameplayStatics:GetPlayerController");
        call.set(L"WorldContextObject", world)
            .set(L"PlayerIndex", 0)
            .run(api.require(L"/Script/Engine.Default__GameplayStatics"));
        return call.get<ue::Object*>();
    }
    void reset_world()
    {
        menu.close();
        renderer.remove(remote_visual);
        remote_track.clear();
        peer_key.clear();
        world_key.clear();
        current_world = {};
        current_pawn = {};
        visual_failed = false;
        ++world_generation;
        publish("");
    }
    State sample(ue::Object* controller, ue::Object* pawn)
    {
        Call position(reflection, L"/Script/Engine.Actor:K2_GetActorLocation");
        position.run(pawn);
        Call rotation(reflection, L"/Script/Engine.Controller:GetControlRotation");
        rotation.run(controller);
        Call velocity(reflection, L"/Script/Engine.Actor:GetVelocity");
        velocity.run(pawn);
        State result;
        result.session = instance + "-" + std::to_string(world_generation);
        result.seq = ++sample_seq;
        result.world = world_key;
        result.position = position.get<Vec3>();
        if (auto* capsule = api.object_property(pawn, L"CapsuleComponent"))
        {
            Call height(reflection, L"/Script/Engine.CapsuleComponent:GetScaledCapsuleHalfHeight");
            height.run(capsule);
            result.position = character_visual_anchor(result.position, height.get<float>());
        }
        result.velocity = velocity.get<Vec3>();
        result.yaw = rotation.get<ue::Rotation>().yaw;
        result.combat = combat_sampler.sample(pawn, result.yaw, rotation.get<ue::Rotation>().pitch,
                                              clock_seconds());
        return result;
    }
    void render(ghost::Visual& visual, ue::Object* world, const State& state, Vec3 viewer,
                ue::Object* controller)
    {
        if (!visual.ready || !api.weak_get(&visual.actor))
            renderer.create(visual, world, state);
        renderer.update(visual, state, viewer, controller, clock_seconds());
        if (!pose_probe_enabled.load())
            pose_probe.reset();
        else
        {
            try
            {
                if (auto sample = pose_probe.sample(api, reflection, visual, state,
                                                    state.position +
                                                        Vec3{0, 0, renderer.feet_for(visual.model)},
                                                    state.yaw + renderer.yaw_for(visual.model),
                                                    clock_seconds(), world_generation))
                {
                    std::scoped_lock lock(exchange_mutex);
                    pose_probe_pending = std::move(sample->text);
                    pose_probe_reset = pose_probe_reset || sample->first;
                }
            }
            catch (const std::exception& error)
            {
                pose_probe.disable();
                action_message = std::string("pose probe disabled: ") + error.what();
                action_until = clock_seconds() + 8;
            }
        }
    }
    void tick(ue::Object* viewport)
    {
        const double now = clock_seconds();
        ue::Object* world = viewport ? api.get_world(viewport) : nullptr;
        ue::Object* controller = world ? player(world) : nullptr;
        ue::Object* pawn = api.object_property(controller, L"Pawn");
        if (!world || !pawn)
        {
            commands.exchange(0);
            if (!world_key.empty())
                reset_world();
            publish("");
            set_status("ready; waiting for a loaded save and player pawn");
            return;
        }
        if (api.weak_get(&current_world) != world || api.weak_get(&current_pawn) != pawn)
        {
            reset_world();
            current_world = api.weak(world);
            current_pawn = api.weak(pawn);
            world_key = ue::utf8(api.get_name(api.outermost(world)));
            stable_since = now;
        }
        if (now - stable_since < 2.0 || !world_key.starts_with("/Game/") ||
            world_key.find("Menu") != std::string::npos)
        {
            commands.exchange(0);
            set_status("ready; waiting for world to settle: " + world_key);
            return;
        }
        State local = sample(controller, pawn);
        unsigned control = commands.exchange(0);
        try
        {
            if (control & MenuToggle)
                menu.toggle(controller);
            const std::string caption = "Online: " + std::string(online_enabled ? "ON" : "OFF") +
                                        (now < action_until ? "\n" + action_message : "");
            const int request = menu.update(caption);
            switch (request)
            {
            case 5:
                control |= OnlineToggle;
                break;
            case 6:
                menu.close();
                teleport_requested = now;
                control |= Teleport;
                break;
            default:
                break;
            }
        }
        catch (const std::exception& error)
        {
            action_message = std::string("menu: ") + error.what();
            action_until = now + 8;
            try
            {
                menu.close();
            }
            catch (...)
            {
            }
        }
        if (control & OnlineToggle)
        {
            online_enabled = !online_enabled;
            visual_failed = false;
        }

        ghost::Roster roster;
        double seen{}, sample_seen{};
        {
            std::unique_lock lock(exchange_mutex, std::try_to_lock);
            if (lock.owns_lock())
            {
                roster = incoming.roster;
                seen = incoming.heartbeat;
                sample_seen = incoming.sample_time;
            }
            else
                return;
        }
        const bool compatible = online_enabled && now - seen < 2.0 && !roster.players.empty() &&
                                roster.players.front().world == world_key;
        if (compatible)
        {
            const auto& remote = roster.players.front();
            const std::string identity = roster.session + "/" + remote.id + "/" + remote.session;
            if (identity != peer_key)
            {
                renderer.remove(remote_visual);
                remote_track.clear();
                peer_key = identity;
                visual_failed = false;
            }
            remote_track.push(remote, sample_seen);
        }
        else
        {
            renderer.remove(remote_visual);
            remote_track.clear();
            peer_key.clear();
        }
        if ((control & Teleport) && now - teleport_requested.load() < 0.5)
        {
            try
            {
                if (teleporter.attempt(pawn, controller, remote_track.latest(now), local, now,
                                       action_message))
                {
                    local = sample(controller, pawn);
                    last_publish = 0;
                }
            }
            catch (const std::exception& error)
            {
                action_message = std::string("teleport rejected: ") + error.what();
            }
            action_until = now + 8;
        }
        if (now - last_publish >= 0.05)
        {
            last_publish = now;
            std::ostringstream line;
            line.imbue(std::locale::classic());
            line << std::fixed << std::setprecision(3) << "AHG3\t" << local.session << '\t'
                 << local.seq << '\t' << world_key << '\t' << local.position.x << '\t'
                 << local.position.y << '\t' << local.position.z << '\t' << local.yaw << '\t'
                 << local.velocity.x << '\t' << local.velocity.y << '\t' << local.velocity.z << '\t'
                 << local.combat.weapon << '\t' << local.combat.pitch << '\t' << local.combat.flags
                 << '\t' << local.combat.ability << '\t' << local.combat.shot << '\t'
                 << local.combat.action << '\t' << static_cast<int>(local.combat.calibrated);
            for (float grip_value : local.combat.grip)
                line << '\t' << grip_value;
            line << '\n';
            publish(line.str());
        }
        if (visual_failed)
            return;
        try
        {
            auto remote = remote_track.evaluate(now);
            if (remote && ghost::length(remote->position - local.position) < 100000)
                render(remote_visual, world, *remote, local.position, controller);
            else
                renderer.remove(remote_visual);
            std::string message = "game ready; online=" + std::to_string(online_enabled) +
                                  "; remote=" + std::to_string(remote_visual.ready);
            message += "; model=" + std::string(ghost::model_assets(renderer.model).name);
            message += "; local_weapon=" + std::to_string(local.combat.weapon) +
                       "; grip=" + std::to_string(local.combat.calibrated);
            if (!combat_sampler.calibration_error.empty())
                message += "; grip_error=" + combat_sampler.calibration_error;
            if (!remote_visual.weapon_error.empty())
                message += "; remote_weapon_error=" + remote_visual.weapon_error;
            if (remote_visual.ready)
                message +=
                    "; remote_model=" + std::string(ghost::model_assets(remote_visual.model).name);
            if (!roster.players.empty() && !compatible)
                message += "; peer in another map or online hidden";
            message += "; world=" + world_key;
            if (now < action_until)
                message += "; " + action_message;
            set_status(message);
        }
        catch (const std::exception& error)
        {
            renderer.remove(remote_visual);
            visual_failed = true;
            set_status(std::string("visual disabled: ") + error.what() +
                       "; toggle online visibility in Home menu to retry");
        }
    }
    void poll_command(double now)
    {
        std::string line;
        if (!read_shared(state_dir / L"native_command.txt", line))
            return;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        auto fields = ghost::split(line, '\t');
        std::uint64_t seq{};
        if (fields.size() != 4 || fields[0] != "AHGC2" || !ghost::token(fields[1]) ||
            !ghost::sequence(fields[2], seq))
            return;
        if (fields[1] == command_session && seq <= command_seq)
            return;
        command_session = fields[1];
        command_seq = seq;
        if (now - last_command < 0.35)
            return;
        last_command = now;
        unsigned value = 0;
        if (fields[3] == "online_toggle")
            value = OnlineToggle;
        if (fields[3] == "menu_toggle")
            value = MenuToggle;
        if (fields[3] == "teleport")
        {
            teleport_requested = now;
            value = Teleport;
        }
        commands.fetch_or(value);
    }

public:
    GhostMod()
    {
        ModName = L"AHGhostCoopNative";
        ModVersion = L"0.9.2-preview";
        ModDescription = L"Two-player LAN ghosts";
        ModAuthors = L"tchas";
        ModIntendedSDKVersion = L"3.0.1";
        DWORD mode{};
        const auto input = GetStdHandle(STD_INPUT_HANDLE);
        if (GetConsoleMode(input, &mode))
            SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    }
    void on_unreal_init() override
    {
        try
        {
            std::wstring path(32768, L'\0');
            DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!count || count == path.size())
                throw std::runtime_error("game executable path unavailable");
            path.resize(count);
            const auto win64 = fs::path(path).parent_path();
            mod_dir = win64 / L"Mods" / L"AHGhostCoop";
            state_dir = mod_dir / L"state";
            log_path = win64 / L"Mods" / L"AHGhostCoopNative" / L"native.log";
            fs::create_directories(state_dir);
            fs::create_directories(log_path.parent_path());
            std::ofstream(log_path) << "AHGhostCoopNative 0.9.2-preview: concrete GameEngine "
                                       "post-tick, protocol AHG3\n";
            write_atomic(state_dir / L"local.tsv", "");
            DeleteFileW((state_dir / L"native_command.txt").c_str());
            DeleteFileW((state_dir / L"remote.tsv").c_str());
            instance =
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            auto setting = [&](const wchar_t* name, float fallback)
            {
                wchar_t buffer[64];
                GetPrivateProfileStringW(L"Visual", name, L"", buffer, 64,
                                         (mod_dir / L"settings.ini").c_str());
                if (!*buffer)
                    return fallback;
                wchar_t* end{};
                float value = std::wcstof(buffer, &end);
                return end != buffer && !*end && std::isfinite(value) && std::abs(value) < 1000
                           ? value
                           : fallback;
            };
            wchar_t model_name[64];
            GetPrivateProfileStringW(L"Visual", L"CharacterModel", L"procedural_p3", model_name, 64,
                                     (mod_dir / L"settings.ini").c_str());
            auto model = ghost::character_model(ue::utf8(model_name));
            if (!model)
                throw std::runtime_error(
                    "unknown Visual.CharacterModel (use runner, story_p3 or procedural_p3)");
            renderer.model = *model;
            renderer.feet_offset = setting(L"FeetOffset", -90);
            renderer.mesh_yaw_offset = setting(L"MeshYawOffset", 0);
            renderer.story_feet_offset = setting(L"StoryFeetOffset", -90);
            renderer.story_mesh_yaw_offset = setting(L"StoryMeshYawOffset", 0);
            renderer.procedural_feet_offset = setting(L"ProceduralFeetOffset", -90);
            renderer.procedural_mesh_yaw_offset = setting(L"ProceduralMeshYawOffset", 0);
            renderer.name_height = setting(L"NameHeight", 210);
            api.init();
            game_tick.install(api, this, [](void* self, ue::Object* engine)
                              { static_cast<GhostMod*>(self)->frame(engine, true); });
            std::ofstream(log_path, std::ios::app)
                << "GameEngine::Tick hooked at vtable slot " << game_tick.slot << '\n';
            initialized.store(true, std::memory_order_release);
        }
        catch (const std::exception& error)
        {
            faulted = true;
            set_status(std::string("initialization failed: ") + error.what());
        }
    }
    ~GhostMod() override
    {
        initialized = false;
        game_tick.stop();
    }
    void on_update() override
    {
        if (state_dir.empty())
            return;
        const double now = clock_seconds();
        if (now - last_io < 0.025)
            return;
        last_io = now;
        poll_command(now);
        std::string data;
        if (read_shared(state_dir / L"remote.tsv", data))
            if (auto roster = ghost::parse_roster(data))
            {
                std::scoped_lock lock(exchange_mutex);
                incoming.push(std::move(*roster), now);
            }
        std::string local, message;
        std::uint64_t revision{};
        std::string pose_report;
        bool reset_pose_report = false;
        {
            std::scoped_lock lock(exchange_mutex);
            local = outgoing;
            revision = outgoing_revision;
            message = status;
            pose_report = std::move(pose_probe_pending);
            pose_probe_pending.clear();
            reset_pose_report = pose_probe_reset;
            pose_probe_reset = false;
        }
        if (!pose_report.empty())
        {
            std::ofstream file(state_dir / L"native_pose_probe.jsonl",
                               reset_pose_report ? std::ios::trunc : std::ios::app);
            file << pose_report;
        }
        if (revision != written_revision && write_atomic(state_dir / L"local.tsv", local))
            written_revision = revision;
        if (now - last_status_io >= 0.5)
        {
            last_status_io = now;
            api.uncached_lookup = GetPrivateProfileIntW(L"Diagnostics", L"UncachedObjectLookup", 0,
                                                        (mod_dir / L"settings.ini").c_str()) != 0;
            pose_probe_enabled = GetPrivateProfileIntW(L"Diagnostics", L"PoseProbe", 0,
                                                       (mod_dir / L"settings.ini").c_str()) != 0;
            std::ostringstream performance;
            performance.imbue(std::locale::classic());
            performance << std::fixed << std::setprecision(3)
                        << "mod_tick_avg_ms=" << mean_tick_ms.load()
                        << "; mod_tick_max_ms=" << max_tick_ms.load()
                        << "; mod_ticks_per_second=" << tick_rate.load()
                        << "; slow_lookups_per_2s=" << lookups_per_window.load()
                        << "; lookup_cache=" << !api.uncached_lookup.load() << '\n';
            write_atomic(state_dir / L"native_performance.txt", performance.str());
            if (!faulted.load() &&
                (!tick_heartbeat.load() || GetTickCount64() - tick_heartbeat.load() > 5000))
                message = "waiting for GameEngine tick; game may be loading or paused";
            write_atomic(state_dir / L"native_status.txt", message + "\n");
            if (message != last_logged)
            {
                std::ofstream log(log_path, std::ios::app);
                log << GetTickCount64() << " " << message << '\n';
                last_logged = message;
            }
        }
    }
};
} // namespace

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new GhostMod();
}
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
