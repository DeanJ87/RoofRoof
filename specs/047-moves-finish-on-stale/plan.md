# Plan: A Move Finishes When N.I.N.A. Stops Answering

**Spec**: [spec.md](spec.md) | **Constitution**: 12.0.0 (II, III, V, VI, IX)

## Approach

1. **The hold** (`lib/SafetyInputLogic/ParkHold.cpp`): `CloseHold::unanswered` from the park result's
   reasons (only `ParkNinaUnreachable`, `Unreadable`, `ApiError`, `Stale`) and N.I.N.A.'s last answer
   (the stale reading itself, or `NinaMemory::lastGood`) re-judged at its own time with the switches
   as now. The ride-through (`riding`, `freshUntilMs`) is unchanged and still drives the log.
2. **The gates** (`lib/RoofIoLogic/RoofGates`): `GateInputs::parkUnanswered`; `closeMayContinue` =
   switches closed and (`parked` or `parkUnanswered` or a fresh ride-through); `parkHoldsFresh`;
   `movesOnStale` (watched, not the N.I.N.A.-lost close, going on without a fresh park);
   `interlockBits` names the park result only when it fails; `interlockRecordBits` adds
   `InterlockDuringOpen`. `currentGates` clears `parkUnanswered` on a stalled hand-over.
3. **The report** (`lib/RoofIoLogic/StaleRide`): per move, `Began` once, `Finished` when it reaches
   its target end; texts. `RoofControl::stepStaleRide` (under the lock, every step, before the
   interlock) logs through `notice` and hands the finish to the main loop (`takeStaleAlert`), which
   sends `CloseSeq::staleFinishAlert` (`AlertType::FinishedUnconfirmed`, `finished_without_park`,
   Normal, no setting) through `CloseOnUnsafe`'s outbox.
4. **The fault text** (`RoofText`): "Park lost during the open/close" from the record's bit; the
   firmware and the demo record `interlockRecordBits`.
5. **The poller** (`lib/SafetyInputLogic/PollSchedule`, `src/SafetyPoller.cpp`): `PollSchedule`
   decides each pass (N.I.N.A. due every 2 s or before a due weather poll; the weather every 1.5 s
   while `setRoofMoving`, else 5 s); `httpGet` takes `HttpLimits` (`SQMETER_LIMITS`, `NINA_LIMITS`);
   the site reads keep clear of the next weather poll as before (`sqmeterNextMs`). The main loop
   feeds `RoofControl::moving()`.
6. **Tests, docs, demo core, constitution 12.0.0.**

## Budgets

No new task, no lasting buffer: `heap_budget.py` unchanged. The poller's pass adds a few locals;
its 8 KB stack keeps the TLS handshake's room. The control step adds one small tracker (two bytes)
and one string assigned only at a move's end.

## Complexity Tracking

Principle II is relaxed by a MAJOR amendment (12.0.0) that states the risk; no exception is taken.
