# Cosmic Microwave 2.8.0 — Server Integration Contract

- **Status:** Normative handoff for the audience server team and its coding agents
- **Date:** 2026-08-26
- **Cosmic Microwave:** `2.8.0`, state schema `11`
- **Server revision audited:** [`d86858791ee70f407bf0576ba33f03c57c75f81f`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/commit/d86858791ee70f407bf0576ba33f03c57c75f81f)
- **Server repository:** [CosmicSymphonyCreative/cosmicsymphony-audience-interaction](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction)
- **OSC reference page:** [Cosmic OSC](https://neva40903.tail4254e0.ts.net/cosmic-osc/)
- **Simulator contract:** `cosmic-microwave-human-simulator/v1`
- **Real10 calibration report:**
  [`Crowd-Simulator-Calibration-2026-08-24.md`](analysis/Crowd-Simulator-Calibration-2026-08-24.md)
- **Real10 reference SHA-256:**
  `4509299725685e9134803f1ad74ebe0a31f2ab89e30d6b2b5ac29be91de97401`

This document is the compatibility boundary between the audience platform and Cosmic
Microwave. It is intentionally prescriptive. If a server implementation, simulator,
load balancer, relay, splitter, or deployment setting conflicts with this document,
the server side must be changed. The Cosmic Microwave timing and source-to-MIDI
mapping described here are fixed product decisions.

For this integration, the authority order is: compiled Cosmic Microwave source,
this contract, current `docs/manual/`, then other repository prose. `AGENTS.md`,
`docs/article/`, and old design specifications describe historical product phases and
are not server-runtime contracts.

`MUST`, `MUST NOT`, `SHOULD`, and `MAY` are used in their usual normative sense.

---

## Yönetici özeti

Cosmic Microwave, seyirciden gelen OSC verisini ses üretmeden Notes Only MIDI'ye dönüştüren
Ableton içi performans aracıdır. Server'ın görevi müzikal karar vermek değil, her
katılımcının niyetini doğru kimlik, doğru sıra ve güvenli lifecycle ile plug-in'e
ulaştırmaktır.

Sunucu ekibi için değişmez kararlar:

- server quantize kapalıdır; zamanlama Cosmic Microwave ve Ableton'a aittir;
- OSC wire formatının fiziksel source aralığı `0..255` olarak sabittir; her zone için
  seçilen **Source Capacity** `64`, `128` veya `256` olur ve server yalnız yoğun,
  sıfır-tabanlı `0..capacity-1` alanından benzersiz kimlik tahsis eder;
- MIDI topolojisi her kapasitede sabit 16 kanaldır: `source 0 -> Ch16`,
  `source 1 -> Ch1`, `16 -> Ch16`, `17 -> Ch1`; server kanal seçmez;
- üretim Ableton receiver topolojisi `Ch1..8 -> OMNI1 Parts1..8` ve
  `Ch9..16 -> OMNI2 Parts1..8` şeklinde iki Omnisphere Multi'dir; bu CPU
  paralelliği garantisi değildir ve gerçek show makinesinde soak testi zorunludur;
- tek telefon/tek dokunuş yalnız `finger0` üretir;
- başlangıç grubu aynı immediate bundle içinde `u -> v -> on 1` olur;
- `u` perdeyi, `v` yalnız Note-On velocity'sini belirler; performans çıkışı yalnız
  Note On/Off'tur, hareket sırasında MIDI controller üretilmez ve mesaj akışını
  gate/modüle eden bir LFO yoktur;
- release `on 0` olur ve hiçbir koşulda drop/rate-limit/quantize edilmez;
- `2n/4n/8n/16n/32n` Note Duration tamamen plug-in tarafındadır; her yeni MIDI
  Note On kendi başlangıç anındaki host BPM snapshot'ıyla sürelenir ve server bunun
  için OSC'yi geciktirmez, yeniden eşlemez veya yeni bir mesaj tipi üretmez;
- Venue Engine karma-zone OSC'yi yerel bridge girişine (`127.0.0.1:6061`) yollar;
- bridge, gösteri için dondurulan açık bir manifest ile her aktif zone'u benzersiz bir
  UDP çıkışına ayırır; Cosmic Microwave porttan zone tahmin etmez;
- Cosmic Microwave 2.8.0'da gerçekten fresh instance'lar en düşük boş complete factory
  route'u atomik claim eder: `A=6062 ... H=6069`; A Leader, B-H Follower'dır;
- saved/restored state, direct route edit'i ve explicit factory preset exact kazanır ve
  auto allocator tarafından başka zone'a kaydırılmaz;
- A-H'nin tamamı doluysa fresh instance OSC receiver, sanal MIDI endpoint veya Global
  Conductor registration açmadan fail-closed kalır; route boşaltıldıktan sonra operatör
  `RETRY AUTO` kullanır;
- bu automatic local claim server manifestinin yerine geçmez: manifest gösteri öncesi
  değiştirilebilir, ancak bridge, Expected Zone, sanal MIDI endpoint ve Ableton routing
  aynı dondurulmuş manifestte birebir eşleşmelidir;
- 2.000 kişi için Source Capacity `256` seçildiğinde kimlik bakımından en az 8
  zone/instance gerekir; 16 zone x yaklaşık 125 kişi, zone başına kapasite `128` ile
  daha yüksek hareket çözünürlüğü sağlayan isteğe bağlı dengeli profildir;
- tek bir OSC üretim yolu aktif olur; Venue Engine ve Relay aynı hedefe birlikte
  göndermez.
- production rehearsal için `Human` simülasyonu Section 11'deki Real10 modelini
  kullanır; canlı worker ve offline trace aynı davranış modülünü paylaşır;
- LoadGen'in canlı Human istemcileri gerçek telefonlarla aynı public load-balanced
  WebSocket yolundan geçer; load balancer trafik yönlendirir ama ayrı bir hareket
  modeli kurmaz ve U/V/On lifecycle'ını yeniden yazmaz.
- Cosmic Microwave Ready Gate, seçili kapasitenin tam source alanını iki saniye boyunca
  yerel olarak sayan bir sinyal kontrolüdür: tüm source'lar aktif tutulur, U ve V ayrı
  ayrı taze kalır, plug-in içi simulator nüfusu sıfır olur. Bu kontrol server owner
  roster'ının, dondurulmuş bridge manifestinin veya 60 saniyelik production-path
  average/P95 soak testinin yerine geçmez.

Bu belgenin Section 15 kabul testleri geçmeden sistem gösteriye hazır sayılmaz.

---

## 1. What Cosmic Microwave is

Cosmic Microwave is a silent OSC-to-Notes-Only-MIDI performance instrument. It does not
generate internal sound. One plugin instance represents one audience zone:

```text
Audience phones
  -> WebSocket/load-balanced server
  -> Venue Engine (mixed zones)
  -> local Venue Bridge ingress 127.0.0.1:6061
  -> manifest-selected unique UDP output per zone
  -> one Cosmic Microwave instance per zone
  -> Normal MIDI channels 1..16
  -> Ch1..8 -> OMNI1 Multi Parts 1..8
  -> Ch9..16 -> OMNI2 Multi Parts 1..8
```

Cosmic Microwave owns the following musical responsibilities:

- source/finger lifecycle and stuck-note recovery;
- fixed Normal MIDI channel ownership;
- Tonal or Atomic-to-nearest-MIDI-note pitch mapping;
- host-synchronised Flow/Grid/Ensemble timing;
- fixed `2n/4n/8n/16n/32n` Note Duration, calculated independently for every new
  Note On from its onset host-tempo snapshot;
- Adaptive Crowd Governor and pressure-aware Safety Governor;
- optional Global Conductor coordination between plugin instances;
- Notes Only participant output: Note On/Off, with U selecting pitch and V selecting
  Note-On velocity;
- Panic using only the bounded CC120/CC123 safety sweep, plus queue recovery,
  telemetry, and Venue Preflight.

The server owns transport, identity, ordering, lifecycle delivery, rate control, zone
separation, and UDP delivery. It must deliver audience intent faithfully and must not
duplicate Cosmic Microwave's musical scheduler.

### Signal-flow authority

```text
PHONE INTENT
    |
    v
[Server identity + lifecycle safety]
    |
    v
[Latest U/V coalescing; Off has highest priority]
    |
    v
[Immediate OSC 1.0 bundles]
    |
    v
[Zone -> dedicated UDP port]
    |
    v
[Cosmic Microwave canonical source ledger]
    |
    +--> Flow: direct lifecycle
    |
    +--> Grid/Ensemble: Ableton PPQ / Cosmic Time Field
    |
    v
[Fixed source -> MIDI ownership]
    |
    v
[Ableton / hardware instrument]
```

There must be exactly one timing authority and exactly one live OSC production path.

---

## 2. Non-negotiable product decisions

The server team must treat these as fixed:

1. **Timing authority is Cosmic Microwave/Ableton.** Server-side note quantisation is
   disabled. The server sends immediate intent; Cosmic Microwave decides Flow, Grid,
   or Ensemble timing.
2. **Normal MIDI mapping is fixed and source-based.** The server sends a stable source
   ID and never assigns or transmits a MIDI channel.
3. **One plugin instance represents one zone.** The server/splitter sends one zone to
   one dedicated UDP port.
4. **The live product is single-touch.** Only `finger0` is part of the production
   contract.
5. **OSC lifecycle identity is `(zone, source, finger0)`.** It is never assigned per
   packet and must remain stable from `on 1` through `on 0`.
6. **Releases are never governed, quantised, delayed, or shed.** `on 0`, disconnect
   release, and panic/mute release have higher priority than On and U/V.
7. **Only immediate OSC bundles are accepted.** Future/dated timetags are deliberately
   rejected by Cosmic Microwave.
8. **The server must not change the plugin's mapping, timing modes, watchdog, or MIDI
   routing to compensate for an upstream defect.** Fix the upstream defect.
9. **Source Capacity is a per-zone identity-domain limit, not a polyphony control.**
   Each instance selects `64`, `128`, or `256`; the matching server allocator uses
   only the dense zero-based domain `0..capacity-1`. Time Field Active Limit,
   Adaptive/Safety Governor quotas, and Global Conductor voice budgets remain
   separate musical controls.
10. **Note Duration is owned by Cosmic Microwave.** The selected
    `2n/4n/8n/16n/32n` value and host BPM are snapshotted at each new MIDI Note On.
    The server still sends immediate canonical U/V/On lifecycle and must not add a
    duration, tempo, MIDI-channel, or scheduling field to OSC.
11. **Ensemble Same Note articulation is owned by Cosmic Microwave.** `Tie` is the
    compatible default; `Retrigger` performs Note Off then Note On at each admitted
    identical-pitch Ensemble pulse. The server sends the same immediate U/V/On
    lifecycle in either case and must not add an articulation field or enable MPE.

---

## 3. Machine-readable deployment contract

Server agents may use this block as the canonical target configuration:

```yaml
contract: cosmic-microwave-osc-v1
cosmicMicrowaveVersion: 2.8.0
stateSchema: 11
timingOwner: cosmic-microwave
serverNoteQuantisation: false
osc:
  transport: udp
  version: "1.0"
  bundleTimeTag: immediate
  maxDatagramBytesRecommended: 1200
  maxDatagramBytesAbsolute: 65507
identity:
  zones: A-Z
  acceptedSourceRange: 0-255
  acceptedWireSourceRange: 0-255
  sourceCapacityChoices: [64, 128, 256]
  freshPluginDefaultSourceCapacity: 64
  legacyProjectMissingCapacityMigratesTo: 256
  productionAllocatedSourceRange: "0..(selectedSourceCapacity-1)"
  denseZeroBasedAllocationRequired: true
  activeOwnerSetMayBeSparse: true
  capacityIsIndependentPerZone: true
  outsideSelectedCapacityPolicy: drop-and-count
  activeSessionOwnersPerZoneSource: 1
  finger: finger0
routing:
  oneZonePerUdpPort: true
  bridgeIngressHost: 127.0.0.1
  bridgeIngressPort: 6061
  routeManifestAuthority: venue-bridge
  freshPluginFactoryPool:
    lowestFirst: true
    routes: ["6062/A/Leader", "6063/B/Follower", "6064/C/Follower", "6065/D/Follower", "6066/E/Follower", "6067/F/Follower", "6068/G/Follower", "6069/H/Follower"]
    claim: retained-exclusive-osc-bind
    exhaustedPolicy: fail-closed-until-retry-auto
    withheldWhenExhausted: [osc_receiver, virtual_midi_endpoint, global_conductor_registration]
  freshAllocationDisabledBy: [host-restored-state, direct-route-edit, explicit-factory-preset]
  exampleRouteManifest:
    A: 6062
    B: 6063
    C: 6064
    D: 6065
    E: 6066
    F: 6067
    G: 6068
    H: 6069
  uniqueOutputPortPerZone: true
  expectedZoneMustMatchManifest: true
  unmappedPort: 7000
  unmappedPortPolicy: quarantine-only
ableton:
  outputPathForShownTemplate: External Only
  normalMidiRouting: Per Source 1-16
  receiverTopology: two-omnisphere-multis
  receiverInstances: 2
  partsPerReceiver: 8
  receiverRoutes:
    - { channels: "1-8", receiver: OMNI1, parts: "1-8" }
    - { channels: "9-16", receiver: OMNI2, parts: "1-8" }
  sourcesPerPartByCapacity: { 64: 4, 128: 8, 256: 16 }
  hostParallelismGuaranteed: false
  showMachineCpuAndDropoutSoakRequired: true
  virtualMidiEndpointPattern: "Cosmic Microwave <udp-port> Out"
  mirrorOutputAllowed: false
normalMidi:
  mode: notes-only
  acceptedSourceIdMinimum: 0
  channelCount: 16
  moduloReferenceSource: 1
  channelFormula: "((source_id - 1) mod 16) + 1"
  sourceZeroChannel: 16
  sourcesPerChannelByCapacity: { 64: 4, 128: 8, 256: 16 }
noteDuration:
  owner: cosmic-microwave
  choices: [2n, 4n, 8n, 16n, 32n]
  freshFactoryDefault: 16n
  legacyProjectMissingDurationMigratesTo: 16n
  tempoSnapshot: host-bpm-at-each-new-midi-note-on
  serverMayQuantiseOrSchedule: false
  oscProtocolExtension: none
performanceMidi:
  format: normal-notes-only
  generatedMessages: [note_on, note_off]
  uRole: pitch
  vRole: note-on-velocity
  velocityFormula: "clamp(round(clamp(v, 0, 1) * 127), 1, 127)"
  continuousParticipantControllers: false
  crowdMacroControllers: false
  mpe: false
  lfoMessageGate: false
  prohibitedGeneratedMessages: [cc11, cc74, cc20, cc21, cc22, cc23, channel_pressure, pitch_bend, rpn]
  panic:
    controllers: [cc120, cc123]
    channels: 1-16
lifecycle:
  attackOrder: [u, v, on1]
  release: on0
  releasePriority: highest
  disconnectProducesRelease: true
  lifecycleMayBeDropped: false
  ordinaryReleasePreservesScheduledTail: true
  liveWatchdogTimeoutMs: 3000
  liveWatchdogAction: internal-cancel-source-and-scheduled-tails
  watchdogCancelOscExtension: none
motion:
  semantics: latest-value
  targetAcceptedEventsPerSecondPerInstance: 1200
  loadGenHumanExpressiveUvBudgetPerSecondPerZone: 600
  expressiveBudgetExcludesLifecycleAndKeepalive: true
  heldTouchHeartbeatMaxIntervalMs: 900
sourceQualityReadyGate:
  scope: local-cosmic-accepted-osc-signal-census
  operatorArmedRuntimeOnly: true
  exactExpectedSourceDomain: "0..(selectedSourceCapacity-1)"
  evidenceRequiredPerSource: [finite_u, finite_v, on1]
  preReadyCleanHoldMs: 2000
  allExpectedSourcesMustRemainActivelyHeld: true
  requireFreshUAndVSeparately: true
  receiverHeartbeatToleranceMs: 1200
  serverSenderHeartbeatMaxGapMs: 900
  maximumMotionEventsPerSecondPerSource: 50
  maximumAggregateMotionEventsPerSecondPerInstance: 1200
  internalCosmicSimulatorPopulationRequired: 0
  separateDropTelemetry: [capacity, motion, lifecycle]
  doesNotReplace: [server_owner_roster, frozen_route_manifest, production_60s_soak_avg_p95]
simulator:
  contract: cosmic-microwave-human-simulator/v1
  modelRevision: real10-2026-08-24
  defaultProfile: Human
  unknownProfilePolicy: fall-back-to-Human
  mode: pad
  minFingers: 1
  maxFingers: 1
  allowSourceModuloCollision: false
  liveTransportPath: public-load-balanced
  loadBalancerMayGenerateOrRewriteEvents: false
  transportBypassSatisfiesProductionGate: false
  productionTickMs: 10
  physicsTickMs: 50
  deterministicPerSourceStreams: true
  human:
    desiredMotionHz: 20
    expressiveUvScalarBudgetPerSecondPerZone: 600
    activeKeepaliveMaxIntervalMs: 900
    startRatePerSecondPerZone: 180
    startBurstPerZone: 16
    maximumEffectiveWorkers: 16
  dense:
    expressiveUvScalarBudgetPerSecondPerZone: 2000
  stress:
    expressiveUvScalarBudgetPerSecondPerZone: 8000
```

`maxDatagramBytesRecommended: 1200` avoids IP fragmentation across common LAN,
VPN, and Tailscale paths. If the deployment deliberately uses larger datagrams, it
still must preserve participant-event groups, stay below the real path MTU, and never
exceed the UDP absolute limit.

---

## 4. Canonical OSC wire protocol

### 4.1 Address

Production addresses MUST use exactly:

```text
/cs/<ZONE>/<SOURCE>/finger0/<PARAM>
```

| Segment | Required value |
|---|---|
| Prefix | `/cs/` |
| Zone | exactly one uppercase ASCII letter `A` through `Z` |
| Source | decimal integer `0` through `255` |
| Finger | exact lower-case token `finger0` |
| Parameter | lower-case `u`, `v`, or `on` |

`0..255` is the physical OSC grammar and compiled storage ceiling. It is not a
promise that every deployed instance currently admits all 256 identities. Before a
show, each zone freezes one Source Capacity choice and the server allocates only:

| Selected Source Capacity | Admitted source IDs in that zone |
|---:|---:|
| `64` | `0..63` |
| `128` | `0..127` |
| `256` | `0..255` |

The provisioned ID domain MUST be dense and zero-based. The set of *currently
connected* owners may of course be sparse when seats are empty or participants
disconnect; those remaining live IDs are not compacted or renumbered. A syntactically
valid source above the selected limit is rejected at admission, not wrapped into the
selected range.

Canonical examples:

```text
/cs/A/1/finger0/u
/cs/A/1/finger0/v
/cs/A/1/finger0/on
/cs/B/17/finger0/on
```

The server MUST NOT produce:

- `finger1` through `finger9` in the live product;
- multi-letter, numeric, punctuation, or out-of-range zones;
- source `256` or higher;
- legacy `/line` or `/off` addresses;
- extra path segments, query strings, or alternate prefixes;
- future/dated OSC bundle timetags.

Cosmic Microwave keeps limited `/line` and `/off` compatibility for old local tools,
but these are not part of the server contract.

### 4.2 Arguments and cardinality

| Address suffix | OSC argument | Range | Meaning |
|---|---|---:|---|
| `/u` | exactly one `float32` | `0.0..1.0` | horizontal position used for pitch selection |
| `/v` | exactly one `float32` | `0.0..1.0` | stored velocity for the next Note On; no continuous MIDI output |
| `/on` | exactly one `int32` | `0` or `1` | source release or activation |

All values MUST be finite. The server SHOULD clamp before serialization and MUST NOT
send NaN, infinity, strings, blobs, doubles, booleans encoded as strings, or messages
with missing/additional arguments.

Real-phone U/V MUST retain the client's full normalized precision. The server MUST
NOT quantise or round incoming production-phone values. On the audited hub this means
`SSE_UV_PRECISION=-1`; a persisted `cs:venuecfg` value must agree.

The `Human` simulator is a separate case: the Real10 reference itself is quantised to
a `0.01` grid, so the simulator deliberately generates finite `[0,1]` U/V values on
that grid. This is model output, not permission for any relay, load balancer, bridge,
or OSC encoder to re-quantise real client traffic.

Cosmic Microwave defensively accepts numeric int32/float32 and clamps U/V, but the
server must emit the canonical types above so captures and test tools remain
unambiguous.

### 4.3 Event groups and ordering

Attack:

```text
immediate bundle/group:
  1. /cs/A/1/finger0/u   float32
  2. /cs/A/1/finger0/v   float32
  3. /cs/A/1/finger0/on  int32 1
```

Motion while held:

```text
immediate bundle/group:
  1. /cs/A/1/finger0/u   float32
  2. /cs/A/1/finger0/v   float32
```

Release:

```text
/cs/A/1/finger0/on  int32 0
```

The U/V/On attack group MUST remain ordered and MUST NOT be split across UDP
datagrams. A bundle may contain multiple complete participant groups, but a chunk
boundary may occur only between groups. Release may be its own immediate bundle and
must bypass motion batching.

### 4.4 Bundle policy

- Every bundle at every nesting level MUST use the OSC `immediately` timetag.
- Prefer one flat top-level bundle; nesting adds no value here.
- A dated bundle is not a scheduling request to Cosmic Microwave; it is rejected.
- The splitter must inspect every message in a mixed upstream bundle and rebuild
  separate per-zone bundles. It must not route the entire bundle by its first address.
- The splitter must preserve the ordering of messages inside each participant group.
- A participant group must never be partially forwarded.

---

## 5. Fixed source-to-MIDI mapping

Normal MIDI uses this fixed formula:

```text
channel = ((source_id - 1) mod 16) + 1
```

The modulo is mathematically normalised, so source `0` wraps backward to Channel 16.

| Source | Channel |
|---:|---:|
| 0 | 16 |
| 1 | 1 |
| 2 | 2 |
| 16 | 16 |
| 17 | 1 |
| 32 | 16 |
| 255 | 15 |

Source Capacity changes only which source identities are admitted. It never changes
the 16-channel MIDI topology or the formula above:

| Source Capacity | Dense admitted domain | MIDI channels | Sources per channel |
|---:|---:|---:|---:|
| `64` | `0..63` | `1..16` | `4` |
| `128` | `0..127` | `1..16` | `8` |
| `256` | `0..255` | `1..16` | `16` |

For example, at capacity `64`, Channel 16 owns sources `0,16,32,48` and Channel 1
owns `1,17,33,49`. Source `64` is over capacity and is dropped; it is **not** wrapped
or admitted merely because the MIDI formula would point at Channel 16. The same
capacity choice and mapping restart independently for every zone/instance.

Source Capacity is not Time Field Active Limit, simultaneous MIDI polyphony, the
Adaptive/Safety Governor voice quota, or the Global Conductor voice budget. Those
controls may remain at 16 while the identity domain is 64, 128, or 256.

Server requirements:

- The server MUST NOT send MIDI-channel data or dynamically rebalance IDs by load.
- The same source ID MUST own U, V, On, Off, disconnect, and reconnect lifecycle.
- An active source ID MUST NOT be reassigned to another phone.
- IDs MUST NOT be compacted or renumbered when another participant disconnects.
- Production audience IDs MUST remain inside that zone's selected dense
  `0..capacity-1` domain and the physical `0..255` wire ceiling; source `0` is a
  normal live identity observed in the real-phone capture.
- Source `0` deliberately maps to Channel 16; this is not an off-by-one bug.
- Mapping restarts independently in every zone because each zone has its own Cosmic
  Microwave instance and virtual MIDI endpoint.

Messages above the selected capacity are deliberately dropped before they can reach
the source ledger or MIDI renderer. Cosmic Microwave reports them through a separate
bounded, saturating capacity-drop telemetry counter; they must not be confused with
malformed OSC, wrong-zone packets, FIFO pressure, or a legal inactive source. The
expected production value is zero.

Capacity expansion is safe and does not remap existing sources. Capacity reduction
is a coordinated show-control operation: pause new On admission, release active
owners whose IDs are outside the new domain, wait for the lifecycle path to drain,
then apply the new value on both server and plug-in. Cosmic Microwave additionally
retires any residual upper-domain identities deterministically and uses its bounded
router/MIDI safety reset if individual cleanup cannot be represented, preventing
stuck notes. It is still an upstream error for the server to continue sending those
IDs after the reduction.

Fresh factory state selects `64`. Current saves use state schema 11. A schema-9-or-
earlier project predates Source Capacity, had an implicit 256-ID domain, and migrates
a missing value to `256`, preserving its old identity and channel routing. A partial
schema-10 state with no capacity value uses the fresh `64` default. The server must
therefore read/freeze the actual value for each zone; it must not assume that every
opened project uses the fresh default. Saved projects with no Note Duration value
migrate to `16n`. The historical schema-9 migration that coerces former MPE state to
Notes Only / Per source and disables Crowd Expression output remains intact.
Schema 11 adds the local Ensemble Same Note parameter. Missing, malformed, and
out-of-range values restore `Tie`; the server contract and OSC wire format are unchanged.

Cosmic Microwave 2.8.0 has one performance MIDI mode: **Normal Notes Only**. Participant
interaction produces Note On and Note Off only. U selects the mapped MIDI note. The
latest finite V value is sampled when a Note On is created and becomes velocity
`clamp(round(V * 127), 1, 127)`. A held-touch V update changes stored state for the
next retrigger/attack but emits no MIDI by itself. A held-touch U update emits no MIDI
while it remains in the same mapped-note region; crossing into a different mapped
note may create a new Note On on the same source-owned channel. The prior pitch keeps
its own immutable Note Duration deadline and emits its Note Off when that tail expires.

Participant interaction MUST NOT generate CC11, CC74, CC20-23, Channel Pressure,
Pitch Bend, RPN, MPE setup, or any other continuous controller stream. Crowd Macro CC
output is disabled. The only generated controller messages are the bounded panic
safety sweep: CC123 (All Notes Off) and CC120 (All Sound Off) once on each of Channels
1 through 16. This panic exception is not participant expression and does not change
the server OSC protocol.

---

## 6. Timing contract

### 6.1 One timing authority

The server MUST run with:

```text
QUANTISE_NOTES=false
```

This applies to every component capable of building OSC, including `osc-bridge` and
`venue-engine`. The current server default is true and must be overridden or changed
for the Cosmic Microwave deployment.

Change both code defaults and deployment configuration. A production deployment must
not become quantised again merely because an environment variable was omitted during
a restart.

The server MUST NOT:

- move On or Off to a wall-clock grid;
- infer Ableton tempo;
- use `Date.now()` as a musical clock;
- attach a future OSC timetag;
- delay Off until a beat;
- perform a second spread, gate, attack-limit, or voice-limit stage.

Cosmic Microwave's Time Field provides:

- **Flow:** direct lifecycle;
- **Grid:** host/internal grid admission;
- **Ensemble:** host/internal grid, source lanes, gate, and repeat;
- **Adaptive Crowd Governor:** density-aware attack/spread/active limits;
- **Global Conductor:** fair budgets across zone instances in one plugin process.

With Host selected and Ableton playing, the shared timebase is Ableton PPQ and tempo.
With no valid playing host clock, Cosmic Microwave uses its common monotonic fallback.
The server does not need tempo, PPQ, or transport information.

Ensemble's local **Same Note** setting is either `Tie` or `Retrigger`. `Tie` retains
identical-note ownership; `Retrigger` safely emits Note Off then Note On on every
admitted pulse. Flow and Grid force Tie. This is not an OSC message or server setting.

### 6.2 Fixed Note Duration is plug-in-side

Cosmic Microwave offers `2n`, `4n`, `8n`, `16n`, and `32n` fixed musical tails. The
fresh factory choice is `16n`. For every newly created MIDI Note On, the plug-in
snapshots both the selected duration and the valid host BPM at that onset and computes
one immutable sample deadline:

```text
durationSeconds = (60 / onsetHostBpm) * quarterNoteUnits
```

| Choice | Quarter-note units | Duration at 120 BPM |
|---:|---:|---:|
| `2n` | `2.0` | `1.000 s` |
| `4n` | `1.0` | `0.500 s` |
| `8n` | `0.5` | `0.250 s` |
| `16n` | `0.25` | `0.125 s` |
| `32n` | `0.125` | `0.0625 s` |

A later Ableton tempo or Note Duration change affects only subsequent new Note Ons;
it does not stretch or shorten an already scheduled tail. If a valid host BPM is not
available, Cosmic owns its bounded deterministic tempo fallback; the server never
supplies one.

The server's lifecycle obligation does not change. It still sends `on 0` immediately,
without quantisation, and Cosmic closes the semantic source ownership immediately.
An ordinary `on 0` does not rewrite or truncate an already scheduled musical tail;
the physical MIDI Note Off is emitted at that Note On's stored deadline. The internal
three-second live-touch watchdog emits a distinct Cancel for the expired semantic
source and ends all its scheduled tails immediately. Panic, route/transport reset, and
capacity-shrink cleanup are the other explicit safety paths that may end tails
immediately. Cancel is internal provenance; the server must not add `/cancel` to the
OSC protocol.

The server MUST NOT infer BPM, send note duration, delay On/Off to approximate the
tail, attach a dated timetag, alter source-to-channel mapping, or add a new OSC
argument/address. Canonical immediate `U,V,On1` and `On0` remain the complete wire
protocol.

### 6.3 Motion rate control is allowed

The server MAY latest-value-coalesce U/V because motion is state, not lifecycle.
Cosmic Microwave measures every admitted U, V, and lifecycle OSC element. Its default
Safety Governor enters HIGH at 1,500 events/s, CRITICAL at 4,000, and EMERGENCY at
8,000 per instance. The normal operating target is therefore at most approximately
1,200 events/s per instance, leaving lifecycle headroom.

For the server LoadGen **Human** profile, use this initial per-zone expressive
motion-rate policy:

```text
expressive_motion_hz <= min(20, 600 / (2 * active_sources))
```

The `600` budget covers expressive U/V only. Lifecycle is never budgeted away. A
separate safety lane republishes the latest canonical U/V pair at most every 900 ms
while a touch remains active. At 256 active sources its finite worst-case reserve is
approximately `2 * 256 / 0.9 = 569` scalar events/s. Ordinary expressive frames
refresh the same deadline, so both lanes do not normally peak together.

| Active sources in one zone | Initial maximum emitted U/V rate |
|---:|---:|
| 32 | 9.4 Hz |
| 64 | 4.7 Hz |
| 77 | 3.9 Hz |
| 125 | 2.4 Hz |
| 250 | 1.2 Hz |

The rate may be refined from measured Venue Console telemetry, but the **total** of
motion, keepalive, and lifecycle must remain below the 1,200 events/s baseline target
in deterministic soak tests, and lifecycle must never be reduced. The calibrated
Human-256 60-second gate measures `1079.6 events/s` average, `1100.0 events/s` P95,
and `1210 events/s` maximum, below the 1,500 events/s Safety HIGH threshold.

Requirements:

- U/V coalescing is keyed by `(zone, source, finger0)`.
- Only the newest U/V pair is retained.
- On and Off are never stored in the U/V coalescer.
- An incoming Off evicts pending U/V for that identity before release is sent.
- While a touch is held, the server MUST emit both a valid U and a valid V heartbeat
  at least once per 900 ms, even if the finger is stationary. The Ready Gate measures
  the two axes separately and allows 1,200 ms only as receiver-side jitter tolerance;
  that tolerance does not relax the server's 900 ms sender maximum. Cosmic
  Microwave's live watchdog closes a touch after a 3,000 ms stale threshold; timer
  cadence makes the practical worst-case release approximately 4 seconds.
- New On must carry the latest U/V in the same atomic group before `on 1`.
- Large timer catch-up bursts are forbidden; skip obsolete motion and send the latest
  state once.

For the currently audited paths, set `VENUE_OSC_HZ` or `CLOUD_OSC_HZ` from the
per-zone policy above rather than one fleet-wide hard-coded value:

- venue-engine deployment: control `VENUE_OSC_HZ`;
- relay deployment: control `CLOUD_OSC_HZ`;
- do not run both OSC-producing paths to the same destination.

The audited control plane persists motion settings in Redis. Environment variables
alone are not authoritative after a runtime change. For the selected deployment
profile, the stored venue configuration must resolve to the per-zone rate budget and
full U/V precision; the health endpoint reports the effective values actually in use.

---

## 7. Zone, bridge, port, and Ableton instance contract

The production topology is deliberately hierarchical. No Cosmic Microwave instance
receives the whole audience:

```text
Venue Engine, all zones -> 127.0.0.1:6061 Venue Bridge

Venue Bridge manifest:
  Zone A -> unique UDP output -> Cosmic A -> Ch1..8 OMNI1 + Ch9..16 OMNI2
  Zone B -> unique UDP output -> Cosmic B -> Ch1..8 OMNI1 + Ch9..16 OMNI2
  Zone C -> unique UDP output -> Cosmic C -> Ch1..8 OMNI1 + Ch9..16 OMNI2
  ...
```

The **bridge route manifest is the only authority for server zone output ports**.
Cosmic Microwave does not derive a zone from received traffic or rewrite the bridge,
and this server contract defines no mandatory `base + zoneIndex` formula. Every OSC
address still carries its zone letter. The plugin's matching Expected Zone is an
independent fail-closed boundary.

Cosmic Microwave 2.8.0 does have a bounded local convenience allocator for a genuinely
fresh plugin instance. It atomically claims the lowest free retained exclusive factory
pair from `6062/A` through `6069/H`, then publishes the matching virtual endpoint and
Conductor role. This does not authorize the server to guess the selected route: the
bridge manifest and Ableton template must still be frozen to the exact resulting or
explicitly configured route before audience admission.

The operator screenshot shows this example manifest:

| Zone | Bridge UDP output | Cosmic setting | Ableton MIDI fan-out |
|---|---:|---|---|
| A | `6062` | UDP `6062`, Expected Zone `A` | Ch1..16 |
| B | `6063` | UDP `6063`, Expected Zone `B` | Ch1..16 |
| C | `6064` | UDP `6064`, Expected Zone `C` | Ch1..16 |
| D | `6065` | UDP `6065`, Expected Zone `D` | Ch1..16 |
| E | `6066` | UDP `6066`, Expected Zone `E` | Ch1..16 |
| F | `6067` | UDP `6067`, Expected Zone `F` | Ch1..16 |
| G | `6068` | UDP `6068`, Expected Zone `G` | Ch1..16 |
| H | `6069` | UDP `6069`, Expected Zone `H` | Ch1..16 |

This table is the compiled Cosmic A-H factory route family and the usual show profile;
it is not a mandatory server formula. The operator may choose different ports for a
test before preflight by saved state, direct edit, or explicit preset. Those explicit
choices win exactly and disable fresh assignment. Once a show profile is selected,
freeze its manifest revision: the saved bridge routes, every Cosmic UDP/Expected Zone
field, and every Ableton virtual-input selection must agree exactly before audience
admission. Changing only one layer—or changing routes after admission—is a failure.

Complete host-restored state is authoritative even if its exact UDP port is occupied;
Cosmic fails closed on that route rather than shifting the saved instance to another
zone. If all A-H factory routes are occupied for a fresh instance, Cosmic opens no OSC
receiver, virtual MIDI endpoint, or Global Conductor registration and does not keep
rescanning. Free the intended route and press **RETRY AUTO**. The server must treat
that state as not ready and must send no participant traffic to the instance.

Server/splitter requirements:

- listen for the mixed-zone Venue Engine stream on the agreed bridge ingress
  (`127.0.0.1:6061` in the current deployment);
- one zone per UDP output port;
- every enabled production zone has an explicit route;
- all active output ports are unique and differ from the ingress and quarantine port;
- no mixed-zone bundle on a zone output;
- stable, explicit zone-to-port table;
- destination host and port exposed in preflight telemetry;
- no two active output pipelines feeding the same zone/port;
- no broadcast of every zone to every port;
- zero wrong-zone packets at the plugin's mismatch counter;
- safe reconfiguration: stop new On, release held identities on the old route, verify
  zero held state, change route, update Cosmic/Ableton, then resume;
- preserve each participant attack as the ordered atomic group `U,V,On1` while
  rebuilding/chunking bundles;
- reject dated input bundles/subtrees instead of silently rebuilding them as immediate;
- treat UDP `7000` as an observable quarantine/sink, never as a production fallback.

Suggested splitter/native-routing configuration:

```yaml
bridgeIngress: 127.0.0.1:6061
routeManifest:
  A: 6062
  B: 6063
  C: 6064
  D: 6065
  E: 6066
  F: 6067
unmappedDestination: 127.0.0.1:7000
unmappedPolicy: quarantine-and-fail-readiness
maxBundleBytesOnLocalhost: 8192
```

The current Venue Bridge output is localhost-only, so its bounded `8192`-byte
datagrams avoid the earlier macOS `EMSGSIZE` failure without crossing a network MTU.
Keep the `1200`-byte recommendation for any route that leaves the host.

### 7.1 Ableton template contract

For every zone, create one Cosmic track, sixteen exact-channel receiver tracks, and
two Omnisphere Multi destinations with eight parts each:

```text
Cosmic <ZONE> (External Only, Normal MIDI, Per Source 1-16)
  Ch1..8  -> OMNI1 Multi Parts 1..8 (one channel per part)
  Ch9..16 -> OMNI2 Multi Parts 1..8 (one channel per part)
```

Each receiver track must listen to the matching zone's port-named virtual endpoint,
`Cosmic Microwave <udp-port> Out`, and exactly one channel. Channels 1..8 feed the
corresponding OMNI1 parts and Channels 9..16 feed corresponding OMNI2 parts. `Host
Only` is not the topology shown here. The two Omnisphere tracks receive only their
eight receiver lanes and must not also subscribe directly to the Cosmic endpoint.
`Mirror` is not allowed because Host and external paths can duplicate the same
lifecycle.

In Ableton MIDI Preferences, enable `Track` for each Cosmic virtual input; leave
`Sync` and `Remote` off. Receiver tracks use `Monitor: In`. Route each receiver
directly to the intended Omnisphere Multi/part and keep the Omnisphere track's own
MIDI input at `No Input`, preventing a second path.

The reviewed Ableton screenshots contain one concrete template defect: both the
`MIDI 11` and `MIDI 12` tracks listen to `Ch.11`; `Ch.12` is absent. Change the
`MIDI 12` input selector to `Ch.12` in every zone template.

At Source Capacity `64`, `128`, or `256`, each Omnisphere part receives respectively
`4`, `8`, or `16` source identities. Capacity never creates extra MIDI channels,
receiver lanes, parts, or Omnisphere instances.

Multiple source IDs still wrap onto the same one of sixteen Normal MIDI channels, but
there is no channel-wide participant expression: Cosmic emits no CC11, CC74, Channel
Pressure, Pitch Bend, RPN, MPE setup, or Crowd Macro CC. Each new Note On carries its
own V-derived attack velocity; later V motion is silent until a new attack/retrigger.
Note ownership and release remain protected by the source ledger.

Within one part, overlapping equal-pitch notes from different source identities depend
on Omnisphere's repeated-note voice semantics after Cosmic's channel+note ownership
protection. This is acceptable only as a deliberate collective texture. The venue test
set must include same-part, equal-pitch overlap and fixed-duration tails.

Two Omnisphere instances are a downstream routing topology, not a guarantee of DAW or
instrument parallelism. Safety Governor protects the Cosmic message/MIDI path but
cannot cap Omnisphere patch, effects, or tail CPU. The exact multis, audio buffer,
Note Duration, capacity profile, and full zone count MUST pass a show-machine CPU and
dropout soak.

### 7.2 Venue Bridge hardened baseline (2026-08-23)

The local Venue Bridge now implements the Cosmic-facing safety boundary:

- the default dashboard profile exposes `A..P` and `VENUE_ZONES=A-Z` enables the
  full alphabet; the operator manifest is still explicit and is never auto-filled;
- the usual show sequence remains `A=6062, B=6063, ...`; different test ports are
  valid while no touch is held and every Cosmic/Ableton endpoint is updated to match;
- an adaptive, lifecycle-bypass bridge governor provides an upper ceiling of about
  `4 Hz` at 125 active sources or `2 Hz` at 250; the Human LoadGen's stricter
  600-event expressive budget remains authoritative upstream, and total accepted
  telemetry must still pass the 1,200 events/s soak gate;
- participant `U,V,On1` groups stay atomic while immediate bundles are rebuilt into
  bounded `<=8192`-byte localhost datagrams;
- dated/nested-dated and malformed input is rejected fail-closed;
- duplicate output ports, bridge ingress `6061`, and quarantine port `7000` are
  rejected as production destinations;
- route/fallback edits are blocked while any live touch is held, and lifecycle
  ledger publication is serialized with route selection and UDP send outcome;
- unmapped zones go to an explicitly counted quarantine path, never a silent
  production fallback;
- the dashboard reports UDP datagrams/s separately from OSC elements/s, plus
  per-zone admitted motion, sampled motion, active source count, send errors, and
  route preflight.

The currently saved manifest deliberately retains only `A=6062 .. F=6067`. Rows
`G..P` are available but remain unmapped until the operator provisions the matching
Cosmic instances and Ableton endpoints. For the usual eight-zone profile, explicitly
save `G=6068` and `H=6069` before preflight. A production zone falling through to
quarantine is a readiness failure.

### Capacity consequence

The parser's physical wire ceiling remains all 256 IDs (`0..255`), while the selected
per-zone Source Capacity controls the production allocator and plug-in admission
domain. Required zone count therefore depends on the frozen capacity profile:

| Source Capacity per zone | Dense IDs per zone | Minimum zones for 2,000 |
|---:|---:|---:|
| `64` | `0..63` | `ceil(2000 / 64) = 32` |
| `128` | `0..127` | `ceil(2000 / 128) = 16` |
| `256` | `0..255` | `ceil(2000 / 256) = 8` |

Eight zones are both the identity-capacity minimum **when every instance selects
256** and a valid production profile if the motion rate is reduced appropriately. At
250 fully active participants per instance, 1.2 Hz expressive U/V pairs create about
600 OSC motion elements/s; the 900 ms safety republish protects a stationary active
touch. At 10 Hz the same zone creates about 5,000 elements/s and intentionally drives
the Safety Governor into CRITICAL pressure.

Minimum-track-count profile:

```text
Zones A..H
Source Capacity 256 in every Cosmic instance
250 participants per zone
Production source IDs 0..249 in every zone
Unique ports selected by the bridge manifest
Expressive latest U/V rate: about 1.2 Hz per active source, plus 900 ms safety republish
```

Optional higher-expression / lower-per-instance-load profile:

```text
Zones A..P
Source Capacity 128 in every Cosmic instance
125 participants per zone
Production source IDs 0..124 in every zone
Unique ports selected by the bridge manifest
Expressive latest U/V rate: about 2.4 Hz per active source, plus 900 ms safety republish
```

The Ableton cost scales separately from the OSC identity cost. The shown Omnisphere
2 x 8 layout uses 19 functional tracks per zone (one Cosmic, sixteen receivers, two
Omnisphere Multi instances), plus an optional zone group/label track: eight zones are
about 152/160 tracks and sixteen zones about 304/320. Safety Governor protects each
Cosmic message/MIDI path; it does not cap CPU used by downstream Omnisphere patches,
effects, or fixed-duration tails. The exact full-template set must therefore pass a
show-machine CPU and dropout soak before choosing 8 versus 16 zones.

Equivalent seating manifest:

```text
QR_ZONES=A:0-124,B:0-124,C:0-124,D:0-124,E:0-124,F:0-124,G:0-124,H:0-124,
         I:0-124,J:0-124,K:0-124,L:0-124,M:0-124,N:0-124,O:0-124,P:0-124
```

The current server gives the Redis `cs:zones` seating plan precedence over boot-time
environment defaults. Updating an env file is not enough: update the live seating
plan and regenerate the QR/token set before the rehearsal.

This preserves the complete 256-ID physical wire contract, including the documented
`source 0 -> Channel 16` and `source 1 -> Channel 1` mapping. Global Conductor
coordinates at most 16 plugin
instances in one process, but it never forwards or merges OSC, sources, or MIDI;
it shares only scalar density and attack/voice budget telemetry between instances.

---

## 8. Identity ownership and reconnect fencing

The current load generator and WebSocket server allow multiple connections to publish
the same `(zone, source)` identity. That is not compatible with Cosmic Microwave's
source-owned lifecycle.

The server MUST implement one active owner per `(zone, source)`:

```text
owner key      = (zone, source)
owner value    = sessionId + monotonically increasing generation
event accepted = event.sessionGeneration == currentOwner.generation
```

The compact internal event format SHOULD carry an epoch/fence and per-session sequence,
for example `e` and `q`. These fields remain inside WebSocket/Redis/SSE transport and
must not be added to the OSC address or arguments.

Required behaviour:

1. A new authorised connection for an occupied identity atomically replaces or rejects
   the previous owner.
2. Replacing an owner first resolves the previous held state with an ordered Off.
3. Events from the old generation are rejected after replacement.
4. A late disconnect from the old socket MUST NOT release the new owner's note.
5. A disconnect from the current owner MUST synthesize `on 0` for its active finger0.
6. An ID is not returned to the free pool until its release has entered the priority
   lifecycle path.
7. Redis, SSE, and multi-replica routing must preserve this fencing information
   internally even though the final OSC address remains unchanged.

This prevents coordinate teleporting, duplicate On, premature Off, false watchdog
heartbeats, and Crowd Governor undercounting.

The load generator currently uses `seat = j % SEATS_PER_ZONE`. That wrap is forbidden
for a correctness test. If requested clients exceed available unique identities, the
test must fail before opening sockets instead of silently reusing IDs.

---

## 9. Lifecycle and backpressure policy

Every server hop must use this priority:

```text
Priority 0: Off, disconnect-derived Off, mute/panic release
Priority 1: On
Priority 2: U/V motion
```

### Mandatory rules

- Off MUST NOT be rejected by a per-client message-rate limiter.
- Off MUST NOT be dropped by Redis publish pressure.
- Off MUST NOT be dropped by SSE or WebSocket backpressure.
- Off MUST bypass motion batching and be sent promptly.
- A compression batch window MAY remain for transport efficiency, but On, Off, and
  session cleanup SHOULD force the pending lifecycle frame toward the network without
  waiting for an obsolete motion batch.
- Disconnect MUST produce an Off for the current fenced owner.
- Queue capacity MUST reserve space for lifecycle release.
- U/V is the first data shed or coalesced under pressure.
- If pressure remains, a new On MAY be rejected rather than creating a note that
  cannot later be released.
- Any lifecycle drop counter in production MUST remain exactly zero.

Cosmic Microwave is deliberately defensive:

- repeated On and Off are handled idempotently;
- lifecycle and motion use separate bounded paths;
- lifecycle drains first;
- U/V collapses to latest state;
- a 3-second live-touch watchdog creates one ordered internal Cancel, closing that
  semantic source and its scheduled MIDI tails immediately;
- Panic sends a bounded 32-message safety sweep: CC123 and CC120 once on each of
  Channels 1 through 16, with no Pressure, Pitch Bend, RPN, or expression CC reset.

Cancel is internal provenance and adds no OSC address. These are safety nets, not
permission for the server to lose lifecycle. A lost server Off can still leave a note
audible for up to the watchdog interval before the internal Cancel.

### Graceful shutdown/mute

On venue mute, service restart, route change, or planned shutdown:

1. stop accepting new On;
2. snapshot all currently owned held identities;
3. generate one Off per identity;
4. chunk releases into safe immediate bundles;
5. send all chunks through the priority path;
6. wait for the bounded sender drain/acknowledged local handoff;
7. clear ownership and close transports.

This sequence resolves server/source ownership; it is not permission for the server
to manufacture MIDI or shorten a fixed musical tail. If the venue requires immediate
silence rather than deadline-complete tails, the operator invokes Cosmic Microwave's
Panic/transport safety reset or the downstream Ableton mute path after lifecycle
drain. The server still emits only OSC On0 releases.

Never put all 2,000 releases into one UDP datagram. At the audited revision, the
single mute bundle reaches about 68,816 bytes for 2,000 held notes and exceeds the UDP
payload limit.

---

## 10. Datagram chunking and atomicity

The chunker input must be participant event groups, not a flat list of OSC messages.

```text
AttackGroup  = [U, V, On1]
MotionGroup  = [U, V]
ReleaseGroup = [On0]
```

Algorithm requirement:

```text
for each complete group:
    encodedGroup = encode(group)
    if currentBundle + encodedGroup exceeds safeDatagramBytes:
        flush currentBundle
    append the entire encodedGroup
flush final bundle
```

The implementation MUST NOT flush after U or V while the matching On remains for the
next datagram. It MUST apply the same bounded chunking to mute/panic releases.

Recommended safe payload is 1,200 bytes. If a venue-specific larger value is chosen,
the server must prove the entire VPN/LAN path MTU and must treat a fragmentation-related
loss as a failed acceptance test. `65,507` bytes is an absolute protocol ceiling, not a
recommended operational target.

---

## 11. Correct simulator/load-generator profile

The server-side simulator has two independent obligations:

1. obey the same OSC identity, single-finger Pad lifecycle, ordering, routing, and
   backpressure rules as a production phone; and
2. reproduce the measured *population distributions* of the Real10 phone capture.

It MUST NOT copy recorded trajectories. Different users and different seeds must
remain independent. For the same seed and configuration, the live worker and offline
trace generator MUST agree on source selection, stable traits, lifecycle decisions,
and model actions before transport effects. Byte identity is required only when the
same offline golden trace is generated repeatedly; a live load-balanced capture is
validated semantically and statistically because its transport timestamps and
framing naturally differ. Cosmic Microwave's local simulator targets the same
statistical behaviour family but is not a byte-parity peer.

### 11.1 Production simulator invariants

A representative production rehearsal MUST use:

```text
profile = Human
mode = pad
MIN_FINGERS = 1
MAX_FINGERS = 1
SOURCE_CAPACITY in {64, 128, 256}, frozen independently for each zone
SEATS_PER_ZONE >= clients assigned to that zone
SEATS_PER_ZONE <= SOURCE_CAPACITY
no source modulo reuse
QUANTISE_NOTES = false
```

The simulator MUST:

- emit one `finger0` attack as one canonical `U,V,On1` group;
- emit zero or more latest-value `U,V` groups while held;
- emit exactly one `On0` at lift, disconnect, forced stop, or trace cleanup;
- never retrigger because U crosses a visual line boundary;
- keep every generated value finite and inside `[0,1]`;
- generate its own Real10-calibrated `0.01` U/V grid without quantising real-phone
  traffic elsewhere in the pipeline;
- expose profile, seed, global source index, zone, source, and session generation;
- allocate the dense zero-based `0..SOURCE_CAPACITY-1` domain and fail before opening
  clients if that selected capacity is insufficient;
- use the same shared behaviour module from live workers and deterministic traces.

The required shared server implementation module is `tools/loadgen/human-model.js`.
Worker, orchestrator, trace, panel, or load-balancer code MUST NOT carry a second copy
of the probability tables. Unknown profile names MUST fail safe to `Human`.

The historical 2–3-finger and line-crossing-retrigger findings in Section 16 describe
the audited `d868587...` revision. They are not the target behaviour.

### 11.2 Calibration authority and reference facts

The Human model is calibrated to:

```text
capture: 10_Kisi_Telefon_Gercek_Kayit.jsonl
sha256: 4509299725685e9134803f1ad74ebe0a31f2ab89e30d6b2b5ac29be91de97401
session: 69.796 s
active packet span: 62.573 s
sources: A/1 ... A/10
packets: 2117
scalar events: 13701
```

The capture contains only `finger0` and canonical U/V/On. Its U/V values are on a
`0.01` grid. One source is still active when recording ends; that gesture is
right-censored and MUST NOT be converted into a natural release for calibration.

Measured population targets:

| Metric | Real10 reference |
|---|---:|
| Events / active-user-second | `36.181` |
| U/V frames / active-user-second | `16.565` |
| Mean concurrent active sources | `6.052` |
| Hold median / P95 / P99 | `150 / 2678 / 7438 ms` |
| Idle median / P95 / P99 | `166.5 / 1164 / 2442 ms` |
| Continuous interval median / P95 / P99 | `51 / 102 / 150 ms` |
| Continuous step median / P95 | `.0412 / .2419` |
| Continuous speed median / P95 | `.6728 / 3.6584 s^-1` |
| Exact unchanged-pair pause fraction | `.237` |
| Step >= .15 fraction | `.143` |
| Turn > 90 degrees fraction | `.165` |
| Occupancy entropy, 10x10 | `.898` |
| Robust zero-lag absolute correlation median / max | `.090 / .347` |
| Strong correlated pairs, absolute r >= .8 | `0` |

This is a limited 10-person/70-second sample. A simulator MUST generalise its
distributions and heterogeneity; it MUST NOT replay or resample whole recorded paths.

### 11.3 Normative Human lifecycle model

The pooled touch target has four duration classes:

| Class | Target share | Range | Within-class median | Lognormal sigma |
|---|---:|---:|---:|---:|
| Tap | `.608` | `30..250 ms` | `100 ms` | `.50` |
| Short drag | `.315` | `250..2000 ms` | `565 ms` | `.64` |
| Long drag | `.070` | `2000..9000 ms` | `3100 ms` | `.55` |
| Extended | `.007` | `9000..20000 ms` | `15000 ms` | `.30` |

Do not sample every user from those pooled weights directly. Assign one stable,
independent lifecycle persona per source:

| Persona | Population weight | Tap / short / long / extended weights | Idle scale |
|---|---:|---|---:|
| Tapper | `.30` | `.820 / .170 / .009 / .001` | `.80` |
| Explorer | `.20` | `.100 / .580 / .280 / .040` | `1.00` |
| Ordinary | `.35` | `.500 / .390 / .100 / .010` | `1.10` |
| Intermittent | `.15` | `.500 / .360 / .130 / .010` | `1.80` |

Persona population weight, different cycle lengths, and idle scales jointly produce
the pooled touch distribution. Motion style and lifecycle persona MUST use separate
random streams; a fast mover must not automatically become a rapid tapper.

Idle is history-dependent:

- after a tap, use a quick-return probability of `.88`;
- after a non-tap, use a quick-return probability of `.35`;
- quick idle is log-uniform `35..300 ms`;
- a tap that leaves its bout uses log-uniform `250..800 ms`;
- other rest is log-uniform `220..2200 ms`;
- multiply by the source's idle scale and divide by `densityScale`;
- `densityScale`/`eps` defaults to `1.0` and affects idle only, never movement speed.

Initial Human activation is independently staggered: `90%` uniform `0..900 ms` and
`10%` uniform `1500..4000 ms`. A fair per-zone token bucket then admits due starts at
`180 starts/s` with burst `16`. It delays only new On; it never gates Off, active
keepalive, or already-held motion.

### 11.4 Normative Human motion and timing model

Every source receives one stable motion persona:

| Persona | Population weight | Speed multiplier range | Pause multiplier range |
|---|---:|---:|---:|
| Still | `.20` | `0.22..0.48` | `2.8..4.6` |
| Gentle | `.35` | `0.48..0.82` | `1.5..2.5` |
| Ordinary | `.25` | `0.86..1.28` | `0.75..1.35` |
| Expressive | `.20` | `1.45..2.15` | `0.38..0.78` |

Source-stable directional preference is horizontal `.50`, vertical `.30`, free
`.20`. Horizontal/vertical sources draw an axis-lock strength in `0.78..0.90`.
Each source also owns a local home region; soft steering begins before a boundary.
Shared sinusoids, shared waypoint cycles, visible deterministic bounce rails, and
fleet-wide periodic motion are forbidden.

Motion rules:

- physics advances on a nominal `50 ms` grid;
- base speed is truncated lognormal: median `1.18 s^-1`, sigma `.72`, maximum
  `6.0 s^-1`, multiplied by stable persona and per-gesture variation;
- a tap is static with probability `.70`; the remainder is a short mini-flick;
- per-physics-frame pause-entry probability is `.052 * pauseMultiplier`;
- pause run length is 1 frame with probability `.78`, 2–3 with `.14`, 4–11 with
  `.07`, and 12–40 with `.01`;
- abrupt-turn probability is `.11` per physics frame; a decisive turn chooses
  approximately `99..180 degrees` in either direction;
- unchanged, quantised U/V pairs during a pause are valid output and MUST remain;
- U/V stays on the simulator's `0.01` grid and is clamped/reflected inside `[0,1]`.

At an unconstrained 20 Hz output target, cadence selects a multiple of the nominal
period:

| Nominal interval | Weight |
|---:|---:|
| `50 ms` | `.732` |
| `100 ms` | `.246` |
| `150 ms` | `.021` |
| `200 ms` | `.001` |

The server cadence layer adds at most approximately `+-2 ms` of model jitter before
the production scheduler. Production worker and offline trace both run a `10 ms`
behaviour tick. A next deadline advances from the previous deadline, not from late
`now`. If a tick is late, obsolete deadlines are consumed but only the newest state is
emitted once; catch-up motion bursts are forbidden.

### 11.5 Determinism, sharding, and count stability

For exact server trace parity:

```text
root = mix32(parseSeed(seed) XOR mix32(globalIndex + 1))
PRNG = Mulberry32
string seed hash = FNV-1a 32-bit followed by mix32

lifecycle stream salt       0x2c1b3c6d
motion stream salt          0x297a2d39
cadence stream salt         0x9e3779b9
network stream salt         0x85ebca6b
motion-trait stream salt    0xc2b2ae35
lifecycle-trait stream salt 0x27d4eb2f
player-selection salt       0xa24baed5
```

The preferred implementation is reuse of the shared module rather than a second
translation of this pseudocode. Required invariants:

- repeating the same seed/configuration produces a byte-identical offline trace;
- every source uses its stable global index, not worker-local position;
- changing worker count does not change source identity or persona;
- increasing fleet size does not reshuffle the first N source traits or lifecycle;
- lifecycle, motion, cadence, network, and trait random consumption stay separated;
- Human uses at most `16` effective workers so aggregate burst `16` remains exact;
- an empty stride worker consumes neither start capacity nor motion-budget estimate.

### 11.6 Profile semantics and traffic governors

| Profile | Purpose | Initial stagger | Desired motion | Expressive U/V scalar budget/zone | Start governor |
|---|---|---:|---:|---:|---|
| Human | Real10 realism and normal soak | `90%: 0..900 ms; 10%: 1500..4000 ms` | `20 Hz` | `600/s` | `180/s`, burst `16` |
| Dense | Lifecycle-correct dense load | uniform `0..1200 ms` | `20 Hz` | `2000/s` | bypass |
| Stress | Lifecycle-correct bounded overload | uniform `0..400 ms` | `20 Hz` | `8000/s` | bypass |

`Human` is the only capture-realism profile. `Dense` and named `Stress` still obey
canonical Pad/`finger0` lifecycle and must not inject malformed packets, collisions,
multi-touch, loss, duplication, or reorder. Those belong to a separately labelled
`Fault`/Chaos test.

The server and plugin Stress ceilings intentionally differ: server Stress has an
`8000` expressive-scalar budget for transport pressure, while Cosmic Microwave's
local Stress generator is lower. Profile names express test intent; only Human shares
the cross-system statistical calibration.

For every profile:

```text
effectiveMotionHz =
  max(0.25, min(desiredMotionHz, motionScalarBudgetPerZone / (2 * activeInZone)))
```

The expressive budget excludes lifecycle and the mandatory latest-U/V keepalive.
Every held source is refreshed within `900 ms`. Human `1/10/16` users form the
realism region; `100/256` users form a lifecycle-safe bounded-load region and are not
expected to retain 20 Hz per person.

### 11.7 Statistical conformance gate

Run five deterministic seeds, each with 10 users, `Human`, `densityScale=1`,
`playerRatio=1`, `tickMs=10`, and 60 seconds. The aggregate of each valid
implementation MUST stay inside these deliberately non-overfitted bands:

| Metric | Real10 | Per-seed acceptance |
|---|---:|---:|
| Events / active-user-second | `36.181` | `33..41` |
| U/V frames / active-user-second | `16.565` | `15.5..17.5` |
| Mean concurrent active | `6.052` | `5.3..6.9` |
| Hold median | `150 ms` | `120..240 ms` |
| Hold P95 | `2678 ms` | `1100..3400 ms` |
| Idle median | `166.5 ms` | `110..220 ms` |
| Continuous interval median / P95 | `51 / 102 ms` | `48..55 / 90..110 ms` |
| Continuous step median / P95 | `.041 / .242` | `.035..060 / .19..27` |
| Continuous speed median / P95 | `.673 / 3.658` | `.55..1.05 / 2.8..4.3` |
| Exact pause fraction | `.237` | `.14..31` |
| Turn > 90 degrees fraction | `.165` | `.14..23` |
| Occupancy entropy | `.898` | `.83..94` |
| Maximum robust zero-lag absolute correlation | `.347` | `< .40` |
| Strong absolute-correlation pairs >= .8 | `0` | exactly `0` |

The fleet must also exhibit between-person diversity rather than ten near-identical
users. The reference spans per-source median speed `0..2.007 s^-1`, pause fraction
`.042..551`, duty cycle `.442..964`, and onset count `10..160`. These ranges are
diagnostic evidence, not hard per-seed quotas.

Canonical server five-seed identifiers are `real10-ms-1` through `real10-ms-5`.
Exact trace hashes and canonical analysis tree hashes are recorded in
`docs/analysis/crowd-simulator-real10-2026-08-24/reproducibility.json`.

Golden primary fixture:

```sh
node tools/loadgen/trace.js \
  --users 10 \
  --seconds 80 \
  --profile Human \
  --seed calibration-2026-08-23 \
  --player-ratio 1 \
  --tick-ms 10 \
  --human-start-rate 180 \
  --human-start-burst 16 \
  --output /tmp/server-human10.jsonl

shasum -a 256 /tmp/server-human10.jsonl
# 991ee47531561c2c94817e047b18ed9abd9a04cfbf994ed23257b577f8f63b43
```

The Human scale gate MUST cover `1, 10, 16, 100, 256` users. At 256 users and
60 seconds the server reference is `1079.6` scalar events/s average, `1100.0` P95,
and `1210` maximum. The normal safety gate is average `<=1200` and P95 `<1500`.

### 11.8 Trace and live-path semantics

Offline JSONL MUST preserve `session_start`, `osc_packet`, and `session_end` records
plus enough metadata to separate model actions:

- `start`, `motion`, `keepalive`, and `end` remain distinguishable;
- an Off forced only because trace time ended is marked
  `completion_reason=simulator_cleanup`;
- cleanup Off remains in packet/event traffic and closes replay lifecycle;
- cleanup Off is right-censored and excluded from natural hold/idle distributions;
- naturally due release remains `completion_reason=packet_end`.

`session_start` MUST identify the simulator contract, model revision/source hash,
profile, seed, requested/effective user and worker counts, zone/seat allocation,
`densityScale`, `playerRatio`, behaviour tick, physics interval, desired motion rate,
expressive budget, keepalive, and start-governor settings. `session_end` MUST report
packet/event totals plus natural, cleanup, and right-censored lifecycle counts. This
metadata is part of reproducibility; a trace without it is diagnostic evidence, not a
golden fixture.

Offline trace proves deterministic model and wire invariants. It does not prove
WebSocket, Redis, load-balancer, worker scheduling, UDP, Venue Bridge, OS-buffer,
jitter, drop, or packet-coalescing behaviour. After deployment, final acceptance MUST
analyse a downstream Venue Bridge JSONL capture with the same analyzer and compare it
to the Real10 envelope.

A production-path Human rehearsal MUST enter through the same public WebSocket/load-
balancer endpoint as real phones and traverse the real session, Redis/hub, OSC, UDP,
and Venue Bridge path. A control panel or load-balancer console MAY launch and observe
LoadGen, but the load balancer MUST NOT contain a second motion model, manufacture
U/V/On events, or reshape lifecycle. A direct-to-worker or direct-to-UDP run must be
labelled `transport_bypass=true` and cannot satisfy the production-path acceptance
gate.

### 11.9 2,000-user rehearsal profiles

```text
minimumZonesAtCapacity256: 8
exampleMinimumProfile: 8 zones x capacity 256 x 250 participants x 1.2 Hz expressive U/V
exampleHigherExpressionProfile: 16 zones x capacity 128 x 125 participants x 2.4 Hz expressive U/V
sourceCapacityPerZone: independently frozen as 64, 128, or 256
sourceIdsPerZone: stable, unique, dense 0..(sourceCapacity-1)
finger: 0
mode: pad
phoneUvHz: 20
venueOscHz: selected from the most-populated zone's rate budget
targetAcceptedEventsPerSecondPerInstance: <=1200
serverQuantisation: false
interactionPath: exactly one of venue-engine OR relay
```

Production-scale rehearsal uses `Human`. Dense and Stress are explicit pressure
tests, not representations of 2,000 naturally moving people.

---

## 12. Single-path topology rule

The repository contains two ways to produce equivalent OSC:

```text
Path A: hub/SSE -> venue-engine -> UDP
Path B: osc-bridge -> venue-relay -> UDP
```

Exactly one path may be active for a venue/zone destination. Running both creates a
full duplicate OSC stream with no event ID available at Cosmic Microwave for dedupe.

The venue control plane MUST expose:

- selected active path;
- the inactive path explicitly disabled;
- OSC producer version/commit;
- splitter version/commit;
- zone-port table;
- quantisation state, which must read `false`;
- motion output rate;
- largest emitted datagram;
- UDP send error count.

---

## 13. Required server telemetry

Expose these counters globally and per zone. They are required to make Venue Preflight
and failure reports actionable.

### Identity

- connected sockets;
- unique active `(zone, source)` owners;
- selected Source Capacity and admitted ID domain per zone;
- server-side over-capacity admission rejects per zone;
- Cosmic Microwave capacity-drop telemetry per zone/instance — expected zero;
- owner replacements;
- rejected stale-generation events;
- duplicate identity attempts;
- free/used identities per zone.

### Lifecycle

- On accepted/sent;
- Off accepted/sent;
- disconnect-derived Off sent;
- On rejected under pressure;
- Off dropped — this MUST remain zero;
- active held identities;
- forced releases during mute/shutdown.

### Motion

- raw U/V received;
- U/V pairs emitted;
- U/V pairs coalesced;
- U/V pairs shed;
- current configured U/V Hz;
- oldest pending motion age.

### OSC/UDP

- datagrams sent per destination;
- bytes sent;
- send errors, including `EMSGSIZE`;
- current and maximum datagram size;
- participant groups per datagram;
- wrong-zone routing attempts;
- immediate versus dated bundle count;
- active OSC pipeline name.

### Queue health

- depth and high-water mark by priority lane;
- oldest item age by lane;
- Redis publish in-flight count;
- SSE/WebSocket buffered bytes;
- reconnect and backoff counts.

### Simulator/LoadGen

- simulator contract and model revision/source hash;
- effective profile and unknown-profile fallback count;
- simulation transport path and `transport_bypass` state;
- seed, `densityScale`/`eps`, and `playerRatio`;
- requested and effective worker-process count;
- desired and effective motion Hz per zone;
- expressive scalar budget and keepalive count per zone;
- Human start rate, burst, admitted starts, delayed starts, and token occupancy;
- active source estimate used by motion pacing;
- skipped obsolete deadlines and forbidden catch-up-burst count;
- natural releases, cleanup releases, and right-censored gestures;
- current golden-fixture and statistical-conformance result.

Recommended health response:

```json
{
  "contract": "cosmic-microwave-osc-v1",
  "ready": true,
  "quantiseNotes": false,
  "activeOscPath": "venue-engine",
  "fingerPolicy": "finger0-only",
  "sourceIdBase": 0,
  "motionHz": 4,
  "largestDatagramBytes": 1184,
  "motionDrops": 0,
  "lifecycleDrops": 0,
  "udpErrors": 0,
  "bridgeIngress": "127.0.0.1:6061",
  "routeManifestRevision": "<immutable-show-revision>",
  "loadgen": {
    "contract": "cosmic-microwave-human-simulator/v1",
    "modelRevision": "real10-2026-08-24",
    "profile": "Human",
    "transportPath": "public-load-balanced",
    "transportBypass": false,
    "seed": "show-2026",
    "densityScale": 1,
    "playerRatio": 1,
    "tickMs": 10,
    "requestedWorkers": 32,
    "effectiveWorkers": 16,
    "desiredMotionHz": 20,
    "expressiveScalarBudgetPerZone": 600,
    "activeKeepaliveMaxMs": 900,
    "startRatePerZone": 180,
    "startBurstPerZone": 16
  },
  "zones": {
    "A": { "port": 6062, "sourceCapacity": 256, "owners": 250, "overCapacityRejects": 0, "collisions": 0 },
    "B": { "port": 6063, "sourceCapacity": 256, "owners": 250, "overCapacityRejects": 0, "collisions": 0 }
  }
}
```

`ready` must become false when any mandatory contract item is violated.

---

## 14. Venue Preflight checklist

The show must not admit the audience until all mandatory rows pass.

| Check | PASS condition | Failure severity |
|---|---|---|
| Server contract/version | expected release/commit and Cosmic 2.8.0 state schema 11 deployed | FAIL |
| OSC path | exactly one producer path active | FAIL |
| Quantisation | false at every OSC producer | FAIL |
| Zone routing | one zone per mapped port | FAIL |
| Expected Zone | each plugin locked to matching A..Z | FAIL |
| UDP ownership | Cosmic instance has exclusive listener | FAIL |
| Source Capacity | server manifest and Cosmic agree on `64`, `128`, or `256` per zone | FAIL |
| Identity capacity | dense `0..capacity-1` has unique IDs for all admitted phones | FAIL |
| Over-capacity drops | server rejects and Cosmic capacity-drop counter both zero | FAIL |
| Duplicate owners | zero | FAIL |
| Lifecycle drops | zero | FAIL |
| UDP errors | zero, including EMSGSIZE | FAIL |
| Datagram size | within configured safe MTU | FAIL |
| Simulator mode | Pad, finger0-only for production rehearsal | FAIL |
| Simulator model | `cosmic-microwave-human-simulator/v1` and frozen seed visible | FAIL |
| Simulator parity | live worker and trace use the same model revision | FAIL |
| Simulator transport | public load-balanced phone path; `transport_bypass=false` | FAIL |
| Human calibration | five-seed Section 11.7 envelope passes | FAIL for production rehearsal |
| Production-path traffic soak | at least 60 seconds through the real load-balanced venue path; average `<=1200` accepted scalar events/s per instance and P95 `<1500` | FAIL |
| Motion rate | dynamically configured from active source count | WARN if unexpected |
| Message freshness | valid OSC received recently on every zone | FAIL |
| Cosmic Source Quality epoch | arm the capacity-labelled check; every identity in exact `0..capacity-1` emits finite U, V, and On-1, remains actively held, and completes the two-second clean hold | FAIL |
| Active heartbeat | every expected held source keeps U and V separately fresh within the plug-in's 1200 ms receiver tolerance; server sender gap still remains `<=900 ms` | FAIL |
| Source Quality motion rate | combined U/V `<=50 events/s` per source and `<=1200 events/s` in aggregate for the instance | FAIL |
| Internal Cosmic simulator | population exactly zero while the local census is armed | FAIL |
| Source Quality motion drops | armed-epoch motion-drop delta is zero and reported separately | FAIL |
| Source Quality lifecycle drops | armed-epoch lifecycle-drop delta is zero and reported separately | FAIL |
| Wrong-zone count | zero on every Cosmic instance | FAIL |
| Bridge manifest | revision frozen; every production zone has one unique output | FAIL |
| MIDI route | matching port-named endpoint, External Only, exact Ch1..8 -> OMNI1 Parts1..8 and Ch9..16 -> OMNI2 Parts1..8 sweep | FAIL |
| Performance MIDI | participant traffic is Note On/Off only; U=pitch, V=Note-On velocity | FAIL |
| Note Duration | plug-in owns `2n..32n`; current choice and host BPM are visible | FAIL |
| Forbidden MIDI | zero CC11/74/20-23, Pressure, Pitch Bend, RPN, or MPE setup | FAIL |
| Panic/mute drill | all active notes released | FAIL |

The plug-in Ready Gate is deliberately a local accepted-OSC **signal census**. It does
not replace the server's connected-owner roster (`owners`, duplicate allocation,
collision, disconnect, and load-balancer health), the frozen bridge route manifest, or
the independent 60-second production-path soak and its average/P95 evidence. The
internal Cosmic simulator never contributes evidence and its population must be zero;
external load-balancer rehearsal clients may qualify only by traversing the accepted
live OSC path.

While the epoch is WARMING, Cosmic holds only new attacks; Note Off, watchdog Cancel,
Panic, and already sounding notes remain available. READY requires every expected
identity in `0..capacity-1` to remain On and keep U and V independently fresh for the
entire two-second clean hold. The `1200 ms` age is receiver tolerance, not a new sender
cadence: the server maximum remains `900 ms`. Combined U/V motion must remain
`<=50 events/s` per source and `<=1200 events/s` for the instance. Capacity, motion,
and lifecycle drops are distinct telemetry; lifecycle loss must never be hidden inside
a generic router-drop total.

Recommended start sequence:

1. Start Redis/server/control plane.
2. Start exactly one venue OSC path.
3. Start the zone splitter and load its explicit route table.
4. Open Ableton and all Cosmic Microwave instances.
5. Compare the frozen bridge manifest with every Cosmic UDP port, Expected Zone,
   Source Capacity, exclusive listener, and port-named virtual endpoint.
6. Run a complete source/channel sweep and verify exactly one receiver for each
   Ch1..16; specifically verify the `MIDI 12` lane listens to Ch12.
7. Capture participant MIDI and verify Note On/Off only, including silence on held V
   motion and no CC11/74/20-23, Pressure, Pitch Bend, RPN, or MPE setup.
8. Send overlapping equal-pitch notes from two identities into the same Omnisphere
   part; verify ownership and stored duration tails do not cut the other voice
   unexpectedly.
9. Confirm the two 8-part Omnisphere multis pass a full-zone CPU/dropout soak with the
   planned patches, buffer, Note Duration, and Source Capacity.
10. Run 25-source and 125-source staged tests, then an intentional 250-source stress
   profile at the corresponding lower motion rate.
11. Run disconnect and mute/panic drills; verify panic emits only CC123/CC120, once
    per channel, and clears every sounding note.
12. Confirm all counters return to zero held sources.
13. Only then enable audience admission.

---

## 15. Acceptance tests

### 15.1 Protocol conformance

For every zone:

- send source 1 attack as float32 U, float32 V, int32 On1 in one immediate group;
- verify source 1 produces MIDI Channel 1 in Normal/Per Source mode;
- send source 16 and verify Channel 16;
- send source 17 and verify Channel 1;
- send source 0 and verify Channel 16;
- for capacities 64/128/256, verify the admitted domain is exactly
  `0..63`/`0..127`/`0..255` and the fixed 16 channels receive exactly 4/8/16
  identities each;
- at capacity 64 and 128, send one finite `/u` element for the first legal-wire ID
  above the selected limit, verify no MIDI output, and verify exactly one
  capacity-drop telemetry increment rather than modulo wrapping; at capacity 256,
  verify source 255 is admitted and source 256 is rejected by the physical OSC
  grammar;
- verify U selects the mapped MIDI pitch;
- verify attack velocity is `clamp(round(V * 127), 1, 127)`, including V=0 -> 1 and
  V=1 -> 127;
- move V while held and verify it updates stored state but emits no MIDI message;
- move U inside the same mapped-note region and verify it emits no MIDI message;
- cross a mapped-note boundary with U and verify the same source/channel remains the
  owner, the new pitch receives one Note On, and the prior pitch receives Note Off
  only at its own stored duration deadline;
- send On0 immediately and verify semantic ownership closes without changing the
  precomputed tail; at its stored Note Duration deadline verify NoteOff uses the
  source-owned channel;
- for all participant traffic, assert every generated MIDI message is Note On or
  Note Off;
- assert participant traffic produces zero CC11, CC74, CC20-23, Channel Pressure,
  Pitch Bend, RPN, MPE setup, or other controller messages;
- verify no dated bundle is emitted;
- verify no `finger1..9`, `/line`, or legacy address appears.

### 15.2 Zone isolation

- send `/cs/A/1/...` only to `routeManifest[A]` (for example `6062`);
- send `/cs/B/1/...` only to `routeManifest[B]` (for example `6063`);
- verify both independently produce Channel 1 on their own virtual endpoints;
- verify wrong-zone and mixed-zone counters stay zero;
- feed a deliberately mixed upstream bundle and verify the splitter rebuilds two
  correctly ordered per-zone outputs.

### 15.3 Identity fencing

- connect one owner for `(A,1)` and activate it;
- connect a replacement owner for `(A,1)`;
- verify the old owner is fenced;
- send a late old-owner move and disconnect;
- verify neither affects nor releases the new owner;
- disconnect the current owner and verify an immediate synthetic On0.

### 15.4 Backpressure

Force every configured pressure threshold:

- U/V may coalesce/drop first;
- new On may be rejected at the documented overload boundary;
- Off and disconnect release must still be delivered;
- lifecycle drop counter must remain zero;
- queue age must recover without an unbounded backlog.

### 15.5 Datagram integrity

- generate 2,000 simultaneous attacks;
- decode every emitted datagram;
- verify each U/V/On group is complete and ordered inside one datagram;
- verify every datagram is within the configured safe size;
- generate mute with 2,000 held sources;
- verify all releases are chunked, all are delivered, and no `EMSGSIZE` occurs.

### 15.6 Timing ownership

- assert `QUANTISE_NOTES=false` at every producer;
- send random-time On/Off and verify the server does not snap them to wall-clock slots;
- select Cosmic Flow and verify arrival timing remains direct;
- select Cosmic Grid/Ensemble and verify Ableton PPQ is the only musical grid;
- change Ableton tempo and verify no server configuration changes are required.
- for `2n/4n/8n/16n/32n`, start one note at 120 BPM and verify physical MIDI NoteOff
  after exactly `1/.5/.25/.125/.0625` seconds respectively;
- start a `4n` note at 120 BPM, change Ableton to 60 BPM while it is held, and verify
  that note retains its 0.5-second onset deadline while the next new `4n` Note On
  receives a 1-second deadline;
- verify the server packet capture remains canonical immediate U/V/On only throughout
  the tempo and Note Duration changes.

### 15.7 Full 2,000-user rehearsal

- `N >= 8` enabled zones with a frozen per-zone capacity in `64/128/256`, total
  selected capacity at least 2,000, and no zone assigned more owners than its own
  selected capacity;
- test the actual frozen bridge route manifest, not a derived base-port formula;
- one finger per client, Pad mode;
- emitted latest U/V rate selected so every instance remains within its baseline
  element-rate budget (for example 8x250@1.2 Hz or 16x125@2.4 Hz expressive U/V,
  plus the 900 ms safety republish);
- baseline admitted traffic at or below approximately 1,200 events/s per instance;
- deterministic touch lifecycle plus reconnect/churn phases;
- zero identity collisions;
- zero wrong-zone packets;
- zero lifecycle drops;
- zero UDP send errors;
- all 2,000 identities released at test end;
- Cosmic Microwave Safety Governor does not remain CRITICAL/EMERGENCY after load ends;
- Panic/mute produces silence and zero held sources on every zone;
- ordinary participant traffic remains Note On/Off only at full load;
- Panic emits exactly CC123 and CC120 once per MIDI Channel 1..16 (32 controller
  messages total) and emits no CC11/74/20-23, Pressure, Pitch Bend, RPN, or MPE setup.

### 15.8 Chaos rehearsal

Use Cosmic Microwave's external Chaos Lab after the splitter to capture raw datagrams
with timing, replay them, and inject deterministic loss, duplication, reorder, jitter,
and burst loss. Capture/replay/proxy are suitable for this contract. At the time of
this handoff, the Chaos Lab **generator** documentation still describes legacy
On-before-U/V and argless `/off`; do not use generator mode as a conformance oracle
until it is updated to U/V/On1 and On0. Text copied from a Max `print:` window is
insufficient because it loses OSC typetags, bundle boundaries, sender identity,
datagram size, and timestamps.

See [`docs/chaos-lab.md`](chaos-lab.md).

### 15.9 Human simulator conformance

- run the Section 11.7 golden command twice and require byte-identical JSONL;
- require its SHA-256 to match the contract fixture for the frozen model revision;
- run `real10-ms-1..5`, 10 users, 60 seconds, and require every statistical band in
  Section 11.7 to pass;
- run `1/10/16/100/256` and require canonical identity, `finger0`-only output,
  finite `[0,1]` U/V, one `U,V,On1` per start, and exactly one matching Off;
- verify source personas and lifecycle do not change when worker count changes;
- verify the first N uncapped source streams remain stable when fleet size increases;
- delay one scheduler tick deliberately and verify no replay/catch-up motion burst;
- verify `playerRatio` selection is identical in live worker and offline trace;
- verify an EOF-forced Off is counted as traffic, labelled `simulator_cleanup`, and
  excluded from natural hold/idle samples;
- run the production Human rehearsal through the public load-balanced phone endpoint
  and verify the load balancer neither generates nor rewrites U/V/On lifecycle;
- after deployment, capture after the actual load balancer and Venue Bridge, then
  rerun schema, lifecycle, timing, packet burst, jitter, and drop analysis.

### 15.10 Notes Only MIDI conformance

- run the test with an empty host MIDI input so MIDI Thru cannot be mistaken for a
  Cosmic-generated message;
- activate every source/channel mapping and require participant output to contain
  only Note On and Note Off;
- hold a source and sweep V through its full range; require zero emitted MIDI until a
  new attack or U-driven pitch retrigger, then require the latest V as Note-On velocity;
- hold a source and move U within one mapped-note region; require zero emitted MIDI;
- cross one mapped-note boundary and require one new-pitch Note On on the source-owned
  channel, no controller message, and the old-pitch Note Off only at its stored
  duration deadline;
- send ordinary On0 before a tail deadline and require the physical note to continue
  until that stored deadline; separately let a live source exceed the three-second
  heartbeat timeout and require one internal Cancel to silence every one of that
  source's scheduled tails immediately, with no `/cancel` OSC element;
- require zero CC11, CC74, CC20-23, Channel Pressure, Pitch Bend, RPN, MPE setup, and
  Crowd Macro CC under Flow, Grid, Ensemble, Adaptive Governor, and Global Conductor;
- invoke Panic with active notes and require exactly one CC123 plus one CC120 on each
  Channel 1..16, 32 controller messages total, and no other generated controller;
- repeat after state recall from an older preset and verify legacy MPE/controller
  settings cannot reactivate non-Notes-Only output.

### 15.11 Source Capacity conformance

- on a fresh instance, verify Source Capacity `64`; restore a schema-9-or-earlier
  project with no capacity node and verify migration to `256` preserves its IDs;
- for each capacity, sweep every legal identity and verify the fixed mapping
  `source 0 -> Ch16`, `source 1 -> Ch1`, then modulo 16, with 4/8/16 sources on each
  channel;
- at capacity 64 and 128, reject the first and last over-capacity physical IDs without
  MIDI output, ledger mutation, population drift, or active-count change; verify the
  bounded capacity-drop telemetry increments and saturates safely. At capacity 256,
  all physical IDs are admitted and source 256 remains a parser/wire-range rejection;
- shrink `256 -> 128 -> 64` with upper-domain sources active and verify deterministic
  cleanup or a bounded safety reset leaves zero stuck notes and zero upper-domain
  owners;
- expand `64 -> 128 -> 256` and verify existing IDs keep their source/channel
  ownership while newly admitted IDs become available;
- configure two zones with different capacities and verify admission, counters,
  mapping, shrink, and expansion remain independent.

---

## 16. Findings against the audited server revision

These are the known incompatibilities at server commit `d868587...` and are the first
work items for the server team.

This section is a historical audit of that immutable revision. The local 2026-08-24
LoadGen candidate addresses the simulator-specific findings and passes its tests, but
it was not deployed, restarted, committed, or pushed as part of the calibration work.
After a server release, use Sections 11 and 15 plus the deployed commit/telemetry as
the current truth; do not infer present failure merely from this historical list.

### Blocker A — source identity collision

The load generator wraps source IDs with `seat = j % SEATS_PER_ZONE`, so excess clients
reuse active identities. The default 3,000-client/26-zone/100-seat profile creates
hundreds of duplicate owners.

Reference: [`tools/loadgen/worker.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/tools/loadgen/worker.js#L403-L425)

Do not repair this by adding `+1` in the OSC encoder. Source `0` is valid and maps to
MIDI Channel 16. The QR/seating plan, token validation, load generator, active-owner
registry, and OSC encoder must preserve the selected dense
`0..sourceCapacity-1` identity domain end-to-end without modulo reuse, within the
canonical physical `0..255` wire ceiling. The current zone validator and token decoder
already accept source `0`; the load generator and admission path must retain that
behaviour:

- [`server/hub/src/zones.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/hub/src/zones.js#L23-L26)
- [`server/ws-server/src/token.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/ws-server/src/token.js#L36-L53)

### Blocker B — lifecycle can be shed

Redis publish pressure, per-client rate limiting, and hub SSE backpressure do not all
reserve a lossless Off path.

References:

- [`server/ws-server/src/index.js` publish shedding](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/ws-server/src/index.js#L217-L233)
- [`server/ws-server/src/index.js` inbound limit](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/ws-server/src/index.js#L303-L339)
- [`server/hub/src/index.js` SSE backpressure](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/hub/src/index.js#L469-L495)

### Blocker C — disconnect does not produce OSC Off

The WebSocket server emits a disconnect event, but osc-bridge removes held bookkeeping
without sending the corresponding On0 to the DAW.

References:

- [`server/ws-server/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/ws-server/src/index.js#L334-L340)
- [`server/osc-bridge/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/index.js#L494-L503)

### Blocker D — event groups are flattened before chunking

Large batches can split one participant's U/V/On group across datagrams.

References:

- [`server/osc-bridge/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/index.js#L290-L303)
- [`server/osc-bridge/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/index.js#L400-L410)

### Blocker E — mute release can exceed UDP capacity

The relay mute path creates one bundle for all held releases. Approximately 2,000 held
sources produce a datagram larger than UDP can send.

References:

- [`server/osc-bridge/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/index.js#L319-L331)
- [`server/venue-relay/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/venue-relay/src/index.js#L41-L49)

### Blocker F — simulator is not the production interaction model

The real client defaults to Pad, but loadgen ignores mode, defaults to 2–3 fingers,
restarts lifecycle at line crossings, and retains a movement dead-zone removed from
the real phone client.

References:

- [`client-v2/src/main.ts`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/client-v2/src/main.ts#L29)
- [`tools/loadgen/worker.js` mode handling](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/tools/loadgen/worker.js#L239-L244)
- [`tools/loadgen/worker.js` lifecycle](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/tools/loadgen/worker.js#L355-L386)
- [`tools/loadgen/worker.js` dead-zone](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/tools/loadgen/worker.js#L98-L106)

### Blocker G — server quantisation conflicts with Cosmic timing

The audited deployment defaults `QUANTISE_NOTES` to true and uses wall-clock timing.
Cosmic Microwave requires it to be false.

References:

- [`docker-compose.yml`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/docker-compose.yml#L137-L156)
- [`server/osc-bridge/src/quantiser.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/quantiser.js#L49-L69)

### Blocker H — repository emits to one UDP port

The server currently uses one `OSC_UDP_PORT`; the external zone splitter is therefore
required for the production topology.

References:

- [`server/osc-bridge/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/osc-bridge/src/index.js#L8-L12)
- [`server/venue-engine/src/index.js`](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/venue-engine/src/index.js#L35-L41)

### Documentation/config drift to fix with the implementation

- The server README still presents removed `/cs/note/on`, `/cs/note/off`, and
  `/cs/touch` addresses as current. Only the Section 4 contract is production.
  [README reference](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/README.md#L93-L103)
- Some prose says nginx uses `least_conn`, while the deployed nginx configuration is
  round-robin. Operational documentation must name the real policy.
  [README](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/README.md#L154-L160),
  [nginx.conf](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/infra/nginx.conf#L37-L103)
- The OSC page must not suggest punctuation zones after Z. Cosmic Microwave and the
  current server registry support exactly A through Z.
- The old server quantiser describes `SUBDIVISIONS=16` as sixteenth notes, but its
  `beatDuration / 16` formula yields 31.25 ms at 120 BPM rather than the musical 125 ms
  sixteenth note. Production avoids this entirely by disabling server quantisation.
- Venue Relay documentation describes a 500-message queue, but the audited path sends
  frames directly to UDP. Health/preflight must describe implemented behaviour, not an
  unused constant.
  [venue-relay](https://github.com/CosmicSymphonyCreative/cosmicsymphony-audience-interaction/blob/d86858791ee70f407bf0576ba33f03c57c75f81f/server/venue-relay/src/index.js#L37-L57)

---

## 17. Observed capture baseline

### 17.1 Real10 phone calibration baseline

The authoritative realism capture contains 10 actual phone participants:

- `2117` timed packets and `13701` scalar events across `69.796 s`;
- 10 independent `A/1..A/10` sources and only `finger0`;
- `6273 U`, `6273 V`, `578 On1`, and `577 On0`;
- no malformed address, missing argument, NaN/Inf, or out-of-range U/V;
- continuous recorder packet and event sequences;
- `1653` OSC bundles and `464` plain/direct Off packets;
- 12 same-packet Off+On rapid retriggers in real behaviour;
- one active source at EOF, treated as right-censored rather than corrupt.

Real arrival timestamps combine phone, WebSocket, load balancer, server, bridge,
network, and recorder scheduling. They are an end-to-end behavioural reference, not a
pure phone-clock measurement. The Human model therefore matches distributions and
micro-variation without claiming to identify which hop caused each jitter sample.

See [`Crowd Simulator Calibration — 2026-08-24`](analysis/Crowd-Simulator-Calibration-2026-08-24.md)
and its [`reproducibility manifest`](analysis/crowd-simulator-real10-2026-08-24/reproducibility.json).

### 17.2 Earlier Max print captures

Two supplied Max captures contained 22,478 messages in total:

- every parsed address matched `/cs/A/<source>/finger0/{u|v|on}`;
- no malformed, non-finite, or out-of-range payload was visible;
- every U was immediately followed by matching-source V;
- every On1 was immediately preceded by matching-source U/V;
- the captures include source `0`; it is a normal production identity and maps to
  Channel 16;
- the first capture contained seven repeated lifecycle states in one local cluster;
- the second capture contained no repeated lifecycle state;
- only Zone A was observed, so Zone B and cross-zone isolation remain unverified.

The first cluster is consistent with simulator source collisions and line-mode
retriggering. Cosmic Microwave handles repeated On/Off idempotently, but this upstream
behaviour makes the simulator an inaccurate production model.

Max console text cannot prove OSC typetags, bundle atomicity, datagram size, timestamp,
jitter, packet loss, or sender identity. Final acceptance requires raw timed capture.

---

## 18. Server-agent implementation directive

The following block can be pasted directly into a server coding-agent task:

```text
Objective:
Make CosmicSymphonyCreative/cosmicsymphony-audience-interaction comply with
docs/Cosmic-Microwave-Server-Integration-Contract.md for Cosmic Microwave 2.8.0.

Hard constraints:
- Do not modify Cosmic Microwave timing, source-to-MIDI mapping, watchdog, or OSC parser.
- Set QUANTISE_NOTES=false for the Cosmic deployment.
- Preserve canonical immediate OSC: /cs/A-Z/0..255/finger0/{u,v,on}. Freeze Source
  Capacity 64/128/256 independently per zone and allocate only its dense
  0..capacity-1 range without wrap or collision.
- Keep exactly 16 MIDI channels at every capacity: source 0->Ch16, source 1->Ch1,
  then fixed modulo-16 mapping, yielding 4/8/16 sources per channel.
- Treat out-of-capacity source messages as admission failures: drop and expose
  telemetry; never wrap them. Coordinate shrink so upper identities release and
  cleanup/reset before admission resumes; expansion must not remap existing IDs.
- float32 U/V, int32 On 0/1; attack order U,V,On1.
- Preserve OSC semantics: U selects pitch, V supplies the next Note-On velocity, and
  On owns activation/release. Do not remove held-touch U/V updates or the 900 ms
  heartbeat merely because Cosmic's participant MIDI output is Notes Only.
- Cosmic participant MIDI is Note On/Off only. Do not expect or synthesize CC11,
  CC74, CC20-23, Channel Pressure, Pitch Bend, RPN, MPE, or Crowd Macro CC. Panic's
  only controller exception is CC123/CC120 on Channels 1..16.
- Note Duration 2n/4n/8n/16n/32n belongs entirely to Cosmic Microwave. Every new
  MIDI Note On uses its onset host-BPM snapshot. Do not send BPM/duration, delay OSC,
  add an argument/address, or alter mapping/timing to imitate this tail.
- Ordinary On0 ends semantic touch state without truncating a stored musical tail.
  Cosmic's three-second watchdog uses an internal Cancel to end the expired source and
  all its scheduled tails immediately; do not add `/cancel` to OSC.
- One stable owner per (zone,source); no modulo ID collision.
- One production finger: finger0, Pad lifecycle.
- Off/disconnect release is never rate-limited, queued behind motion, or shed.
- Disconnect of the current fenced owner must synthesize On0.
- Chunk complete participant groups; never split U/V/On across datagrams.
- Chunk 2,000-source mute/release; no UDP payload over the configured safe MTU.
- Emit one separated zone per configured UDP port.
- Send the mixed-zone Venue Engine stream to bridge ingress 127.0.0.1:6061.
- Treat the frozen bridge route manifest as the sole zone-port authority; require
  unique outputs, exact Expected Zone matches, and quarantine unmapped traffic.
- For the shown Ableton template use External Only and the port-named virtual MIDI
  endpoint; never Mirror or a simultaneous Host route.
- Route Channels 1..8 to OMNI1 Multi Parts 1..8 and Channels 9..16 to OMNI2 Multi
  Parts 1..8. Treat this as routing, not guaranteed CPU parallelism, and pass a
  full-template show-machine CPU/dropout soak.
- Keep baseline accepted traffic near or below 1200 events/s per Cosmic instance;
  adapt only U/V rate, never lifecycle.
- Implement cosmic-microwave-human-simulator/v1 from Section 11. Live worker and
  offline trace must import one shared model; do not duplicate probability tables.
- Send production Human clients through the same public load-balanced WebSocket path
  as phones. The load balancer may route and observe them but must not implement a
  second behaviour model or rewrite U/V/On lifecycle.
- Human defaults: 10 ms scheduler, 50 ms physics, 20 Hz desired cadence, 600
  expressive U/V scalars/s/zone, 900 ms keepalive, 180 starts/s/zone, burst 16,
  and no more than 16 effective Human workers.
- Derive source-stable, independent lifecycle/motion/cadence/network/trait streams
  from seed plus global source index. Fleet size and worker sharding must not
  reshuffle an existing source.
- Treat Dense and Stress as lifecycle-correct load profiles. Put malformed OSC,
  multi-touch, collision, loss, reorder, and duplication only in Fault/Chaos tests.
- Expose model revision, profile, seed, densityScale, playerRatio, requested/effective
  workers, motion budget/Hz, start governor, and cleanup/right-censor telemetry.
- Run exactly one OSC-producing venue path.

Required work packages:
1. selected-capacity dense identity allocation + session-generation fencing;
2. lifecycle-priority queues through ws-server, Redis/hub, SSE/relay, and UDP;
3. disconnect-derived release and reconnect tests;
4. participant-group-aware OSC chunker used by normal, quantised-legacy, and mute paths;
5. explicit zone-to-port splitter/config + telemetry;
6. shared Real10-calibrated Human model, Pad/finger0 lifecycle, no ID wrap;
7. production defaults and control-panel preflight for timing/path/identity/ports;
8. deterministic golden trace, five-seed statistical and 1/10/16/100/256 scale gates;
9. raw downstream capture, 2,000-user, backpressure, disconnect-storm, and mute tests.
10. per-zone Source Capacity manifest/telemetry plus shrink/expand conformance tests.

Definition of done:
- all Section 15 acceptance tests pass;
- zero identity collision, wrong-zone packet, lifecycle drop, UDP error, or EMSGSIZE;
- 2,000 participants release to zero held state;
- Section 11 golden trace and statistical conformance gates pass;
- deployed downstream Venue Bridge capture remains inside the Human envelope;
- server health reports ready=true only while every mandatory contract is satisfied;
- documentation matches the deployed implementation.
```

---

## 19. Change ownership boundary

### Server team changes

- audience/client single-touch enforcement;
- load-generator behaviour and identity allocation;
- WebSocket session fencing;
- Redis/hub/SSE lifecycle priority;
- OSC group construction and chunking;
- disconnect and mute release;
- one-path venue deployment;
- zone splitter and port configuration;
- server telemetry and health/preflight;
- server-side conformance/load tests.

### Cosmic Microwave remains unchanged

- OSC address grammar and numeric conversion;
- physical `0..255` source ceiling, selected per-zone `64/128/256` Source Capacity,
  dense admission policy, and finger0 production policy;
- source-to-Normal-MIDI formula and Notes Only performance output;
- fixed 16-channel topology and 4/8/16 sources per channel by capacity;
- U-to-pitch and V-to-Note-On-velocity mapping;
- plug-in-side `2n/4n/8n/16n/32n` Note Duration and per-Note-On host BPM snapshot;
- absence of CC11/74/20-23, Pressure, Pitch Bend, RPN, MPE, and Crowd Macro output;
- panic's bounded CC123/CC120-only safety sweep;
- one-zone/one-port/one-instance model;
- Expected Zone and exclusive-port safety;
- 3-second live watchdog and its internal immediate Cancel semantics;
- Flow/Grid/Ensemble timing;
- Adaptive/Safety Governors and Global Conductor;
- virtual MIDI endpoint naming.

This boundary keeps server transport correctness separate from musical policy and makes
both systems independently testable.

---

## 20. Machine-readable requirements registry

This is the canonical checklist for server agents and CI. Narrative sections explain
the requirements; this registry gives them stable IDs.

```yaml
contract_schema: cosmic-microwave-server-contract/v1
simulator_contract: cosmic-microwave-human-simulator/v1
target:
  product: Cosmic Microwave
  plugin_version: "2.8.0"
  state_schema: 11

requirements:
  - id: CM-SRV-TIME-001
    level: MUST
    owner: osc-producer
    statement: "Disable server-side note quantisation on every production path."
    verification: { kind: config-and-packet-test, expected: "QUANTISE_NOTES=false" }

  - id: CM-SRV-OSC-001
    level: MUST
    owner: osc-encoder
    statement: "Emit /cs/<A-Z>/<0..255>/finger0/{u,v,on} with canonical spelling."
    verification: { kind: binary-packet-fixture }

  - id: CM-SRV-OSC-002
    level: MUST
    owner: osc-encoder
    statement: "Use float32 U/V, int32 On 0/1, exact one-argument cardinality."
    verification: { kind: typetag-fixture }

  - id: CM-SRV-OSC-003
    level: MUST
    owner: osc-encoder
    statement: "Use immediate OSC bundle timetags only."
    verification: { kind: raw-datagram-decode }

  - id: CM-MIDI-NOTES-001
    level: MUST
    owner: cosmic-microwave
    statement: "Generate only Note On and Note Off for participant performance; U selects pitch and V supplies Note-On velocity."
    verification: { kind: notes-only-midi-capture }

  - id: CM-MIDI-NOTES-002
    level: MUST_NOT
    owner: cosmic-microwave
    statement: "Generate CC11, CC74, CC20-23, Channel Pressure, Pitch Bend, RPN, MPE setup, Crowd Macro CC, or any continuous participant controller."
    verification: { kind: forbidden-midi-message-scan }

  - id: CM-MIDI-MAP-001
    level: MUST
    owner: cosmic-microwave-and-server-admission
    statement: "Keep 16 MIDI channels and source 0->Ch16, source 1->Ch1 modulo mapping at every Source Capacity, yielding 4/8/16 admitted sources per channel."
    verification: { kind: capacity-channel-sweep }

  - id: CM-ABLETON-RECEIVER-001
    level: MUST
    owner: ableton-show-template
    statement: "Route Cosmic Channels 1-8 to OMNI1 Multi Parts 1-8 and Channels 9-16 to OMNI2 Multi Parts 1-8; validate the two instances with a full show-machine CPU/dropout soak."
    verification: { kind: exact-route-sweep-and-full-template-soak }

  - id: CM-MIDI-DURATION-001
    level: MUST
    owner: cosmic-microwave
    statement: "Apply 2n/4n/8n/16n/32n Note Duration from the host BPM snapshot of each new MIDI Note On without any server scheduling or OSC extension."
    verification: { kind: tempo-change-deadline-test }

  - id: CM-MIDI-PANIC-001
    level: MUST
    owner: cosmic-microwave
    statement: "Panic emits only CC123 and CC120 once on each of MIDI Channels 1 through 16."
    verification: { kind: panic-midi-capture, expected: "32 controller messages" }

  - id: CM-SRV-ID-001
    level: MUST
    owner: admission-service
    statement: "Freeze Source Capacity 64, 128, or 256 per zone and allocate its dense zero-based 0..capacity-1 domain without wrap or collision."
    verification: { kind: per-capacity-allocation-test }

  - id: CM-SRV-ID-002
    level: MUST
    owner: websocket-service
    statement: "Maintain exactly one generation-fenced owner per zone and source."
    verification: { kind: replacement-and-late-disconnect-test }

  - id: CM-SRV-ID-003
    level: MUST
    owner: admission-service-and-cosmic-microwave
    statement: "Drop and count every source above the selected per-zone capacity; shrink cleans upper identities safely and expansion never remaps existing IDs."
    verification: { kind: capacity-drop-shrink-expand-test }

  - id: CM-SRV-TOUCH-001
    level: MUST
    owner: client-and-loadgen
    statement: "Production interaction is Pad mode with finger0 only."
    verification: { kind: capture-test, expected: "no finger1..9" }

  - id: CM-SRV-LIFE-001
    level: MUST
    owner: all-transport-hops
    statement: "Never rate-limit, quantise, delay behind motion, or shed On0."
    verification: { kind: forced-backpressure-test, expected: "lifecycleDrops=0" }

  - id: CM-SRV-LIFE-002
    level: MUST
    owner: websocket-service
    statement: "Current-owner disconnect synthesizes an ordered On0."
    verification: { kind: disconnect-test }

  - id: CM-SRV-LIFE-003
    level: MUST
    owner: motion-scheduler
    statement: "Emit both valid held-touch U and V heartbeats at least once per 900 ms; Cosmic's 1200 ms Ready Gate ceiling is receiver tolerance only."
    verification: { kind: stationary-hold-test }

  - id: CM-MIDI-WATCHDOG-001
    level: MUST
    owner: cosmic-microwave
    statement: "After three seconds without a valid live-touch heartbeat, create one internal Cancel that immediately clears the semantic source and every scheduled tail; do not extend OSC with /cancel."
    verification: { kind: watchdog-cancel-and-silence-test }

  - id: CM-SRV-GROUP-001
    level: MUST
    owner: osc-chunker
    statement: "Keep U,V,On1 attack groups complete and ordered in one datagram."
    verification: { kind: 2000-attack-decode }

  - id: CM-SRV-MTU-001
    level: SHOULD
    owner: osc-chunker
    statement: "Keep UDP payload at or below 1200 bytes unless path MTU is proven."
    verification: { kind: max-datagram-metric }

  - id: CM-SRV-MUTE-001
    level: MUST
    owner: mute-and-shutdown
    statement: "Chunk all held-source releases and deliver every On0 without EMSGSIZE."
    verification: { kind: 2000-held-mute-test }

  - id: CM-SRV-ROUTE-001
    level: MUST
    owner: zone-splitter
    statement: "Route every enabled zone through one unique output from the frozen venue manifest; never derive ports inside Cosmic."
    verification: { kind: manifest-and-wrong-zone-test }

  - id: CM-SRV-PATH-001
    level: MUST
    owner: venue-control-plane
    statement: "Run exactly one OSC-producing path for a venue destination."
    verification: { kind: preflight, expected: "one active path" }

  - id: CM-SRV-RATE-001
    level: SHOULD
    owner: motion-scheduler
    statement: "Keep baseline accepted traffic at or below 1200 events/s per instance and prove it with a separate 60-second production-path average/P95 soak; the local Cosmic signal census is not a substitute."
    verification: { kind: 60-second-production-path-soak, averageMaxEventsPerSecond: 1200, p95MaxEventsPerSecondExclusive: 1500 }

  - id: CM-CMW-READY-001
    level: MUST
    owner: venue-operator
    statement: "With internal Cosmic simulator population zero, arm the local capacity-labelled Ready Gate and keep every expected source actively held with U and V separately fresh for the full two-second clean hold."
    verification: { kind: cosmic-local-signal-census, perSourceMotionEventsPerSecondMax: 50, aggregateMotionEventsPerSecondMax: 1200, receiverHeartbeatToleranceMs: 1200, serverSenderHeartbeatMaxGapMs: 900 }

  - id: CM-SRV-SIM-001
    level: MUST
    owner: load-generator
    statement: "Fail instead of wrapping source IDs when the selected per-zone Source Capacity is exhausted."
    verification: { kind: over-capacity-negative-test }

  - id: CM-SRV-SIM-PROFILE-001
    level: MUST
    owner: load-generator
    statement: "Use the shared Real10-calibrated model for Human live workers and offline traces; unknown profiles fall back to Human."
    verification: { kind: shared-module-and-fallback-test }

  - id: CM-SRV-SIM-RNG-001
    level: MUST
    owner: load-generator
    statement: "Derive independent deterministic lifecycle, motion, cadence, network, and trait streams from seed plus stable global source index."
    verification: { kind: worker-and-fleet-stability-test }

  - id: CM-SRV-SIM-SCHED-001
    level: MUST
    owner: load-generator
    statement: "Tick Human behaviour at 10 ms over 50 ms physics, advance from prior deadlines, and skip obsolete output without catch-up bursts."
    verification: { kind: delayed-tick-no-catch-up-test }

  - id: CM-SRV-SIM-RATE-001
    level: MUST
    owner: load-generator
    statement: "Human defaults to 20 Hz desired motion, 600 expressive U/V scalars/s/zone, 900 ms keepalive, 180 starts/s/zone with burst 16, and at most 16 effective workers."
    verification: { kind: human-defaults-and-budget-test }

  - id: CM-SRV-SIM-FAULT-001
    level: MUST
    owner: load-generator-and-chaos-lab
    statement: "Keep Human, Dense, and Stress lifecycle-correct; restrict malformed OSC, multi-touch, collision, loss, duplication, and reorder to explicitly labelled Fault/Chaos tests."
    verification: { kind: profile-fault-isolation-test }

  - id: CM-SRV-SIM-STATS-001
    level: MUST
    owner: release-ci
    statement: "Pass the five-seed Real10 statistical envelope and the 1/10/16/100/256 Human scale gate in Section 11.7."
    verification: { kind: human-statistical-conformance }

  - id: CM-SRV-SIM-TRACE-001
    level: MUST
    owner: release-ci
    statement: "Match the frozen golden trace hash and preserve live-worker/offline-trace model parity for the same seed and configuration."
    verification: { kind: golden-trace-and-live-parity-test }

  - id: CM-SRV-SIM-CLEANUP-001
    level: MUST
    owner: load-generator
    statement: "Emit an On0 at trace cleanup, label it simulator_cleanup, and exclude that right-censored gesture from natural hold and idle statistics."
    verification: { kind: trace-cleanup-censoring-test }

  - id: CM-SRV-SIM-META-001
    level: MUST
    owner: load-generator
    statement: "Record simulator revision, seed, effective configuration, allocation, and lifecycle totals in every trace session."
    verification: { kind: trace-metadata-schema-test }

  - id: CM-SRV-SIM-PATH-001
    level: MUST
    owner: load-balancer-and-load-generator
    statement: "Run production Human rehearsal through the real public load-balanced transport path without a second model or event rewriting in the load balancer."
    verification: { kind: downstream-end-to-end-capture }

  - id: CM-SRV-OBS-001
    level: MUST
    owner: server-health
    statement: "Expose selected Source Capacity, over-capacity rejects/drops, identity, lifecycle, pressure, datagram, route, error, and simulator model/profile/governor telemetry."
    verification: { kind: health-schema-test }

  - id: CM-SRV-TEST-001
    level: MUST
    owner: release-ci
    statement: "Pass every acceptance test in Section 15 before venue release."
    verification: { kind: release-gate }
```

Allowed `level` values are `MUST`, `MUST_NOT`, `SHOULD`, `SHOULD_NOT`, and `MAY`.
Future protocol changes must add or supersede requirement IDs; they must not silently
reinterpret an existing ID.
