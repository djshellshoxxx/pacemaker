# Architecture Summary

From `docs/specs/ES-04-architecture-threads-buses.md` and the engine layout.

- **Tracking engine.** `engine/src/`: a front end (`FrontEnd`), tempo induction (`TempoInduction`), a beat tracker (`Tracker`) and a bar tracker (`BarTracker`), orchestrated by `Engine`.
- **Lock-free handoff.** `engine/include/pacemaker/SpscQueue.h` is a single-producer, single-consumer queue between threads; `FFT.h` and `Types.h` hold shared pieces.
- **Clock outputs as planned work.** Ableton Link, MIDI clock, OSC and sync pulse are specified in `ES-02`, not implemented in this build.
- **Thread and bus design.** Threads, buses and standalone audio and MIDI are specified in ES-04 (sections 2 to 5).
- **Research-first.** The approach and competitor comparison are documented in `docs/research/` and `docs/DIFFERENTIATION.md`.
