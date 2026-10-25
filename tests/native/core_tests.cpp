#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string>
#include "ghost/core/protocol.hpp"
#include "ghost/animation/hand_rig.hpp"
#include "ghost/core/interpolation.hpp"
#include "ghost/core/state.hpp"
#include "ghost/engine/object_lifetime.hpp"
#include "ghost/core/navigation_core.hpp"
#include "ghost/visual/ghost_model.hpp"
#include "ghost/ui/menu_layout.hpp"
#include "ghost/engine/transform_validation.hpp"
#include "fixtures/gait_fixture.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

void check(bool value, const char* label)
{
    if (!value)
        throw std::runtime_error(label);
}
void run_tests()
{
    using namespace ghost;
    ue::ObjectLifetime lifetime;
    int storage{};
    auto* address = reinterpret_cast<ue::Object*>(&storage);
    auto first = lifetime.track(address);
    auto copy = first;
    check(ue::ObjectLifetime::get(first) == address, "live object handle");
    std::thread deletion([&] { lifetime.forget(address); });
    deletion.join();
    check(!ue::ObjectLifetime::get(first) && !ue::ObjectLifetime::get(copy),
          "GC invalidates all handle copies");
    auto reused = lifetime.track(address);
    check(ue::ObjectLifetime::get(reused) == address && !ue::ObjectLifetime::get(first),
          "address reuse does not revive old handle");
    lifetime.clear();
    check(!ue::ObjectLifetime::get(reused), "shutdown invalidates remaining handles");
    std::string combat_columns = "\t0\t0\t0\t0\t0\t0\t0";
    for (int i = 0; i < 18; ++i)
        combat_columns += "\t0";
    const std::string row =
        "p\tPlayer\ts\t1\t/Game/Map\t10\t20\t90\t350\t100\t0\t0" + combat_columns + "\n";
    auto roster = parse_roster("AHGR3\tb\t1\t1\n" + row);
    check(roster && roster->players.size() == 1, "roster decode");
    check(!parse_roster("AHGR3\tb\t1\t0\n" + row), "count mismatch");
    check(!parse_roster("AHGR3\tb\t1\t1\np\tn\ts\t1\t/Game/Map\tnan\t0\t0\t0\t0\t0\t0" +
                        combat_columns + "\n"),
          "NaN rejected");
    check(!parse_roster("AHGR3\tb\t-1\t0\n"), "negative sequence");
    check(parse_roster("AHGR3\tb\t2\t0\n").has_value(), "empty roster");
    check(!parse_roster("AHGR2\tb\t2\t0\n"), "old protocol rejected");
    RosterFeed feed;
    check(feed.push(*roster, 1.0), "first bridge roster accepted");
    auto heartbeat = *roster;
    heartbeat.seq = 2;
    check(feed.push(heartbeat, 1.7) && feed.heartbeat == 1.7 && feed.sample_time == 1.0,
          "bridge heartbeat does not renew a peer sample");
    check(!feed.push(*roster, 1.8) && feed.heartbeat == 1.7, "old file sequence rejected");
    Track recreated;
    recreated.push(feed.roster.players.front(), feed.sample_time);
    check(!recreated.latest(1.8),
          "visibility reset does not make stale teleport coordinates fresh");
    heartbeat.players.clear();
    heartbeat.seq = 3;
    feed.push(heartbeat, 1.8);
    heartbeat = *roster;
    heartbeat.seq = 4;
    feed.push(heartbeat, 1.9);
    check(feed.sample_time == 1.0, "empty roster does not forget the peer sample age");
    heartbeat.session = "restartedBridge";
    heartbeat.seq = 1;
    feed.push(heartbeat, 2.1);
    check(feed.sample_time == 1.0, "bridge restart does not renew unchanged peer state");
    recreated.clear();
    recreated.push(feed.roster.players.front(), feed.sample_time);
    check(!recreated.evaluate(3.1), "recreated stale visual stays expired");
    heartbeat.seq = 2;
    heartbeat.players.front().seq = 5;
    feed.push(heartbeat, 3.2);
    check(feed.sample_time == 3.2, "new peer sequence renews freshness");
    heartbeat.seq = 3;
    heartbeat.players.front().seq = 4;
    heartbeat.players.front().position.x = 999;
    feed.push(heartbeat, 3.3);
    check(feed.sample_time == 3.2 && feed.roster.players.front().seq == 5 &&
              feed.roster.players.front().position.x != 999,
          "reordered peer sample cannot replace newer state");
    heartbeat.seq = 4;
    heartbeat.players.front().session = "newGame";
    heartbeat.players.front().seq = 1;
    feed.push(heartbeat, 3.4);
    check(feed.sample_time == 3.4 && feed.roster.players.front().seq == 1,
          "new game session accepts sequence reset");
    heartbeat.seq = 5;
    heartbeat.players.front().world = "/Game/Other";
    feed.push(heartbeat, 3.5);
    check(feed.sample_time == 3.5, "peer world change resets sample watermark");
    heartbeat.seq = 6;
    heartbeat.players.front().id = "rejoined";
    feed.push(heartbeat, 3.6);
    check(feed.sample_time == 3.6, "new peer identity resets sample watermark");
    Track track;
    State a = roster->players.front();
    a.position = {0, 0, 0};
    a.yaw = 350;
    track.push(a, 1.0);
    check(!track.push(a, 1.5), "duplicates do not refresh expiry");
    check(track.latest(1.7).has_value() && !track.latest(1.8),
          "teleport freshness is stricter than rendering");
    State b = a;
    b.seq = 2;
    b.position = {100, 0, 0};
    b.yaw = 10;
    track.push(b, 1.1);
    auto middle = track.evaluate(1.15);
    check(middle && std::abs(middle->position.x - 50) < 0.01, "interpolation midpoint");
    check(std::abs(angle_delta(middle->yaw, 0)) < 0.01, "shortest yaw arc");
    auto stalled = track.evaluate(2.0);
    check(stalled && std::abs(stalled->position.x - 115) < 0.01 && length(stalled->velocity) == 0,
          "bounded extrapolation");
    check(!track.evaluate(3.2), "stale ghost expires");
    a.session = "newsave";
    a.position = {5000, 0, 0};
    track.push(a, 4.0);
    check(track.evaluate(4.0)->position.x == 5000, "save transition snaps");
    b = a;
    b.seq = 2;
    b.position = {10000, 0, 0};
    track.push(b, 4.1);
    check(track.evaluate(4.1)->position.x == 10000, "teleport clears interpolation history");
    check(!track.latest(3.9), "clock rollback rejected");
    auto points = arrival_candidates(a, 45);
    check(std::abs(length(points[0] - a.position) - 160) < 0.01, "arrival offset from peer");
    check(!grounded_arrival({}, Vec3{0, 0, 1}, false, false, 90), "unloaded ground rejected");
    check(!grounded_arrival({}, Vec3{0, 0, 0.5f}, true, false, 90), "steep ground rejected");
    check(!grounded_arrival({}, Vec3{0, 0, 1}, true, true, 90), "embedded trace rejected");
    check(!grounded_arrival({}, Vec3{0, 0, 1}, true, false, 500), "invalid capsule rejected");
    check(grounded_arrival(Vec3{1, 2, 3}, Vec3{0, 0, 1}, true, false, 90)->z == 97,
          "capsule above floor");
    for (float floor : {-120.0f, 0.0f, 75.0f})
        for (float height : {90.0f, 85.0f, 65.0f, 45.0f, 90.0f})
        {
            const auto anchor = character_visual_anchor({12, 34, floor + height}, height);
            check(anchor.x == 12 && anchor.y == 34 && anchor.z - 90 == floor,
                  "capsule crouch and recovery leave the visual floor anchor unchanged");
        }
    check(character_visual_anchor({1, 2, 3}, 0).z == 3 &&
              character_visual_anchor({1, 2, 3}, std::numeric_limits<float>::quiet_NaN()).z == 3,
          "invalid capsule height retains the sampled location");
    TeleportCooldown cooldown;
    const float identity_scale[4]{1, 1, 1, 0};
    const float unused_lane[4]{1, 1, 1, std::numeric_limits<float>::quiet_NaN()};
    const float bad_axis[4]{1, 0, 1, 0};
    const float nonfinite_axis[4]{1, 1, std::numeric_limits<float>::quiet_NaN(), 0};
    check(unit_xyz_scale(identity_scale) && unit_xyz_scale(unused_lane),
          "weapon attachment accepts identity XYZ scale regardless of SIMD padding");
    check(!unit_xyz_scale(bad_axis) && !unit_xyz_scale(nonfinite_axis),
          "weapon attachment still rejects invalid spatial scale axes");
    for (auto viewport :
         {std::array{1280.0f, 720.0f}, std::array{1920.0f, 1080.0f}, std::array{2560.0f, 1080.0f},
          std::array{3440.0f, 1440.0f}, std::array{3840.0f, 2160.0f}})
    {
        const float expected = *menu_render_scale(viewport[0], viewport[1], 1);
        for (float dpi : {.5f, .75f, 1.0f, 1.5f, 2.0f, 3.0f})
        {
            const auto scale = menu_render_scale(viewport[0], viewport[1], dpi);
            check(scale && std::abs(*scale * dpi - expected) < .00001f,
                  "menu physical size is independent of game DPI");
            check(600 * (*scale) * dpi <= viewport[0] - 48 &&
                      640 * (*scale) * dpi <= viewport[1] - 48,
                  "menu fits viewport including ultrawide and high DPI");
            check(20 * (*scale) * dpi >= 16, "menu text retains readable pixel height");
        }
    }
    check(!menu_render_scale(0, 1080, 1) && !menu_render_scale(1920, 1080, 0),
          "menu rejects unavailable viewport/DPI");
    check(cooldown.consume(10) && !cooldown.consume(10.1) && cooldown.consume(12),
          "teleport cooldown");
    check(!character_model("/Game/ArbitraryAsset"),
          "model selection rejects arbitrary asset paths");
    auto motion = visual_motion(CharacterModel::StoryP3, 0, false);
    check(motion.model == CharacterModel::StoryP3 && !motion.moving,
          "story profile uses normal P3 while idle");
    motion = visual_motion(CharacterModel::StoryP3, 100, false);
    check(motion.model == CharacterModel::Runner && motion.moving,
          "story profile returns to runner for movement");
    motion = visual_motion(CharacterModel::StoryP3, 20, true);
    check(motion.model == CharacterModel::Runner && motion.moving,
          "deceleration retains runner inside hysteresis band");
    motion = visual_motion(CharacterModel::StoryP3, 20, false);
    check(motion.model == CharacterModel::StoryP3 && !motion.moving,
          "small idle speed does not flicker models");
    motion = visual_motion(CharacterModel::StoryP3, 14, true);
    check(motion.model == CharacterModel::StoryP3 && !motion.moving, "stopping restores story P3");
    check(visual_motion(CharacterModel::Runner, 0, false).model == CharacterModel::Runner,
          "runner profile remains runner at rest");
    PoseClock pose;
    check(pose.advance(10, 1, 1) == 0, "first animation sample starts at zero");
    check(std::abs(pose.advance(10.1, 1, 1) - 0.1f) < 0.0001f, "pose follows elapsed time");
    check(std::abs(pose.advance(10.2, 2, 1) - 0.3f) < 0.0001f, "pose speed scales with locomotion");
    check(std::abs(pose.advance(10.3, 0, 1) - 0.3f) < 0.0001f, "zero play rate freezes the pose");
    check(std::abs(pose.advance(10.0, 1, 1) - 0.3f) < 0.0001f,
          "clock rollback does not reverse pose");
    check(std::abs(pose.advance(20, 1, 1) - 0.55f) < 0.0001f,
          "hitches have a bounded animation step");
    check(std::abs(pose.advance(20.25, 2, 1) - 0.05f) < 0.0001f,
          "manual pose loops at clip boundary");
    PoseClock another_pose;
    check(another_pose.advance(20.25, 1, 1) == 0, "visuals have independent animation clocks");
    pose.reset();
    check(pose.advance(21, 1, 1) == 0, "clip changes reset animation time");
    bool invalid_clip = false;
    try
    {
        pose.advance(21, 1, 0);
    }
    catch (const std::runtime_error&)
    {
        invalid_clip = true;
    }
    check(invalid_clip, "invalid clip duration is rejected");
    const Vec3 anchor{10, 20, 90};
    const Vec3 root_offset{-48.12614f, 226.94295f, 0}; // Measured from the live story idle.
    auto anchored = story_mesh_origin(anchor, 0, root_offset);
    check(length(anchored + root_offset - anchor) < 0.0001f,
          "story root stays at anchor despite cutscene offset");
    anchored = story_mesh_origin(anchor, 90, root_offset);
    check(std::abs(anchored.x - (anchor.x + root_offset.y)) < 0.001f &&
              std::abs(anchored.y - (anchor.y - root_offset.x)) < 0.001f,
          "root compensation rotates with mesh yaw");
    anchored = story_mesh_origin(anchor, -90, Vec3{1, 2, 3});
    check(length(anchored - Vec3{8, 21, 87}) < 0.001f,
          "root compensation handles opposite yaw and height");
    check(length(story_mesh_origin(anchor, 149.469f, {}) - anchor) < 0.001f,
          "in-place clips keep their origin");
    const Vec3 second_root{70, -90, 4};
    check(length(story_mesh_origin(anchor, 0, second_root) + second_root - anchor) < 0.001f,
          "changing root pose does not move the intended anchor");
    bool invalid_root = false;
    try
    {
        story_mesh_origin(anchor, 0, Vec3{3000, 0, 0});
    }
    catch (const std::runtime_error&)
    {
        invalid_root = true;
    }
    check(invalid_root, "implausible root offsets fail closed");
    using namespace ghost::gait;
    const auto opposite = rotate(from_to({1, 0, 0}, {-1, 0, 0}), {1, 0, 0});
    check(length(opposite - Vec3{-1, 0, 0}) < 0.0001f,
          "procedural rotations handle antiparallel bones");
    for (const Vec3 target : {Vec3{0, 0, -70}, Vec3{0, 0, -150}, Vec3{}, Vec3{100, 20, -30}})
    {
        const auto solved = solve_limb({}, target, {1, 0, 0}, 40, 42);
        check(std::abs(length(solved.joint) - 40) < 0.001f &&
                  std::abs(length(solved.end - solved.joint) - 42) < 0.001f,
              "IK preserves limb lengths even outside reach");
    }
    check(foot_path(0.3f).lift == 0 && foot_path(0.8f).lift > 0,
          "feet distinguish stance and swing");
    check(std::abs(foot_path(0.62f - 0.00001f).travel - foot_path(0.62f + 0.00001f).travel) <
              0.001f,
          "foot path is continuous at liftoff");
    check(std::abs(foot_path(0.99999f).travel - foot_path(0).travel) < 0.001f,
          "foot path is continuous at touchdown");
    PoseCadence cadence;
    int updates = 0;
    for (int tick = 0; tick < 640; ++tick)
        if (cadence.due(tick / 64.0))
            ++updates;
    check(updates >= 299 && updates <= 301, "30 Hz pose cadence retains remainder at a 64 Hz tick");
    check(cadence.due(0), "pose cadence recovers from a clock reset");
    for (const bool left : {true, false})
    {
        const float sign = left ? -1.0f : 1.0f;
        std::array<BonePose, 15> fingers;
        for (int finger = 0; finger < 5; ++finger)
            for (int joint = 0; joint < 3; ++joint)
                fingers[finger * 3 + joint] = {
                    {static_cast<float>(2 - finger), sign * (4 + joint * 3.0f), 0}, {}};
        const BonePose wrist{{0, 0, 0}, {}};
        HandRig hand(wrist, fingers, left);
        auto relaxed_wrist = wrist;
        relaxed_wrist.q = hand.orientation({0, 0, 30}, wrist.p, {1, 0, 0});
        const auto posed = hand.evaluate(relaxed_wrist);
        const auto finger_direction = unit(posed[3].p - wrist.p);
        check(dot(finger_direction, Vec3{0, 0, -1}) > 0.999f,
              "wrist follows the forearm instead of retaining A-pose direction");
        const auto palm =
            cross(finger_direction, unit(posed[0].p - posed[9].p)) * (left ? -1.0f : 1.0f);
        check(dot(palm, Vec3{0, -sign, 0}) > 0.9f, "relaxed palms face inward on both hands");
        check(dot(posed[5].p - posed[3].p, palm) > 1, "fingers curl into their own palm");
        for (int i = 0; i < 15; ++i)
        {
            const auto parent = i % 3 ? posed[i - 1].p : wrist.p;
            const auto rest_parent = i % 3 ? fingers[i - 1].p : wrist.p;
            check(std::abs(length(posed[i].p - parent) - length(fingers[i].p - rest_parent)) <
                      0.001f,
                  "relaxed fingers preserve segment lengths");
        }
        for (float grasp : {-1.0f, 1.0f})
        {
            const auto closed = hand.evaluate(relaxed_wrist, grasp);
            for (int i = 0; i < 15; ++i)
            {
                const auto parent = i % 3 ? closed[i - 1].p : wrist.p;
                const auto rest_parent = i % 3 ? fingers[i - 1].p : wrist.p;
                check(std::abs(length(closed[i].p - parent) - length(fingers[i].p - rest_parent)) <
                          .001f,
                      "grasp and thumb opposition preserve finger segment lengths");
            }
        }
    }
    Rig rig(gait_fixture());
    Animator gait;
    for (int frame = 0; frame < 600; ++frame)
    {
        const Vec3 velocity =
            frame < 60 || frame > 450 ? Vec3{} : (frame < 300 ? Vec3{160, 0, 0} : Vec3{0, 160, 0});
        const auto frame_pose = gait.evaluate(rig, velocity, frame / 60.0);
        check(length(frame_pose[Pelvis].p - rig.rest[Pelvis].p) < 20,
              "authored gait has bounded pelvis motion");
        const float sole =
            std::min(sole_height(rig, frame_pose, FootL), sole_height(rig, frame_pose, FootR));
        check(sole > -.005f, "authored feet do not penetrate the floor");
        if (gait.has_support())
            check(std::abs(sole) < .005f, "heel or toe retains stance support");
        for (const auto chain :
             {std::array{ThighL, CalfL, FootL}, std::array{ThighR, CalfR, FootR},
              std::array{UpperArmL, LowerArmL, HandL}, std::array{UpperArmR, LowerArmR, HandR}})
            for (int i = 0; i < 2; ++i)
                check(std::abs(length(frame_pose[chain[i + 1]].p - frame_pose[chain[i]].p) -
                               length(rig.rest[chain[i + 1]].p - rig.rest[chain[i]].p)) < 0.005f,
                      "authored gait preserves calibrated bone lengths");
    }
    check(gait.movement_blend() < 0.001f, "stopping returns smoothly to idle");
    Animator reverse;
    for (int frame = 0; frame < 120; ++frame)
        reverse.evaluate(rig, {-160, 0, 0}, frame / 30.0);
    check(reverse.movement_direction().x < -0.99f,
          "exact backward motion reverses foot travel direction");
    check(visual_motion(CharacterModel::ProceduralP3, 300, true).model ==
              CharacterModel::ProceduralP3,
          "procedural P3 keeps the same body while moving");
    std::cout << "Core protocol/interpolation/lifetime/navigation/model tests passed\n";
}

int main()
{
    try
    {
        run_tests();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
