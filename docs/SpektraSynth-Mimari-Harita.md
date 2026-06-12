## Genel Bakis

**SpektraSynth** (version `1.0.35`, JUCE `8.0.4`), bir kalabaligin telefonlarindan gelen dokunma verisiyle calinan, koltuk-tabanli (seat-based) bir JUCE enstrumanidir. Her katilimci koltuk konumuyla tanimlanir (`row` harfi `A..Z` + `col` numarasi `0..99`), ve onlarin `X/Y/On` dokunma verisi ya dogrudan ornek oynatmayi (Direct Sample Player), granuler oynatmayi (Granular Sample Engine) ya da bir **Element Spectral Synth** sesini tetikler ve sekillendirir. Boylece bir kalabalik kolektif bir harmonik dokuya donusur.

Ayirt edici ozellik, **gercek atomik emisyon spektrumlarinin** (NIST tarzi dalga boyu + yogunluk verisi) mikrotonal olceklere ve additif tinilere donusturulmesidir. `X` ekseni secili bir muzikal olcege VEYA bir elementin spektral cizgilerine quantize edilir; `Y` ise genlige (amplitude) surulur.

Repo, CMake uzerinden **4 urun hedefi** uretir:

| Hedef | Plugin Code | Tur | Rol |
|---|---|---|---|
| `AudienceHarmonicSynth` | `Ahss` | VST3 + Standalone | Ana spektral synth (MPE'li, **bayrak urun**) |
| `AudienceHarmonicMidi` | `Ahmd` | VST3 + Standalone | MIDI-effect plugin (non-MPE jenerator) |
| `AudienceMidiGenerator` | `Amgn` | VST3 | Ableton-odakli MIDI jenerator + scale-lock |
| `AudienceMidiDevice` | (GUI app) | Standalone | Basit UDP->MIDI kopru uygulamasi |

> **Onemli ayrim:** Sadece `AudienceHarmonicSynth` hedefi `PluginProcessor.cpp` ve `PartialEngine`'i derler. Diger uc hedef `MidiProcessor.cpp`/`MidiEngine.cpp` etrafinda kuruludur. Bu, alt-sistem haritalarinda tekrar tekrar dogrulanan ve gorev cercevesinin karistirdigi kritik bir mimari sinirdir (asagida "Capraz-Kesen Endiseler" altinda detaylandirilmistir).

---

## Sistem Mimarisi

| Alt Sistem | Sorumluluk | Anahtar Dosyalar | Bagimliliklar (depends-on) |
|---|---|---|---|
| **DSP / Spectral Synthesis Engine** | Koltuk girisini sesli seslere (voice) cevirir; additif osilator banki + ornek oynatma + FX zinciri render eder | `PartialEngine.h/.cpp` (4185 satir) | `AtomicScaleBuilder`, `ElementSpectralData`, `SampleLibrary`, `SeatEventSink`, JUCE |
| **Microtonal Scale System** | Spektrumdan olcek matematigi: cizgi -> cents -> olcek derecesi + tini partial'lari | `AtomicScaleBuilder.h/.cpp`, `MidiScaleModule.h/.cpp`, `MidiPitch.h` | `ElementSpectralData`, JUCE, `<cmath>` |
| **MIDI / MPE Engine** | Iki ayri MIDI yolu: MPE (AudienceProcessor) ve non-MPE jenerator (MidiEngine) | `PluginProcessor.cpp` (MPE), `MidiEngine.h/.cpp`, `MidiProcessor.h/.cpp`, `MidiPitch.h` | `PartialEngine`, `SeatEventSink`, `MidiScaleModule`, JUCE |
| **Plugin Core (Audio Processor / State / Params)** | APVTS (46 param), `pullParams`, `processBlock`, durum kaydetme, MIDI/MPE cikis, harici MIDI port | `PluginProcessor.h/.cpp`, `CMakeLists.txt` | `PartialEngine`, `SeatEventSink`, `OscBridge`, `Simulator`, `MidiPitch` |
| **UI / Editors / Visualization** | Iki bagimsiz editor + Aurora gorsellestirici, LibraryRail, DebugPanel | `PluginEditor.*`, `AuroraComponent.*`, `LibraryRail.*`, `DebugPanel.*`, `MidiGeneratorEditor.*` | `AudienceProcessor`, `PartialEngine`, `SampleLibrary`, APVTS, JUCE |
| **Networking, Samples & Seat Model** | OSC/UDP ingress, Simulator, `SeatEventSink` soyutlamasi, ornek yukleme | `SeatEventSink.h`, `OscBridge.*`, `Simulator.*`, `SampleLibrary.*` | JUCE (osc/audio_formats/events/core), `PartialEngine`, `MidiEngine` |
| **Build / Apps / Test** | CMake, standalone GUI app, Python codegen, ctest harness | `CMakeLists.txt`, `AudienceMidiDeviceApp.cpp`, `Tests/*`, `tools/generate_element_spectral_data.py` | JUCE FetchContent, tum alt sistemler |

### Baglanti Anlatisi

Sistemin kalbi `SeatEventSink` soyut sinifidir (`MAX_ROWS=26`, `MAX_COLS=100`, `MAX_SEATS=2600`). Bu, **fan-in sozlesmesidir**: tum giris kaynaklari (`OscBridge`, `Simulator`, `MidiProcessor`, on-screen klavye, harici MIDI) bu arayuze `setX/setY/setOn(row, col, ...)` cagrilariyla yazar. Tuketici taraf, derleme hedefine gore degisir:

- **Synth tarafinda:** `OscBridge` + `Simulator` -> `DualSeatRouter` (ince adaptor) -> `PartialEngine`. Ana synth'in `MidiEngine`'i **yoktur**; MIDI'si `PartialEngine`'in `MidiSourceEvent` kuyrugundan uretilir ve `AudienceProcessor` tarafindan MPE/Normal MIDI'ye cevrilir.
- **MIDI hedeflerinde:** ayni `OscBridge`/`Simulator` -> `AudienceMidiProcessor`/`AudienceMidiDeviceModel` -> `MidiEngine` (non-MPE not uretici).

Spektral matematik tek bir yerde toplanmistir (`AtomicScaleBuilder`), ve hem ses motoru (pitch + tini) hem de MPE cikisi ayni `getScalePitch` cozucusunu kullanir — AGENTS.md'nin "audio engine ve MIDI/MPE engine ayni pitch resolver'i paylasmali" kuralina uyar.

---

## Veri Akisi

### Giris -> Olcek -> Motor -> Ses

```text
UDP/OSC  /cs/<rowLetter>/<col>/finger<n>/{on|off|line|v}
  -> OscBridge::SharedPort (OSC realtime thread, audio thread DEGIL)
       -> fan-out (lock-free atomic array, <=16 client)
  -> SeatEventSink::setX/setY/setOn   (DualSeatRouter -> PartialEngine)
       -> per-seat atomics (lastX/lastY/active) + VoiceEvent -> eventFifo (8192, AbstractFifo)
  --- AUDIO THREAD SINIRI ---
  -> PartialEngine::render()
       -> processPendingCommands -> drainEvents -> handleEvent
            On  -> maybeTrigger(force)  [hysteresis: minTriggerMs * energyMacro]
            XChange -> maybeTrigger + per-voice filter sweep
            YChange -> targetAmp recompute
            Off -> voices releasing
       -> maybeTrigger:
            getScalePitch (xToPitch) -> PitchTarget {midi, key, frequencyHz, velocityGain}
            allocateVoice  x (1..3 adaptif unison)
       -> renderVoices  (additif osilator banki VEYA Direct/Granular sample player)
       -> applyReverb -> applyDelay -> wet/dry -> master -> applyTapeSaturation -> applyLimiter
  -> stereo audio out + 96 auroraBands atomic + per-voice UI atomics
```

### Spektral Olcek Yolu (cache)

`ElementSpectralData.cpp` (generated, ~20.7k satir) -> `sourceLinesForElement` -> `AtomicScaleBuilder::buildPlayableAtomicScale`. Bu donusum:

1. Tum dalga boylarini nm'ye normalize eder, en uzun dalga boyunu **root** (`0 cents`) olarak secer
2. Her cizgi icin `ratio = lambdaRef/lambda`, `cents = normalizedCents(1200*log2(ratio))` (oktav-katlanmis `0..1200`)
3. Salience-siralamali greedy secim (min separation + max degree cap)
4. Cluster -> temsilci secimi (varsayilan **Medoid**)
5. Cikti: `scaleDegrees` (pitch/UI) + `timbrePartials` (additif banka)

> **Dogrulanmis kritik nokta:** `atomicResultFor` (PartialEngine.cpp:1748) 29 element x 5 ScaleMode = **145 Result** objesini fonksiyon-yerel bir `static const std::array` icinde tutar. Bu cache **`PartialEngine::prepare()` icinde (satir 2056-2058) ses thread'i DISINDA isitilir** — `prepareToPlay` mesaj thread'inde calisir. Iki paralel haritada bu konuda celiski vardi (DSP haritasi "isitiliyor" derken Microtonal haritasi "dogrulanmadi" demisti); kaynak okumasi DSP haritasini dogruladi. Risk **latent**: ayni magic-static ses thread'inden de erisilebilir (`getScalePitch`, `renderVoices`), dolayisiyla dogruluk `prepare()`'in her zaman ilk `render()`'dan once kosmasina baglidir.

### MIDI / MPE / OSC Cikis

`PartialEngine` her not olayini lock-free `midiEventFifo`'ya `MidiSourceEvent` (NoteOn/NoteOff/Expression/AllNotesOff) olarak yazar. `AudienceProcessor::renderOutgoingMidi` bunu block basina <=512 olay tuketir:

- **Normal MIDI:** en yakin olcek notasi, tek kanal
- **MPE:** `allocateMpeChannelForSource` (round-robin + yas-tabanli steal) -> `convertFrequencyToMidiPitch(freq, bendRange)` -> pitchWheel + CC74(timbre) + CC11(expression) + noteOn + channelPressure. `sendMpeSetupIfNeeded` MCM (RPN6) + per-channel bend-range RPN yollar. Expression olaylari sadece `delta>1` ise yeniden yollanir.

---

## Real-time / Thread Modeli

Sistem **uc thread baglami** ile calisir ve gercek-zamanli disiplin AGENTS.md kurallariyla siki sikiya uygulanir.

| Thread | Ne calisir | Tahsis / kilitleme |
|---|---|---|
| **Audio thread** | `processBlock`, `render`, `renderVoices`, FX, `renderOutgoingMidi`, MPE | **Hicbir kilit yok, tahsis yok.** `ScopedNoDenormals`. Tum scratch bufferlari `prepareToPlay`'de boyutlanir |
| **Message thread** | `prepare`, `pullParams`'in tetikledigi UI, durum kaydet, `setSampleDirectory`, Simulator (~30Hz), timer drain (60Hz) | Disk I/O `suspendProcessing` ile cevrelenir |
| **OSC realtime thread** | `SharedPort::oscMessageReceived` (audio DEGIL, message DEGIL) | Alloc-light parse; fan-out lock-free |

**Sinir gecisi mekanizmalari:**

- **Kontrol -> ses:** iki `juce::AbstractFifo` (`eventFifo` 8192, `midiEventFifo`). Sadece *uretici* taraf `eventWriteLock` (`CriticalSection`) alir — ses thread'i okuyucu olarak asla bloklanmaz.
- **Per-field atomics:** `SeatState`/`Voice` alanlari `std::atomic`, cogu `std::memory_order_relaxed`.
- **Harici MIDI:** SPSC `juce::AbstractFifo(8192)`; ses thread'i `juce::MidiOutput`'a asla dokunmaz (sadece 60Hz timer). Tasma `externalMidiDropped` ile sayilir.
- **UI telemetri:** 96-band `std::atomic<float>` aurora dizisi + sequence-stamped ring bufferlar (release/acquire); GUI ses thread'ini asla bloklamaz.

**Dogrulanmis maliyetler / capanlar:**

1. **`atomicResultFor` magic-static:** `prepare()` ile isitilir ama ses-thread yolundan da erisilebilir (latent risk, yukarida).
2. **Per-partial `std::sin` per sample:** `renderVoices` spektral kolu (satir 3677+) her partial icin her ornekte `std::sin` cagirir. Worst-case ~512 partial x 1024 voice — motorun en sicak dongusudur ve en olasi dropout kaynagidir. LFO'lar artimsel rotasyon-matrisi kullanir ama partial'lar kullanmaz.
3. **Full-array sweepleri:** `renderVoices` her block 1024 `Voice` (~2KB elementPhase[512] ile = ~2MB dizi) uzerinden strider; kontrol op'lari 2600 `SeatState` dolasir.
4. **512-olay drain capi:** `midiSourceScratch[512]` kaynak uzayini (2664) sessizce sinirlar.

---

## Alt Sistemler

### DSP / Spectral Synthesis Engine (`PartialEngine`)

- **Anahtar tipler:** `PartialEngine` (SeatEventSink subclass, deger olarak `AudienceProcessor` icinde yasar), `Voice` (POD + UI atomics, `std::array<float,512> elementPhase`), `SeatState` (tum atomic), `PitchTarget` (X->pitch sonucu), `AtomicScaleBuilder::Result` (kopru artifaktı), `ModeProfile/kModes[5]` (imza modlari), `VoiceEvent/MidiSourceEvent`.
- **Sorumluluk:** Tum durum + render pipeline. `std::array<Voice,1024>`, `std::array<SeatState,2600>`, iki FIFO, `SampleLibrary`, `juce::Reverb`, ~40 atomic param.
- **Public arayuz:** `prepare/reset`, `render/processControlEvents`, `setX/setY/setOn`, `setKeyboardStep`, `processKeyboardPitchRealtime`, `drainMidiSourceEvents`, `getScaleMidi/getScaleFrequencyHz/getScaleLineWavelengthNm/getAuroraBand/getActiveVoiceCount` ve ~40 public `std::atomic` parametre alani.
- **Bagimliliklar:** `AtomicScaleBuilder`, `ElementSpectralData`, `SampleLibrary`, `SeatEventSink`, JUCE.

### Microtonal Scale System (`AtomicScaleBuilder` + `MidiScaleModule` + `MidiPitch`)

- **Anahtar tipler:** `AtomicScaleBuilder` (tamamen static/stateless utility), `Options` (rootHz varsayilan `130.8128`=C3), `ScaleDegree` (cents/frequencyHz/velocity), `Result`, `ScaleMode` (Melodic 7 / Performable 12 / Microtonal 24 / Scientific 48 / Raw sinirsiz), `TimbrePartial`, `MidiScaleModule` (12-TET quantizer + `ActiveNoteStack[16*128]`), `convertFrequencyToMidiPitch`.
- **Sorumluluk:** Iki bagimsiz quantization yolu: spektral (`AtomicScaleBuilder` -> PartialEngine) ve 12-TET diatonik (`MidiScaleModule` -> MIDI jenerator tarafı). `MidiPitch.h` mikrotonal frekansi MIDI nota + 14-bit pitch-bend'e koprule.
- **Public arayuz:** `buildPlayableAtomicScale(...)`, `normalizeWavelengthNm`, `circularDistanceCents`, `MidiScaleModule::mapNoteToScale/setConfig/process`, `convertFrequencyToMidiPitch`.
- **Bagimliliklar:** `ElementSpectralData` (tip-seviyesi: `vector<SourceLine>`), JUCE, `<cmath>`.
- **Not (latent tutarsizlik):** `ScaleDegree.frequencyHz` builder'da `Options.rootHz` ile hesaplanir ama `getScalePitch` (satir 2318) bunu YOK SAYAR ve `midiToHz(scaleRootMidi)` ile yeniden hesaplar — `frequencyHz` sentez icin etkin olarak olu, sadece debug dump'larinda kullaniliyor.

### MIDI / MPE Engine

- **Anahtar tipler:** `AudienceProcessor` (gercek MPE motoru, PluginProcessor'da), `MidiOutVoiceState`, `mpeChannelOwner`/`allocateMpeChannelForSource`, `MidiEngine` (AYRI non-MPE jenerator), `MidiChannelMode`, `AudienceMidiProcessor`, `MidiScaleModule`, `PartialEngine::MidiSourceEvent`.
- **Sorumluluk:** **Iki ayri pipeline.** (1) MPE: `AudienceProcessor` `MidiSourceEvent`'leri drain eder, per-channel allocation + per-note bend + pressure + CC74/CC11 + bend-range RPN + MCM uretir. (2) Non-MPE jenerator: `MidiEngine` X'i 12-TET notaya, Y'yi velocity'ye cevirir, CC1/CC11/CC74/CC91/CC93 yollar; pitch bend/RPN/pressure YOKTUR.
- **Public arayuz:** `AudienceProcessor::getActiveMpeVoices/getAvailableMpeChannels`, `MidiEngine::renderMidi/setX/setY/setOn`, `convertFrequencyToMidiPitch`.
- **Dogrulanmis duzeltme:** Gorev cercevesi MPE'yi `MidiEngine`/`MidiProcessor`'a atfetti, ANCAK kaynak MPE mantiginin tamamen `PluginProcessor.cpp` icindeki `AudienceProcessor` (satir ~868-1201) oldugunu gosterdi. `mpeZone` parametresi sadece deklare edilir (satir 255), asla okunmaz — inert (Lower-only).

### Plugin Core (`AudienceProcessor`)

- **Anahtar tipler:** `AudienceProcessor` (juce::AudioProcessor + private Timer), `DualSeatRouter`, `RawParams` (46 cached `std::atomic<float>*`), `MidiOutVoiceState[2664]`, `MidiDebugSlot` (256'lik iki ring), `PackedMidiEvent + externalMidiFifo(8192)`.
- **Sorumluluk:** APVTS sahibi, her block `pullParams` ile motor atomics'ine kopyalar, ses render, MIDI/MPE cikis, durum serialize, debug snapshot.
- **Public arayuz:** `processBlock`, `panic/setMuted/setUdpPort`, `getMidiOutputOptions/setMidiOutputOptionIndex`, `getStateInformation/setStateInformation`, `createEditor`.
- **Bagimliliklar:** `PartialEngine`, `SeatEventSink`, `OscBridge`, `Simulator`, `MidiPitch`, JUCE.

### UI / Editors / Visualization

- **Anahtar tipler:** `AudienceEditor` (~70 kontrol), `AuroraComponent` (30Hz seat-map + spektral strip), `LibraryRail` (Samples/Elements browser), `DebugPanel` (MIDI/MPE diagnostik), `AudienceMidiGeneratorEditor` (ayri MIDI-jenerator plugin'i icin), `SA/CA/BA` APVTS attachment typedef'leri.
- **Sorumluluk:** Mesaj-thread GUI. Kontroller APVTS'e attachment ile baglanir; gorseller Timer ile lock-free getter'lari poll eder. **Hicbir kod ses thread'inde calismaz.**
- **Dikkat:** Iki tam bagimsiz tasarim sistemi (`cs::` palette vs `oklch()`), uc kopyalanmis `midiName()` tanimi (drift riski). Per-paint `std::array<float,8192>` stack bufferlari (~32KB) — RT sorunu degil ama frame basina maliyet.

### Networking, Samples & Seat Model

- **Anahtar tipler:** `SeatEventSink` (cekirdek soyutlama), `OscBridge` + `OscBridge::SharedPort` (process-global UDP port paylasimi, <=16 client fan-out), `Simulator`, `SampleLibrary` + `Sample`, `DualSeatRouter`.
- **Sorumluluk:** Harici/simule kalabaligi motorlara koprule. OSC parse: `row A-Z->0-25`, `col < MAX_COLS`, **`finger<n>` indexi YOK SAYILIR** (multi-touch tek koltuga collapse), `v`->setY, `line`->setX (value/127), `on`/`off`->setOn.
- **Public arayuz:** `SeatEventSink::setX/setY/setOn`, `OscBridge::start/stop`, `Simulator::addRandomSeat(s)`, `SampleLibrary::loadFromDirectory/getSampleForMidi/parseRootMidiFromName`.
- **OSC wire format:** `/cs/<rowLetter A-Z>/<col 0..99>/finger<n>/{on(int)|off|line(0..127->X)|v(0..1->Y)}`.

### Build / Apps / Test

- **Anahtar tipler:** `AUDIENCE_SYNTH_BUILD_TESTS` (CMake option, ON), `FetchContent JUCE 8.0.4`, 4 urun hedefi, 7 test exe, `ELEMENTS` Python listesi (29 element), `Runner/expect()` ad-hoc test harness.
- **Sorumluluk:** Ne derlenecegini tanimlar, JUCE'yi pinler, spektral-veri codegen'i, ve repodaki TEK otomatik regresyon kapsamini saglar.
- **Veri pipeline:** `data/<symbol>.txt` --(`generate_element_spectral_data.py`, 29-element liste)--> `Source/ElementSpectralData.{h,cpp}` (git-tracked, elle yeniden uretilir).

---

## Capraz-Kesen Endiseler

1. **MPE kapsam/isim yanlis hizalamasi (en onemli mimari netlik):** MPE ozellikleri (kanal allocation, per-note bend, CC74/CC11/pressure, bend-range RPN, MCM) `MidiEngine`/`MidiProcessor`'da DEGIL, `PluginProcessor.cpp` icindeki `AudienceProcessor`'dadir. `MidiEngine` sadece nota + CC uretir. Her mimari dokuman MPE'yi `AudienceHarmonicSynth`/`Ahss` binary'sine atfetmelidir.

2. **`atomicResultFor` magic-static deseni:** 145-girisli cache fonksiyon-yerel `static const`. `prepare()` ile isitilir ama ses-thread yolundan da erisilebilir — dogruluk cagri sirasina baglidir. Tum spektral pitch + tini bu cache'e dayanir.

3. **Spektral matematik tek kaynak (iyi):** AGENTS.md kuralina uygun olarak hem ses hem MPE `getScalePitch`/`AtomicScaleBuilder` paylasir. Ancak UI tarafinda UC bagimsiz olcek-isim vokabuleri var: `MidiScaleModule::scaleNames()` (16 tip), `atomicScaleModeName` (5 mod), ve `AudienceMidiDeviceApp.cpp`'deki hardcoded liste — karistirilmasi kolay.

4. **Lock-free sinir deseni (tutarli):** Tum alt sistemler ayni deseni kullanir — `AbstractFifo` + atomic + sequence-stamped ring. Uretici taraf `CriticalSection`, ses-thread okuyucu hicbir zaman bloklanmaz.

5. **Kopyalanmis veri ve yardimcilar:** (a) `PartialEngine.cpp:95-1530` element cizgilerinin ikinci bir fallback kopyasini tutar (generated dataset bos olursa). (b) `rootNames()`/`midiNoteName()`/`midiName()` 3-4 yerde tekrar tanimli. (c) `data/` 91 element .txt tutar ama sadece 29'u derlenir.

6. **APVTS ID kararliligi (kisitlama):** AGENTS.md acikca parametre ID'lerini ve 4-char kodlari korumayi zorunlu kilar. Tum iyilestirmeler ID degisikligi ONERMEZ.

---

## Test Kapsami ve Bosluklar

**Test edilen (7 ctest hedefi):**

| Test | Kapsam |
|---|---|
| `AtomicScaleBuilderTests` | Angstrom->nm, en-uzun-dalga-boyu root, clustering invariantlari, degree caps |
| `MidiScaleModuleTests` | 12-TET scale correction (Nearest/Up/Down), remap, note-off eslesme |
| `MidiPitchTests` | `convertFrequencyToMidiPitch` matematigi (A4->note69/bend8192, clamp) |
| `MidiMappingTests` | `MidiEngine` (non-MPE): X->nota, Y->velocity, retrigger debounce, kanal modlari |
| `OscBridgeTests` | Iki bridge tek UDP port, `/cs/...` fan-out (loopback socket) |
| `SignalFlowTests` | `PartialEngine`+`SampleLibrary` entegrasyonu, 29 element line-count oracle, render sonluluk |
| `PerformanceSmokeTests` | 60/130/512/1024 katilimci, wall<50x audio, peak<=1.0001 |

**Test EDILMEYEN (kritik bosluklar):**

- **`AudienceProcessor` MPE/harici-MIDI cikis yolu:** `convertFrequencyToMidiPitch` izole test edilir ama gercek tuketicisi (MPE note-on/bend/channel allocation/panic, PluginProcessor.cpp ~879-1201) hicbir otomatik kapsama sahip degil. AGENTS.md bir bolumu bunlara ayirmasina ragmen.
- **`PluginProcessor` + `PluginEditor` (bayrak urun):** APVTS state save/restore, parametre wiring, preset recall, output-mode switching — hicbir test hedefinde derlenmiyor.
- **CI yok:** `.github/workflows` veya herhangi bir build config mevcut degil. 7 ctest ve 4 urun sadece gelistiricinin yerel calismasiyla dogrulanir.
- **Generated `ElementSpectralData.cpp` drift'i:** `data/` ile committed .cpp arasinda senkronizasyon dogrulayan build/test adimi yok; `SignalFlowTests` exact line-count oracle'i (orn. Iron==4041) ikili guncelleme gerektirir.
- **Ornek-varlik bagimliligi:** `SignalFlowTests`/`PerformanceSmokeTests` `Samples/Piano Dream`'i diskte zorunlu kilar; eksikse cevresel basarisizlik (kod regresyonu degil).

---

## Riskler ve Teknik Borc (oncelikli)

### Yuksek

1. **MPE / harici-MIDI emisyonu ana plugin'de tamamen test edilmemis.** `AudienceProcessor`'in MPE kanal allocation, RPN/MCM byte dizisi, ve bend-before-note-on davranisi hicbir testle calistirilmamis. AGENTS.md bunu kritik sayar.
2. **`PluginProcessor`/`PluginEditor` (bayrak urun) sifir test.** State, param wiring, preset recall, mode switching dogrulanmamis.
3. **`atomicResultFor` cache fonksiyon-yerel magic-static, ses-thread'inden erisilebilir.** `prepare()` ile isitilir (dogrulandi) ama herhangi bir ses cagrisi `prepare`'i gecerse C++ thread-safe-static init + agir heap allocation/sorting ses thread'inde calisir. Dogruluk tamamen cagri sirasina bagli.

### Orta

4. **Per-partial `std::sin` per sample.** `renderVoices` (3677+) en sicak dongu; worst-case ~512k transcendental cagri/ornek. Ultra polyphony'de en olasi dropout kaynagi.
5. **"Element Partial" knob'u solo-disi modda etkisiz (DOGRULANMIS HATA).** `v.elementPartials` `elementLineCount`'tan set edilir (satir 3385-3386, TUM tini partial sayisi), `spectralPartialCount` sadece solo secimini etkiler (satir 3656-3679). Knob normal additif oynatmada label'ladigi gibi davranmiyor.
6. **CI konfigurasyonu yok.** Test/build zorlamasi tamamen manuel.
7. **Generated veri drift'i + ornek-varlik baglanmasi.** Sessiz divergence riski.

### Dusuk (dogrulanmis somut bulgular)

8. **Olu `scaleTable`/`scaleTableCount`/`buildScaleTable` makinesi (DOGRULANMIS).** `scaleTableCount` sadece store edilir (satir 2399), asla load edilmez; `scaleTable[]` hicbir yerde okunmaz. Header yorumu (satir 398-402) hala aktif scale table oldugunu iddia eder.
9. **`mpeZone` parametresi inert (DOGRULANMIS).** Sadece deklare (satir 255), asla okunmaz. Upper/dual-zone MPE implemente degil.
10. **`spectralPartialCount` header varsayilani olu (DOGRULANMIS).** Header `{9}` (satir 159) vs APVTS `1` (satir 304); `pullParams` her zaman `1` fallback ile yazar (satir 456). `9` runtime'da asla gozlenmez.
11. **`getScaleLineWavelengthNm/Amplitude` negatif idx'i clamp'lemez** (UB potansiyeli, mevcut cagiranlar tetiklemiyor).
12. **OSC `finger<n>` indexi sessizce atilir** (multi-touch tek koltuga collapse). **`SharedPort` 16-client cap'i sessiz drop.** **`SampleLibrary` INT_MAX disinda boyut sinirina sahip degil** (RAM spike riski).
13. **macOS-only ad-hoc codesign + sabit-yol VST3 install** her build'de `~/Library/Audio/Plug-Ins/VST3`'u ezer (opt-out yok).

---

## Iyilestirme Backlog'u (oncelikli)

> Her madde value/effort ve parallel-safe ile isaretli. **Hicbir madde APVTS ID veya 4-char kod degistirmez.** En kritik catisma noktasi: `PartialEngine.cpp` ve `PluginProcessor.cpp`'ye dokunan maddeler ayni batch'te olmamali.

### Yuksek deger

- **B1 — Spektral osilator banki: per-partial `std::sin` yerine wavetable/phase-LUT** (`PartialEngine.cpp/.h`). value:high, effort:medium, parallel-safe: evet (renderVoices'a yerel). En sicak dongunun CPU maliyetini kirar.
- **B2 — "Element Partial" knob'unu solo-disi modda gercekten etkili yap** (`PartialEngine.cpp`). value:high, effort:low, parallel-safe: evet. `spectralPartialCount`'u non-solo kolda `min(lineCount, count)` olarak onurlandir.
- **B3 — `AudienceProcessor` MPE cikis yolu icin test hedefi ekle** (`Tests/MpeOutputTests.cpp` + `CMakeLists.txt`). value:high, effort:high, parallel-safe: hayir (CMakeLists). En yuksek riskli kapsam bosluğunu kapatir.
- **B4 — Minimal CI workflow (configure + build + ctest)** (`.github/workflows/ci.yml`). value:high, effort:medium, parallel-safe: evet (kaynaga dokunmaz).

### Orta deger

- **B5 — 145-girisli spektral cache'i acikca-sahipli member'a tasi (sadece `prepare`'de kur)** (`PartialEngine.cpp/.h`). value:medium, effort:medium, parallel-safe: evet. Latent ses-thread alloc riskini kaldirir.
- **B6 — Aktif-voice index listesi (full MAX_VOICES sweep'inden kacin)** (`PartialEngine.cpp/.h`). value:medium, effort:medium, parallel-safe: evet.
- **B7 — `externalMidiDropped` tasmasini MIDI debug raporunda goster** (`PluginProcessor.cpp`). value:medium, effort:low, parallel-safe: hayir (PluginProcessor).
- **B8 — `mpeMasterChannel` ve member range'i yasal MPE zone'a couple/validate et** (`PluginProcessor.cpp`). value:medium, effort:medium, parallel-safe: hayir (PluginProcessor MPE cekirdek).
- **B9 — Generator-sync check (regenerate + diff)** (`tools/generate_element_spectral_data.py` + CI). value:medium, effort:low, parallel-safe: evet.
- **B10 — `SignalFlowTests`/`PerformanceSmokeTests`'i Piano Dream yokken graceful degrade et** (her iki test dosyasi). value:medium, effort:medium, parallel-safe: evet.
- **B11 — LibraryRail element->scale index mapping'i hardcoded fallback 7 yerine saglam yap** (`LibraryRail.cpp`). value:medium, effort:low, parallel-safe: evet.
- **B12 — MidiGeneratorEditor hit-test dikdortgenlerini `resized()`'da hesapla (paint yerine)** (`MidiGeneratorEditor.cpp/.h`). value:medium, effort:medium, parallel-safe: evet.
- **B13 — `MidiEngine`'in non-MPE oldugunu ve MPE'nin `AudienceProcessor`'da oldugunu yorumlarla netlestir** (`MidiEngine.h`, `PluginProcessor.h`). value:medium, effort:low, parallel-safe: hayir (PluginProcessor.h).
- **B14 — `ScaleDegree.frequencyHz`'i sentez icin yetkili-degil olarak belgele** (`AtomicScaleBuilder.h`). value:medium, effort:low, parallel-safe: evet.
- **B15 — macOS VST3 auto-install/codesign'i opt-in option arkasina al** (`CMakeLists.txt`). value:medium, effort:low, parallel-safe: hayir (CMakeLists).
- **B16 — OSC wire format'ini paylasilan belgeli parser/sabit header'a cikar** (`OscBridge.h/.cpp`). value:medium, effort:low, parallel-safe: evet.
- **B17 — `SampleLibrary::loadFromDirectory`'ye sample-count/byte budjesi ekle** (`SampleLibrary.h/.cpp`). value:medium, effort:medium, parallel-safe: evet.
- **B18 — `SampleLibrary::parseRootMidiFromName`/`getSampleForMidi` icin unit test** (`Tests/` + `CMakeLists.txt`). value:medium, effort:medium, parallel-safe: evet (yeni dosya).

### Dusuk deger

- **B19 — Olu `scaleTable`/`scaleTableCount`/`buildScaleTable` makinesini kaldir** (`PartialEngine.cpp/.h`). value:medium, effort:low, parallel-safe: evet.
- **B20 — Kopyalanmis in-file element cizgi tablolarini generated kaynaga collapse et** (`PartialEngine.cpp`). value:low, effort:medium, parallel-safe: evet.
- **B21 — `getScaleLineWavelengthNm/Amplitude`'i negatif/overflow idx'e karsi koru** (`PartialEngine.cpp`). value:low, effort:low, parallel-safe: evet.
- **B22 — Uc kopyalanmis `midiName()` tanimini paylasilan helper'a cikar** (`PluginEditor.cpp`, `AuroraComponent.cpp`, `MidiGeneratorEditor.cpp`, `DebugPanel.cpp`). value:low, effort:low, parallel-safe: evet.
- **B23 — Per-paint `std::array<float,8192>` bufferlarini reusable member'a tasi** (`AuroraComponent.cpp/.h`, `PluginEditor.cpp/.h`). value:low, effort:low, parallel-safe: evet.
- **B24 — `mpeSetupDirty`'i `std::atomic<bool>` yap** (`PluginProcessor.h/.cpp`). value:low, effort:low, parallel-safe: hayir (PluginProcessor).
- **B25 — OSC `SharedPort` 16-client cap'ine ulasildiginda farkli status goster** (`OscBridge.h/.cpp`). value:low, effort:low, parallel-safe: evet.
- **B26 — OSC parser'da per-message `juce::String` allocation'indan kacin** (`OscBridge.cpp`). value:low, effort:low, parallel-safe: evet.
- **B27 — OSC sinir/edge testleri ekle (col>=MAX_COLS, finger collapse, zero-arg off)** (`Tests/OscBridgeTests.cpp`). value:medium, effort:low, parallel-safe: evet.
- **B28 — `Sources/` (plural) raw-asset klasorunu belgele/yeniden adlandir** (`README.md`). value:low, effort:low, parallel-safe: evet.