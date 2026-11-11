// Which platform actions wait for the roof and the automatic close (spec 010,
// contracts/platform-hold.md; constitution I: OTA, restarts and flash writes
// never overlap a move; III: nothing interrupts the close sequence needlessly;
// VIII: the open setup hotspot can't open the roof).

#include <initializer_list>
#include <unity.h>

#include "Config.h" // for the library finder: RoofIoLogic builds with ConfigModel
#include "CloseSequence.h"
#include "MotionGuards.h"
#include "PlatformHold.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const PlatformAction ALL_ACTIONS[] = {
        PlatformAction::FlashWrite,
        PlatformAction::FilesystemUpload,
        PlatformAction::Restart,
        PlatformAction::MandatoryRestart,
        PlatformAction::FirmwareUpload,
        PlatformAction::WifiChange,
        PlatformAction::WifiScan,
    };

    const Roof::State AT_REST[] = {
        Roof::State::Unknown,
        Roof::State::Closed,
        Roof::State::Open,
        Roof::State::Stopped,
        Roof::State::Error,
    };

    // Whether the close sequence holds this action (contracts/platform-hold.md).
    bool sequenceHolds(PlatformAction action)
    {
        return action == PlatformAction::Restart || action == PlatformAction::FirmwareUpload ||
               action == PlatformAction::FilesystemUpload || action == PlatformAction::WifiChange || action == PlatformAction::WifiScan;
    }

    // `sequenceWorking`: the sequence waits for the park it asked for, or closes.
    PlatformState with(Roof::State roof, bool closeWaiting = false, bool sequenceWorking = false)
    {
        PlatformState s;
        s.roof = roof;
        s.closeWaiting = closeWaiting;
        s.sequence = sequenceWorking ? SequenceState::Working : SequenceState::Idle;
        return s;
    }

    PlatformState in(SequenceState sequence)
    {
        PlatformState s;
        s.roof = Roof::State::Open;
        s.sequence = sequence;
        return s;
    }
} // namespace

void test_every_action_held_while_moving(void)
{
    for (PlatformAction action : ALL_ACTIONS)
        for (Roof::State moving : {Roof::State::Opening, Roof::State::Closing})
        {
            TEST_ASSERT_EQUAL_MESSAGE(HoldReason::Moving, platformHold(action, with(moving)), platformActionName(action));
            // Moving is named first, whatever else holds it.
            TEST_ASSERT_EQUAL(HoldReason::Moving, platformHold(action, with(moving, true, true)));
        }
}

void test_close_waiting_holds_every_action(void)
{
    for (PlatformAction action : ALL_ACTIONS)
        for (Roof::State rest : AT_REST)
        {
            TEST_ASSERT_EQUAL_MESSAGE(HoldReason::CloseWaiting, platformHold(action, with(rest, true)), platformActionName(action));
            TEST_ASSERT_EQUAL(HoldReason::CloseWaiting, platformHold(action, with(rest, true, true)));
        }
}

void test_sequence_busy_holds_restart_firmware_and_wifi(void)
{
    for (PlatformAction action : ALL_ACTIONS)
        for (Roof::State rest : AT_REST)
        {
            const HoldReason expected = sequenceHolds(action) ? HoldReason::CloseSequence : HoldReason::None;
            TEST_ASSERT_EQUAL_MESSAGE(expected, platformHold(action, with(rest, false, true)), platformActionName(action));
        }
}

void test_stand_downs_leave_the_way_out_open(void)
{
    // Backing off or stood down (unknown, fault, halted), the sequence may
    // wait for hours, often because the network is broken: a WiFi fix, a
    // restart or an update must stay possible.
    for (PlatformAction action : ALL_ACTIONS)
        TEST_ASSERT_EQUAL_MESSAGE(HoldReason::None, platformHold(action, in(SequenceState::Waiting)), platformActionName(action));
    // A person's stand-down only holds a new firmware (it might lose the stand-down).
    for (PlatformAction action : ALL_ACTIONS)
    {
        const HoldReason expected = action == PlatformAction::FirmwareUpload ? HoldReason::CloseSequence : HoldReason::None;
        TEST_ASSERT_EQUAL_MESSAGE(expected, platformHold(action, in(SequenceState::HeldByPerson)), platformActionName(action));
    }
    for (PlatformAction action : ALL_ACTIONS)
        TEST_ASSERT_EQUAL(HoldReason::None, platformHold(action, in(SequenceState::Idle)));
}

void test_working_sequence_without_network_leaves_the_way_out_open(void)
{
    // With the home network gone (or the setup hotspot on), waiting for the
    // park can't succeed: a restart or a WiFi fix must not wait for it. Only
    // uploads (a new firmware, a restart nothing cancels) still wait.
    PlatformState s = in(SequenceState::Working);
    s.networkUp = false;
    for (PlatformAction action : ALL_ACTIONS)
    {
        const bool upload = action == PlatformAction::FirmwareUpload || action == PlatformAction::FilesystemUpload;
        TEST_ASSERT_EQUAL_MESSAGE(
            upload ? HoldReason::CloseSequence : HoldReason::None, platformHold(action, s), platformActionName(action));
    }
    // The roof still holds everything.
    s.roof = Roof::State::Closing;
    TEST_ASSERT_EQUAL(HoldReason::Moving, platformHold(PlatformAction::Restart, s));
}

void test_hotspot_refuses_open_under_the_lock(void)
{
    Blockers b;
    b.hotspotOn = true;
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Open, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Close, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Stop, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Abort, b).action);
    TEST_ASSERT_EQUAL_STRING("hotspot", blockerReason(b));
    TEST_ASSERT_EQUAL_STRING(HOTSPOT_TEXT, blockerText(b));
    b.restartCommitted = true; // the reset comes first
    TEST_ASSERT_EQUAL_STRING("restarting", blockerReason(b));
}

void test_mandatory_restart_ignores_sequence(void)
{
    // The restart after a web UI upload remounts the filesystem; the same
    // firmware keeps spec 005's RTC record, so it waits only for the roof.
    TEST_ASSERT_EQUAL(HoldReason::None, platformHold(PlatformAction::MandatoryRestart, with(Roof::State::Open, false, true)));
    TEST_ASSERT_EQUAL(HoldReason::None, platformHold(PlatformAction::FlashWrite, with(Roof::State::Open, false, true)));
    // A web UI upload always ends in a restart: it waits for the sequence at its start.
    TEST_ASSERT_EQUAL(HoldReason::CloseSequence, platformHold(PlatformAction::FilesystemUpload, with(Roof::State::Open, false, true)));
    TEST_ASSERT_EQUAL(HoldReason::CloseSequence, platformHold(PlatformAction::WifiScan, with(Roof::State::Open, false, true)));
    TEST_ASSERT_EQUAL(HoldReason::CloseSequence, platformHold(PlatformAction::Restart, with(Roof::State::Open, false, true)));
}

void test_nothing_held_at_rest(void)
{
    for (PlatformAction action : ALL_ACTIONS)
        for (Roof::State rest : AT_REST)
            TEST_ASSERT_EQUAL_MESSAGE(HoldReason::None, platformHold(action, with(rest)), Roof::stateName(rest));
}

void test_hold_texts_and_names(void)
{
    TEST_ASSERT_EQUAL_STRING("", holdText(HoldReason::None));
    TEST_ASSERT_EQUAL_STRING("The roof is moving: stop it first, then try again", holdText(HoldReason::Moving));
    TEST_ASSERT_EQUAL_STRING("A close is waiting to start: try again once the roof has closed", holdText(HoldReason::CloseWaiting));
    TEST_ASSERT_EQUAL_STRING(
        "The automatic close is under way: try again once the roof has closed or the weather is safe, or press Stop to stand it down",
        holdText(HoldReason::CloseSequence));
    TEST_ASSERT_EQUAL_STRING("none", holdName(HoldReason::None));
    TEST_ASSERT_EQUAL_STRING("moving", holdName(HoldReason::Moving));
    TEST_ASSERT_EQUAL_STRING("closeWaiting", holdName(HoldReason::CloseWaiting));
    TEST_ASSERT_EQUAL_STRING("closeSequence", holdName(HoldReason::CloseSequence));
    TEST_ASSERT_EQUAL_STRING("flashWrite", platformActionName(PlatformAction::FlashWrite));
    TEST_ASSERT_EQUAL_STRING("filesystemUpload", platformActionName(PlatformAction::FilesystemUpload));
    TEST_ASSERT_EQUAL_STRING("restart", platformActionName(PlatformAction::Restart));
    TEST_ASSERT_EQUAL_STRING("mandatoryRestart", platformActionName(PlatformAction::MandatoryRestart));
    TEST_ASSERT_EQUAL_STRING("firmwareUpload", platformActionName(PlatformAction::FirmwareUpload));
    TEST_ASSERT_EQUAL_STRING("wifiChange", platformActionName(PlatformAction::WifiChange));
    TEST_ASSERT_EQUAL_STRING("wifiScan", platformActionName(PlatformAction::WifiScan));
}

void test_hotspot_refuses_only_open(void)
{
    TEST_ASSERT_TRUE(hotspotRefuses(Roof::Command::Open, true));
    for (Roof::Command other : {Roof::Command::Close, Roof::Command::Stop, Roof::Command::Abort})
        TEST_ASSERT_FALSE(hotspotRefuses(other, true));
    for (Roof::Command any : {Roof::Command::Open, Roof::Command::Close, Roof::Command::Stop, Roof::Command::Abort})
        TEST_ASSERT_FALSE(hotspotRefuses(any, false));
    TEST_ASSERT_EQUAL_STRING("hotspot", HOTSPOT_REASON);
    TEST_ASSERT_EQUAL_STRING("Opening isn't available while the setup hotspot is on", HOTSPOT_TEXT);
}

// Spec 010's additions to spec 003's command blockers (MotionGuards).

void test_short_writes_count_as_busy_without_touching_the_upload(void)
{
    // Spec 010 FR-003, FR-004: a settings save (or any short flash write)
    // counts as writing; it neither replaces nor stops an upload.
    FlashActivity f;
    TEST_ASSERT_FALSE(f.writing());
    f.holdShort();
    TEST_ASSERT_TRUE(f.writing());
    TEST_ASSERT_FALSE(f.busy(0)); // busy() is the upload alone
    const uint32_t upload = f.begin(0);
    f.holdShort();
    TEST_ASSERT_FALSE(f.shouldStop(upload));
    TEST_ASSERT_TRUE(f.busy(10));
    f.releaseShort();
    TEST_ASSERT_TRUE(f.writing()); // one still running
    f.releaseShort();
    TEST_ASSERT_FALSE(f.writing());
    f.releaseShort(); // never below zero
    TEST_ASSERT_FALSE(f.writing());
    f.holdShort();
    TEST_ASSERT_TRUE(f.writing());
    TEST_ASSERT_FALSE(f.shouldStop(upload));
    f.requestStop(); // a close stops the upload, not the short write
    TEST_ASSERT_TRUE(f.shouldStop(upload));
    TEST_ASSERT_TRUE(f.writing());
    f.end(upload);
    TEST_ASSERT_TRUE(f.writing());
    // Short writes never go stale: they always end.
    TEST_ASSERT_TRUE(f.writing());
}

void test_open_refused_close_deferred_during_short_write(void)
{
    Blockers b;
    b.flashWriting = true;
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Open, b).action);
    const BlockerDecision close = blockerDecision(Roof::Command::Close, b);
    TEST_ASSERT_EQUAL(BlockerAction::DeferClose, close.action);
    TEST_ASSERT_FALSE(close.stopFlash); // a short write can't be stopped and ends by itself
    TEST_ASSERT_FALSE(close.cancelRestart);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Stop, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Abort, b).action);
    TEST_ASSERT_EQUAL_STRING("A settings save is being written", blockerText(b));
    TEST_ASSERT_EQUAL_STRING("busy", blockerReason(b));
    // With an upload as well, the upload is still asked to stop.
    b.flashBusy = true;
    TEST_ASSERT_TRUE(blockerDecision(Roof::Command::Close, b).stopFlash);
}

void test_committed_restart_refuses_moves(void)
{
    // Spec 010 FR-008: once the restart's last check passed, nothing may start.
    RestartPlan plan;
    TEST_ASSERT_FALSE(plan.commit()); // nothing due
    TEST_ASSERT_FALSE(plan.committed());
    plan.schedule(false);
    TEST_ASSERT_TRUE(plan.cancellable());
    TEST_ASSERT_FALSE(plan.mandatory());
    TEST_ASSERT_TRUE(plan.commit());
    TEST_ASSERT_TRUE(plan.committed());
    TEST_ASSERT_FALSE(plan.cancellable());
    TEST_ASSERT_FALSE(plan.cancel());
    TEST_ASSERT_TRUE(plan.scheduled());
    plan.schedule(false); // a later schedule keeps it committed
    TEST_ASSERT_TRUE(plan.committed());
    TEST_ASSERT_FALSE(plan.cancel());

    Blockers b;
    b.restartScheduled = true;
    b.restartCancellable = false;
    b.restartCommitted = true;
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Open, b).action);
    const BlockerDecision close = blockerDecision(Roof::Command::Close, b);
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, close.action);
    TEST_ASSERT_FALSE(close.cancelRestart);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Stop, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Abort, b).action);
    TEST_ASSERT_EQUAL_STRING("The device is restarting", blockerText(b));
    // Committed and still reported cancellable: a close never cancels it.
    Blockers both = b;
    both.restartCancellable = true;
    const BlockerDecision closeBoth = blockerDecision(Roof::Command::Close, both);
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, closeBoth.action);
    TEST_ASSERT_FALSE(closeBoth.cancelRestart);
    // Committed alone (the probe saw it before anything else) refuses a close too.
    Blockers only;
    only.restartCommitted = true;
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Close, only).action);
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Open, only).action);
    TEST_ASSERT_EQUAL_STRING("The device is restarting", blockerText(only));
    TEST_ASSERT_EQUAL_STRING("restarting", blockerReason(only));
    TEST_ASSERT_EQUAL_STRING("restarting", RESTARTING_REASON);

    plan.clear(); // ignored once committed: moves stay refused until the reset
    TEST_ASSERT_TRUE(plan.committed());
    TEST_ASSERT_TRUE(plan.scheduled());
    RestartPlan fresh;
    fresh.schedule(true);
    TEST_ASSERT_TRUE(fresh.mandatory());
    fresh.clear();
    TEST_ASSERT_FALSE(fresh.scheduled());
    fresh.schedule(true);
    TEST_ASSERT_TRUE(fresh.commit());
}

void test_network_change_waited_for_like_a_write(void)
{
    // A WiFi change or scan can drop N.I.N.A.: a close waits for it, an open
    // is refused, stop goes on (spec 010, review round 1).
    Blockers b;
    b.networkChanging = true;
    TEST_ASSERT_EQUAL(BlockerAction::Refuse, blockerDecision(Roof::Command::Open, b).action);
    const BlockerDecision close = blockerDecision(Roof::Command::Close, b);
    TEST_ASSERT_EQUAL(BlockerAction::DeferClose, close.action);
    TEST_ASSERT_FALSE(close.stopFlash);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Stop, b).action);
    TEST_ASSERT_EQUAL(BlockerAction::Proceed, blockerDecision(Roof::Command::Abort, b).action);
    TEST_ASSERT_EQUAL_STRING("The WiFi network is changing", blockerText(b));
    TEST_ASSERT_EQUAL_STRING("busy", blockerReason(b));
}

void test_restart_may_commit(void)
{
    RestartInputs in;
    TEST_ASSERT_TRUE(restartMayCommit(in));
    in.sequenceWorking = true;
    TEST_ASSERT_FALSE(restartMayCommit(in));
    in.mandatory = true; // the mandatory one doesn't wait for the sequence
    TEST_ASSERT_TRUE(restartMayCommit(in));
    in = RestartInputs();
    in.uploadBusy = true;
    TEST_ASSERT_FALSE(restartMayCommit(in));
    in.mandatory = true;
    TEST_ASSERT_TRUE(restartMayCommit(in));
    // Nothing restarts with a fault not yet saved, or part-way through a short write.
    for (bool mandatory : {false, true})
    {
        RestartInputs f;
        f.mandatory = mandatory;
        f.faultUnsaved = true;
        TEST_ASSERT_FALSE(restartMayCommit(f));
        RestartInputs w;
        w.mandatory = mandatory;
        w.shortWrite = true;
        TEST_ASSERT_FALSE(restartMayCommit(w));
    }
}

void test_network_hold_is_bounded_inside_the_close_start_limit(void)
{
    // A stuck scan or a failed WiFi change must never hold a waiting close
    // past the close sequence's limit (which would halt the automatic close).
    TEST_ASSERT_TRUE(NETWORK_HOLD_MAX_MS + 5000 < CloseSeq::CLOSE_START_MS);
    TEST_ASSERT_TRUE(networkHoldsClose(true, 0));
    TEST_ASSERT_TRUE(networkHoldsClose(true, NETWORK_HOLD_MAX_MS));
    TEST_ASSERT_FALSE(networkHoldsClose(true, NETWORK_HOLD_MAX_MS + 1));
    TEST_ASSERT_FALSE(networkHoldsClose(false, 0));
    // Ages are unsigned differences of millis(): across the wrap they stay small.
    const uint32_t startedAt = 0xFFFFFFFFu - 1000;
    TEST_ASSERT_TRUE(networkHoldsClose(true, static_cast<uint32_t>(startedAt + 5000) - startedAt));
    TEST_ASSERT_FALSE(networkHoldsClose(true, static_cast<uint32_t>(startedAt + NETWORK_HOLD_MAX_MS + 1) - startedAt));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_action_held_while_moving);
    RUN_TEST(test_close_waiting_holds_every_action);
    RUN_TEST(test_sequence_busy_holds_restart_firmware_and_wifi);
    RUN_TEST(test_stand_downs_leave_the_way_out_open);
    RUN_TEST(test_working_sequence_without_network_leaves_the_way_out_open);
    RUN_TEST(test_hotspot_refuses_open_under_the_lock);
    RUN_TEST(test_mandatory_restart_ignores_sequence);
    RUN_TEST(test_nothing_held_at_rest);
    RUN_TEST(test_hold_texts_and_names);
    RUN_TEST(test_hotspot_refuses_only_open);
    RUN_TEST(test_short_writes_count_as_busy_without_touching_the_upload);
    RUN_TEST(test_open_refused_close_deferred_during_short_write);
    RUN_TEST(test_committed_restart_refuses_moves);
    RUN_TEST(test_network_change_waited_for_like_a_write);
    RUN_TEST(test_restart_may_commit);
    RUN_TEST(test_network_hold_is_bounded_inside_the_close_start_limit);
    return UNITY_END();
}
