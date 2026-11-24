# Quickstart: spec 026

1. `cd web && npm run build:demo && PW_PORT=4396 npx playwright test tests/journey.spec.ts`.
2. In the demo: the dashboard's Roof setup card lists seven stages; "Calibration run" says the move
   time limits are still defaults (about 66 s a move).
3. Open the roof, Demo panel Weather Unsafe, press Stop, Weather Safe: the Roof card says the
   automatic close is on hold after a Stop, clears at a clock time, with Release hold. Calibrate says
   the same; Release hold asks if you are at the roof, then "Nothing stops a move now."
4. Weather Unsafe, 12 V supply Low on Calibrate: one list, each line with when it clears and a fix.
5. Settings, Network: type an MQTT user name in the followed group: "Set here, not from SQMeter".
6. Test box: with the roof open and N.I.N.A.'s address cleared, turn No weather station on: the note
   says the SQMeter source goes off in the same save; Save; no automatic close starts.
