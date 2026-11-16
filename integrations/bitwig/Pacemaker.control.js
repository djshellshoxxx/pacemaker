// Pacemaker.control.js : Bitwig Studio controller script.
// Listens for Pacemaker's OSC output (/pacemaker/tempo f) on a UDP port and sets the
// Bitwig transport tempo, with smoothing, a deadband and a confidence/state gate.
//
// API used (all appear in Bitwig's own and community controller scripts):
//   loadAPI, host.defineController, host.getOscModule, oscModule.createAddressSpace,
//   addressSpace.registerMethod(address, typeTag, description, callback(source, message)),
//   message.getArguments(), oscModule.createUdpServer(port, addressSpace),
//   host.getPreferences().getStringSetting / getEnumSetting, host.createTransport(), host.restart().
// Not verified against a running Bitwig in this repo: the Parameter setter used for tempo
// (see setTempo below, with a fallback).

loadAPI(16);

host.defineController("Pacemaker", "OSC Tempo Follow", "0.1.0", "8761eb1d-cf90-404e-acdf-3f2441ad2a83", "Pacemaker");
host.defineMidiPorts(0, 0);

var CFG = {
   port: 9000,
   root: "/pacemaker",
   alpha: 0.35,          // smoothing, 1 = off
   deadband: 0.05,       // BPM
   minIntervalMs: 100,
   minBpm: 30, maxBpm: 300,
   confidenceGate: 0.35,
   confidenceMaxAgeMs: 1000,
   jumpBpm: 12
};

var transport = null, tempoParam = null;
var enabledSetting = null;
var smoothed = null, lastApplied = null, lastApplyMs = 0;
var confidence = null, confidenceMs = 0, state = "";

function nowMs() { return new Date().getTime(); }

function setTempo(bpm) {
   // Parameter.setRaw takes the value in the parameter's own units (BPM for tempo).
   try {
      tempoParam.setRaw(bpm);
   } catch (e) {
      try { tempoParam.value().setRaw(bpm); }
      catch (e2) { println("Pacemaker: cannot set tempo: " + e2); }
   }
}

function maybeApply() {
   if (smoothed === null || enabledSetting.get() !== "On") return;
   var t = nowMs();
   if (state === "HOLD" || state === "IDLE") return;
   if (confidence !== null && t - confidenceMs <= CFG.confidenceMaxAgeMs && confidence < CFG.confidenceGate) return;
   if (t - lastApplyMs < CFG.minIntervalMs) return;
   if (lastApplied !== null && Math.abs(smoothed - lastApplied) <= CFG.deadband) return;
   setTempo(smoothed);
   lastApplied = smoothed; lastApplyMs = t;
}

function onTempo(bpm) {
   if (typeof bpm !== "number" || isNaN(bpm) || bpm < CFG.minBpm || bpm > CFG.maxBpm) return;
   if (smoothed === null || Math.abs(bpm - smoothed) > CFG.jumpBpm) smoothed = bpm;
   else smoothed = smoothed + CFG.alpha * (bpm - smoothed);
   maybeApply();
}

function init() {
   var prefs = host.getPreferences();
   enabledSetting = prefs.getEnumSetting("Follow Pacemaker tempo", "Pacemaker", ["On", "Off"], "On");

   var portSetting = prefs.getStringSetting("OSC listen port", "Pacemaker", 8, String(CFG.port));
   var rootSetting = prefs.getStringSetting("OSC root", "Pacemaker", 32, CFG.root);
   var initialPort = parseInt(portSetting.get(), 10);
   var initialRoot = rootSetting.get();
   // Bitwig requires OSC connections to be created in init(), so changing these restarts the script.
   portSetting.addValueObserver(function (v) { if (parseInt(v, 10) !== initialPort) host.restart(); });
   rootSetting.addValueObserver(function (v) { if (v !== initialRoot) host.restart(); });
   if (!isNaN(initialPort) && initialPort >= 1024 && initialPort <= 65535) CFG.port = initialPort;
   if (initialRoot && initialRoot.charAt(0) === "/") CFG.root = initialRoot;

   transport = host.createTransport();
   tempoParam = transport.tempo();
   tempoParam.markInterested();

   var oscModule = host.getOscModule();
   var space = oscModule.createAddressSpace();
   space.registerMethod(CFG.root + "/tempo", ",f", "Pacemaker tempo in BPM", function (source, message) {
      onTempo(message.getArguments()[0]);
   });
   space.registerMethod(CFG.root + "/confidence", ",f", "Pacemaker confidence 0..1", function (source, message) {
      confidence = message.getArguments()[0]; confidenceMs = nowMs();
   });
   space.registerMethod(CFG.root + "/state", ",s", "Pacemaker state name", function (source, message) {
      state = message.getArguments()[0];
   });
   // /beat, /downbeat, /phase arrive too and are ignored (address space logs unknown messages only if asked).
   space.setShouldLogMessages(false);

   try {
      oscModule.createUdpServer(CFG.port, space);
      println("Pacemaker: listening on UDP " + CFG.port + " for " + CFG.root + "/tempo");
   } catch (e) {
      println("Pacemaker: cannot open UDP port " + CFG.port + ": " + e);
      host.showPopupNotification("Pacemaker: cannot open UDP port " + CFG.port);
   }
}

function flush() {}
function exit() {}
