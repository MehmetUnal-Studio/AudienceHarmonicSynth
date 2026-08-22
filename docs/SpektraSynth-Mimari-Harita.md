# Cosmic Microwave 2.5.0 Mimari Harita

Bu belge, `AudienceHarmonicSynth` hedefinin güncel 2.5.0 kaynak sınırına göre yeniden
yazılmıştır. Eski SpektraSynth mimarisinin ses üretim yolu artık bayrak ürünün çalışma
zamanına dahil değildir. Hangi dosyanın ürüne dahil olduğunu belirleyen otorite
`CMakeLists.txt` içindeki `target_sources(AudienceHarmonicSynth ...)` listesidir.

## 1. Genel bakış

Cosmic Microwave, izleyici sunucusundan OSC/UDP alan ve Normal MIDI veya MPE üreten
tek-dokunuş odaklı bir JUCE ürünüdür.

Temel çalışma modeli:

```text
önceden ayrılmış Zone A -> UDP 6060 -> Cosmic Microwave 1 -> MIDI
önceden ayrılmış Zone B -> UDP 6061 -> Cosmic Microwave 2 -> MIDI
```

Her instance tek UDP portunu dinler ve kendi bağımsız MIDI kanal alanına sahiptir.
Plugin port numarasından zone tahmin etmez; Expected Zone seçimi OSC adresindeki harfi
açıkça doğrular. Yeni session'lar portu exclusive sahiplenir ve Host Only / External
Only / Mirror çıkış politikasından yalnız seçileni uygular.

Bayrak ürün davranış olarak yalnızca MIDI üretir. Ableton yerleşimi ve eski VST3 class
kimliği korunabilsin diye sessiz mono/stereo instrument bus sözleşmesi devam eder.

## 2. Ürün hedefleri

| CMake hedefi | Ürün adı | Format | Rol |
|---|---|---|---|
| `AudienceHarmonicSynth` | Cosmic Microwave | VST3 + Standalone | Güncel tek-dokunuş Normal MIDI/MPE bayrak ürün. |
| `AudienceHarmonicMidi` | Cosmic Microwave MIDI | VST3 + Standalone | Ayrı `MidiEngine` kullanan MIDI-effect. |
| `AudienceMidiGenerator` | Cosmic Microwave MIDI Generator | VST3 | Scale correction/remap içeren Ableton odaklı MIDI-effect. |
| `AudienceMidiDevice` | Cosmic Microwave MIDI Device | Standalone | Hafif UDP->MIDI uygulaması. |

Bayrak ürünün uyumluluk kimliği:

| Alan | Değer |
|---|---|
| Bundle ID | `com.mehmetunal.spektrasynth` |
| Manufacturer code | `Mhmt` |
| Plugin code | `Ahss` |
| CMake target adı | `AudienceHarmonicSynth` |

Yardımcı üç ürün `MidiProcessor`/`MidiEngine` hattını kullanır. Bayrak ürünün
finger/channel garantileri veya UI parametreleri bu hedeflere otomatik olarak
genellenmemelidir.

## 3. Bayrak ürün kaynak sınırı

`AudienceHarmonicSynth` yalnızca şu implementasyon birimlerini derler:

```text
Source/PluginProcessor.cpp
Source/PluginStateMigration.cpp
Source/MpeMidiOutput.cpp
Source/PluginEditor.cpp
Source/AdaptiveCrowdGovernor.cpp
Source/PressureAwareSafetyGovernor.cpp
Source/GlobalConductorHub.cpp
Source/CrowdExpressionMacros.cpp
Source/AtomicScaleMap.cpp
Source/AtomicScaleCatalog.cpp
Source/CrowdTimeField.cpp
Source/MidiAudienceModel.cpp
Source/MidiPitchMap.cpp
Source/OscBridge.cpp
Source/OscFingerRouter.cpp
Source/Simulator.cpp
```

Ana bağımlılıklar JUCE audio processor/device/basics, GUI, OSC, core, data structures,
events ve graphics modülleridir. Bayrak hedefin güncel link listesinde ayrı bir DSP
veya dosya-formatı modülü yoktur.

## 4. Bileşen haritası

| Bileşen | Sorumluluk | Ana dosya |
|---|---|---|
| `AudienceProcessor` | APVTS, process lifecycle, MIDI thru, pitch map, touch durumu, Normal/MPE render, host/harici çıkış, state migration. | `PluginProcessor.*` |
| `CosmicStateMigration` | Released 1.x'den schema 8'e kadar state'leri güvenli taşıma; eski state'lerde routing/Flow/Manual uyumluluğu. | `PluginStateMigration.*` |
| `AudienceEditor` | PERFORM/SHOW CONSOLE MIDI-only kontrol, izleme ve Venue Preflight arayüzü. | `PluginEditor.*` |
| `OscBridge` | Paylaşımlı/exclusive UDP listener, strict OSC parse/validation, Expected Zone filtresi, değer clamp ve ownership/zone/traffic telemetrisi. | `OscBridge.*`, `OscWireFormat.h` |
| `MidiAudienceModel` | 256 source için atomic UI/control snapshot, aktif `finger0` maskesi ve 3 saniyelik live-touch watchdog. | `MidiAudienceModel.*` |
| `OscFingerRouter` | Ayrı lifecycle/motion FIFO'ları, On/Off önceliği ve latest U/V coalescing ile audio thread'e sabit kapasiteli aktarım. | `OscFingerRouter.*` |
| `CrowdTimeField` | Flow/Grid/Ensemble scheduling, host/monotonic clock çözümü, fairness, lane, gate ve telemetry. | `CrowdTimeField.*` |
| `AdaptiveCrowdGovernor` | Son 8 saniyelik benzersiz live source ile held source yoğunluğunu yumuşatıp Grid/Ensemble admission profilini seçme. | `AdaptiveCrowdGovernor.*` |
| `PressureAwareSafetyGovernor` | Ingress/queue/FIFO/deadline baskısından dört-state realtime güvenlik profili üretme. | `PressureAwareSafetyGovernor.*` |
| `GlobalConductorHub` | En fazla 16 instance ve dört izole grupta process-local leader/yoğunluk/kota koordinasyonu. | `GlobalConductorHub.*` |
| `CrowdExpressionMacros` | 256 sabit source üzerinden density, centroid X/Y ve motion CC snapshot'ı üretme. | `CrowdExpressionMacros.*` |
| `MidiPitchMap` | Yedi tonal 12-TET tablo ve normalize X lookup. | `MidiPitchMap.*` |
| `AtomicScaleCatalog` | 29 element x 5 density için immutable, önceden üretilmiş degree katalogu. | `AtomicScaleCatalog.*`, `AtomicScaleCatalogData.h` |
| `AtomicScaleMap` | En fazla 128 degree/768 pitch-step içeren exact-frequency lookup. | `AtomicScaleMap.*` |
| `MpeMidiOutput` | Normal/MPE note ownership, CC, RPN/MCM, kanal allocation, ref-count ve safety reset. | `MpeMidiOutput.*` |
| `Simulator` | UI thread üzerinde sahte source üretimi ve random movement. | `Simulator.*` |

## 5. Uçtan uca veri akışı

```text
OSC UDP callback                          Simulator / UI thread
       |                                          |
       +-> Expected Zone -> MidiAudienceModel <---+
                            |
                    atomic source snapshot
                            |
           lifecycle FIFO + latest U/V FIFO
                            |
                    AUDIO PROCESS BLOCK
                            |
          PressureAwareSafetyGovernor ceiling
                            |
             AdaptiveCrowdGovernor policy
                  (Grid / Ensemble only)
                            |
                    CrowdTimeField
                 Flow / Grid / Ensemble
                            |
                    FingerMidiState[2560]
                            |
                    Pitch System lookup
                    /                 \
             MidiPitchMap       AtomicScaleMap
                            |
                     MpeMidiOutput
                            |
            opsiyonel CrowdExpressionMacros CC
                      /                         \
             Host Only / Mirror       External Only / Mirror
             host MidiBuffer              external MIDI FIFO
                                      |
                           2 ms high-resolution sender
                                      |
                          virtual / hardware output

host MIDI input -> değişmeden MIDI thru (Output Off değilse), sonra seçili path'e gider
audio buffer     -> sessiz uyumluluk çıkışı

GlobalConductorHub -> process-local grup kotası (100 ms)
Chaos Lab          -> ayrı Node.js prosesi; plugin/audio thread dışı
```

### Process block sırası

`AudienceProcessor::processBlock` ana hatlarıyla:

1. Host MIDI input'u önceden reserve edilmiş scratch buffer'a kopyalar ve JUCE
   `PositionInfo` üzerinden host BPM/PPQ/transport durumunu örnekler.
2. APVTS raw pointer değerlerinden Time Field ve Tonal `MidiPitchMap`
   veya Atomic `AtomicScaleMap` konfigürasyonunu günceller.
3. Ingress/queue/FIFO/deadline snapshot'ını Safety Governor'dan geçirir ve hard
   admission/motion/macro sınırlarını alır.
4. Held source ile son 8 saniyelik unique live source sayısının maksimumunu
   `AdaptiveCrowdGovernor` üzerinden geçirir; Adaptive ve timed mode seçiliyse soft
   attack/active/spread politikasını Time Field config'e uygular.
5. Global Conductor snapshot'ı coherent ve fresh ise timed policy'yi instance kotasıyla
   sınırlar; değilse local politika kullanır.
6. MIDI protokolü/kanal/zone/output path/route değişimlerini karşılaştırır.
7. Gerekirse safety reset üretir ve aktif OSC touch'larını yeniden kurmak üzere
   retrigger işaretler.
8. Audio buffer'ı sessizler, host MIDI output buffer'ını yeniden kullanıma hazırlar.
9. Output açıksa ve path host'u içeriyorsa host MIDI input'u değişmeden geçirir.
10. Bir block'ta en fazla `min(64, max(1, block sample sayısı))` OSC lifecycle event'i
   drain eder. Flow hareket event'lerini doğrudan işler; Grid/Ensemble yalnız On/Off'u
   scheduler'a verir ve güncel U/V snapshot'ını grid sınırlarında örnekler.
11. `CrowdTimeField`, direct veya sample-offset'li Attack/Release/SampleMotion istekleri
   üretir.
12. Touch state değişimlerini `MpeMidiOutput::NoteEvent` dizisine çevirir.
13. Normal MIDI veya MPE mesajlarını geçici üretim buffer'ına yazar; opsiyonel macro
    tick'ini lifecycle sonrasına ekler.
14. Host Only/External Only/Mirror seçimine göre host buffer ve harici FIFO'yu ayrıştırır.

## 6. Kimlik modeli

### 6.1 Zone, source ve touch

Canonical adres:

```text
/cs/<zone>/<source>/finger0/<param>
```

| Alan | Aralık |
|---|---|
| Zone | `A..Z` |
| Source ID | `0..255` |
| Live finger | yalnız `0`; `1..9` bridge'de state/telemetry öncesi düşürülür |
| Param | `u`, `v`, `on`, `off`, legacy `line` |

Bayrak üründe realtime voice anahtarı:

```text
voice_id = source_id * 10 + finger
```

Toplam 2560 sabit voice-state slotu geriye uyumluluk ve sabit-kapasiteli core sınırı
olarak korunur; canlı ürün yalnız `source_id * 10 + 0` anahtarlarını besler. Zone bu
anahtarın parçası değildir; çünkü instance'ın upstream'de tek zone'a ayrılmış olması
beklenir. `expectedZone` Any ise bu legacy varsayım geçerlidir; A..Z seçimi yanlış zone
paketini bu modele ulaşmadan reddeder ve mismatch telemetrisi üretir.

`SeatEventSink` içinde eski yardımcı ürünlerle uyumluluk için 26x100 seat sabitleri
bulunmaya devam eder. Fakat bayrak ürünün geçerli OSC source kapasitesi 256'dır ve
`MidiAudienceModel` 26x100 grid kullanmaz.

### 6.2 Normal MIDI channel kuralı

Bayrak ürün base-1 convention kullanır:

```text
channel = positive_mod(source_id - 1, 16) + 1
```

| Source | Channel |
|---:|---:|
| 0 | 16 |
| 1 | 1 |
| 16 | 16 |
| 17 | 1 |
| 32 | 16 |
| 255 | 15 |

Kritik invariant: **source 0, Channel 16'ya gider.**

Canlı `finger0` touch source'un Normal MIDI kanalını kullanır.
`normalNoteRefCounts[16 * 128]`, aynı channel/note'u tutan son
semantic owner bırakana kadar fiziksel Note Off gönderilmesini erteler.

### 6.3 MPE kimliği

MPE kanalını source ID doğrudan belirlemez. Her aktif source touch bir member
channel alır:

| Zone | Master | Members |
|---|---:|---|
| Lower | 1 | 2..16 |
| Upper | 16 | 1..15 |

15 member doluysa en yaşlı aktif MPE note önce düzgün biçimde bırakılır, sonra kanalı
yeni source touch'a atanır. Free channel taraması round-robin'dir ve yeni bırakılan kanalı
diğerlerinden sonra tekrar kullanır.

## 7. OSC ingress

### Parse ve scaling

`OscWireFormat.h` adresi bounded ve allocation-free biçimde parse eder:

- `/cs/`, zone ve param isimleri case-insensitive;
- `finger` literal'i lower-case ve tam eşleşmeli;
- source 256 geçersizdir; parser exact `finger0..finger9` token'larını tanır,
  Cosmic Microwave'ın `OscBridge` politikası yalnız `finger0`ı içeri alır;
- `u/v`: `0..1` aralığına clamp;
- `line`: 127'ye böl, sonra clamp;
- `on`: numeric ve sıfır değilse aktif;
- `off`: argümansız veya finite numeric kabul edilir;
- NaN/Inf ve numeric olmayan argümanlar reddedilir;
- nested OSC bundle'lar recursive işlenir.

### Port ownership

`OscBridge::PortPolicy` Shared veya Exclusive'dir. Yeni schema-8 session Exclusive
başlar: aynı process'teki ikinci owner `OWNERSHIP CONFLICT` alır ve bounded aralıkta
yeniden dener. Schema 7 ve eski state Released davranışı korumak için Shared'a migrate
eder. Shared modda maksimum 16 client vardır. Callback client listesini
`CriticalSection` ile korur; bu thread audio thread değildir. Client taşması UI'da
`PORT FULL` görünür.

Üretim tasarımı yine bir zone/port/instance kuralıdır. SharedPort zone ayırma aracı
değildir. Port/ownership/Expected Zone değişimi aktif state'i bırakan routing boundary'dir.

### Overflow davranışı

`OscFingerRouter`, 8192 lifecycle olayı ve 8192 latest-motion marker'ı için ayrı
sabit kuyruklar kullanır. Producer'lar kısa bir `SpinLock` ile serialize edilir;
audio consumer lock almaz. U/V tekrarları source/finger/axis başına son değere
coalesce edilir ve lifecycle her zaman önce tüketilir. Lifecycle FIFO dolarsa:

1. dropped counter artar;
2. `resetPending` set edilir;
3. audio thread eski queue'yu discard eder;
4. all-off reset üretir.

Bu politika, her hareket paketini korumak yerine stuck-note güvenliğini önceler.

## 8. Pitch systems

Her iki sistem de audio thread üzerinde heap allocation ve parse olmadan çalışır.
`MidiPitchMap` yedi tonal 12-TET tabloyu üretir:

| Scale | Semitone offset'leri |
|---|---|
| Major | `0,2,4,5,7,9,11` |
| Natural Minor | `0,2,3,5,7,8,10` |
| Pentatonic | `0,2,4,7,9` |
| Dorian | `0,2,3,5,7,9,10` |
| Lydian | `0,2,4,6,7,9,11` |
| Harmonic Minor | `0,2,3,5,7,8,11` |
| Whole Tone | `0,2,4,6,8,10` |

Ortak root hesabı:

```text
root_midi = (root_octave + 1) * 12 + pitch_class
```

Yeni session varsayılanı C2 (`36`), Atomic, Helium, Extended ve 4 octave'dır. Tonal
seçildiğinde saklanan varsayılan scale Major'dır.

Atomic katalog Hydrogen'dan Zinc'e uzanan 29 element içerir; uygun veri seti olmadığı
için Nitrogen listede yoktur. Developer-only `tools/GenerateAtomicScaleCatalog.cpp`,
`ElementSpectralData` içindeki wavelength/intensity verisini `AtomicScaleBuilder` ile
önceden işler. Her elementin en uzun geçerli wavelength'i referans alınır, wavelength
ratio'ları bir octave içine cents olarak katlanır ve kompakt sonuç
`AtomicScaleCatalogData.h` içine yazılır. Büyük spektral tablo ve builder bayrak ürünün
runtime target'ında derlenmez.

| Atomic mode | Degree üst sınırı | Minimum katalog aralığı |
|---|---:|---:|
| Core | 7 | 80 cent |
| Extended | 12 | 40 cent |
| Microtonal | 24 | 20 cent |
| Scientific | 48 | 10 cent |
| Raw 128 | 128 | ek ayrım yok |

Bunlar üst sınırdır; elementte daha az kullanılabilir çizgi varsa gerçek degree sayısı
daha düşüktür. `AtomicScaleMap` degree bank'ini 1..6 octave tekrarlar ve en fazla
`128 x 6 = 768` pitch step saklar. Atomic target için exact frequency, en yakın MIDI
note ve bu note'a göre cents offset birlikte tutulur.

X her iki sistemde eşit genişlikli pitch-step bölgelerine bölünür; X=0 ilk, X=1 son
geçerli step'i seçer. Üst MIDI sınırını aşan tablo güvenli biçimde truncate edilir.

Pitch System, root, octave, tonal Scale, Atomic Element/Density veya Range değiştiğinde
aktif source touch'ları yeni tabloyla yeniden çözülür. Bir block'ta en fazla 256 retrigger
üretilerek büyük durum geçişi bounded tutulur.

## 9. Crowd Time Field

`CrowdTimeField`, OSC lifecycle ile pitch/MIDI render arasındaki saf C++ realtime
scheduler'dır. Canlı bridge 256 source için yalnız `finger0`'ı kabul eder; core ise
2560 sabit voice-state rezervini, 16 aktif-ID slotunu ve block başına en fazla 64 çıkış
event'ini korur. `process()` allocation, lock, log, I/O veya message-thread çağrısı
yapmaz.

### Modlar

| Mod | Davranış |
|---|---|
| Flow | On/Off'u block offset'inde doğrudan üretir; U/V mevcut hareket event hattından geçer. Timing kontrolleri bypass edilir. |
| Grid | Attack'i seçili division sınırına kuyruğa alır; fair cursor ile pending voice seçer; attacks/step ve active limit uygular. Held note ordered Off ile, admission alan kısa tap minimum gate ile bırakılır. |
| Ensemble | Source'u spread içindeki deterministik lane'e koyar, fixed gate uygular ve hâlâ held olan voice'u sonraki pulse için yeniden pending yapar. |

Yeni 2.5.0 instance varsayılanları Ensemble, Host, 1/16, %70 gate ve Adaptive Crowd
Governor'dır. Manual değerler attack 4, active 16 ve spread 4 olarak saklanır. MPE
seçildiğinde her efektif aktif limit 15'e clamp edilir; çünkü Lower/Upper zone yalnız
15 member channel sağlar. Schema 6 ve önceki session'lara Manual Governor atanır;
schema 4 öncesi session'lara ayrıca Flow eklenir ve mevcut doğrudan timing grid'e
taşınmaz.

### Adaptive Crowd Governor

Tek `crowdGovernorEnabled` bool parametresi UI'da **Manual / Adaptive** anahtarıdır.
Governor'ın observed density tanımı:

```text
density = max(current_held_sources, unique_live_sources_in_last_8_seconds)
```

Inclusive profil tablosu:

| Source | Attack/step | Spread | Active |
|---:|---:|---:|---:|
| 0-8 | 4 | 1 | 8 |
| 9-24 | 4 | 2 | 10 |
| 25-64 | 3 | 4 | 12 |
| 65-128 | 2 | 8 | 14 |
| 129-256 | 2 | 16 | 16 |

MPE'de Active sonucu 15'e clamp edilir. Density envelope 0.5 saniyede hızlı yükselir,
6 saniyede yavaş düşer; 0.5 saniyelik promotion hold, 4 saniyelik demotion hold ve
%20 aşağı hysteresis band chatter'ını bastırır.

Governor sadece Grid/Ensemble config'inde gelecekteki admission'ı etkiler; Flow bypass
eder. `maxAttacksPerStep`, `maxActive` ve `spreadSlots` değişimi clock-domain reset
değildir. Daha düşük active önerisi çalan voice'u kesmez; spread büyümesi pending
deadline'ları en az yeni lane cycle'a uzatır. Ordered Off, üç saniyelik watchdog ve
Panic hiçbir zaman governor tarafından engellenmez. Gate ve APVTS'deki Manual üçlü
değişmeden kalır; Manual'a dönüldüğünde saklanan/automated değerler geri gelir.

### Pressure-aware Safety Governor

Bu Governor müzikal density Governor'ından bağımsızdır. 10 Hz kontrol snapshot'ında
validated ingress rate, lifecycle queue pressure, motion-drop delta, Time Field pending,
external FIFO pressure/age ve process deadline ratio okunur. Profil:

| State | Motion divisor | Attack cap | Active cap | Min spread | Admission | Macro |
|---|---:|---:|---:|---:|---|---|
| NORMAL | 1 | 16 | 16 | 1 | açık | açık |
| HIGH | 2 | 8 | 12 | 2 | açık | açık |
| CRITICAL | 4 | 2 | 8 | 4 | açık | kapalı |
| EMERGENCY | 8 | 1 | 4 | 8 | kapalı | kapalı |

Yükselme anlıktır. Düşüş %15 hysteresis, state'e göre 2/3/5 saniye hold ve her hold'da
yalnız bir basamak ile gerçekleşir. Invalid numeric/clock EMERGENCY'e fail-closed olur.
Flow NORMAL'da doğrudan kalır, baskıda safety cap'lerine uyar. Ordered Off, watchdog ve
Panic hiçbir profilde kapanmaz.

### Global Conductor

`GlobalConductorHub` aynı plugin process'indeki en fazla 16 instance'ı dört izole gruba
ayırır. Her grup için en düşük UDP portlu Leader deterministik seçilir. Leader'ın global
attack (`1..64`) ve voice (`1..128`) budget'ı 100 ms'de bir aktif zone density'sine göre
adil kota olarak dağıtılır; kıt capacity deterministik rotation ile paylaşılır.

Audio thread tek coherent atomik policy snapshot'ı okur. Registration kaybı, in-flight
publication veya 1500 ms'den eski leader/allocation local policy'ye beklemeden fallback
eder. Sistem network/cross-machine conductor değildir.

### Crowd Expression macro'ları

`CrowdExpressionMacros`, seçili 5/10/20/30 Hz tick'te 256 sabit source slotunu tarar.
Density, valid active centroid X/Y ve smoothed motion değerleri varsayılan CC20/21/22/23
olarak Channel 1..16 veya Broadcast'a gönderilir. İlk/re-enabled tick tam snapshot,
sonrakiler change-only'dir. Empty centroid MIDI 64'tür. Non-finite position centroid ve
motion'dan çıkarılır. CRITICAL/EMERGENCY MIDI macro emission'ı durdurur; telemetry ve
Off işleme sürer.

### Clock domain

Host yalnız finite BPM/PPQ mevcutken ve transport playing durumundayken primary'dir.
Bu durumda block beat origin host PPQ'dur. Sürekli tempo automation reset üretmez;
seek, loop veya clock-domain sınırı safety reset ve canonical held-state rehydrate
tetikler.

Host seçili ama kullanılamıyor ya da durmuşsa scheduler, process-wide monotonic
timestamp'i Internal BPM ile beat'e çevirir. Çalışmaya devam eder fakat host lock false
raporlar. Internal seçimi aynı ortak monotonic zamanı açıkça kullanır ve locked raporlar.
Absolute time kullanıldığı için instance'ların ayrı free-running accumulator'ları yoktur.

### Admission, lane ve kısa tap

Her grid tick'inde yeni attack sayısı hem `maxAttacksPerStep` hem de kalan `maxActive`
kapasitesiyle sınırlanır. Fair cursor yoğunluk altında düşük source ID'lerin sürekli
öncelik almasını engeller. Ensemble lane hesabı kavramsal olarak:

```text
lane = (source_id + hash(udp_port) mod spread_slots) mod spread_slots
```

Port seed, farklı zone instance'larında aynı source ID'lerin aynı host tick'ine
yığılmasını azaltır; zone harfini tahmin veya filtre etmez. Ensemble pending lifetime,
en az bir tam `division * spread_slots` lane cycle'dır ve hiçbir zaman bir quarter-note
beat'ten kısa değildir. Böylece kısa tap expire olmadan önce en az bir tam lane-cycle
fırsatı bulur. Grid pending fırsatı bir beat'tir.

### Movement coalescing ve telemetry

Grid/Ensemble'da `MidiAudienceModel` canonical son U/V değerini güncellemeye devam
eder, fakat her hareket paketi için FIFO event'i üretmez. Attack ve grid-boundary
`SampleMotion`, o andaki en son snapshot'ı örnekler. On/Off FIFO sırası korunur ve
best-effort hareket update'lerinden daha önceliklidir; source/touch ownership ile
Normal MIDI channel kuralı değişmez.

PENDING bekleyen attack, ACTIVE çalan scheduler voice sayısıdır. MERGED, kapasite
bulamayıp coalesce veya expire edilen scheduled work için saturating telemetry
counter'ıdır. Herhangi bir CC, Crowd Energy değeri ya da başka MIDI mesajı üretmez.

## 10. MIDI emission

### Normal MIDI

- Note On velocity doğrudan V/Y'den gelir (`1..127`).
- CC74 doğrudan U/X'ten gelir (`0..127`).
- CC11 doğrudan V/Y'den gelir (`0..127`).
- OSC touch'ları için pitch wheel gönderilmez.
- Tonal map exact 12-TET note üretir; Atomic map exact target'ın en yakın semitone'unu
  gönderir.
- Fixed Channel veya Per source 1-16 seçilebilir.

Normal CC'ler channel-wide olduğu için Channel 1'i paylaşan source 1 ve 17 birbirinin
son CC74/CC11 değerini etkileyebilir. Per-source-touch expression izolasyonu için MPE gerekir.

### MPE

Setup açıkken master'da RPN6 MPE Configuration Message, her member'da RPN0 bend range
gönderilir. Note On mesaj sırası:

```text
Pitch Wheel -> CC74 -> CC11 -> Note On -> Channel Pressure
```

Note Off sırası:

```text
Note Off -> Channel Pressure 0 -> Pitch Wheel 8192
```

Tonal map exact 12-TET MIDI note üretir; dolayısıyla pitch wheel normalde center'dır.
Atomic map nearest base note'u ve element-derived exact target frequency'ye ulaşan
per-note bend'i üretir. Exact target MIDI pitch wheel'in 14-bit çözünürlüğüyle temsil
edilir. Bend range seçenekleri 2/12/24/48 semitone, varsayılan 2'dir ve receiver aynı
aralığa ayarlanmalıdır.

### Host ve harici destination

Destination sırası:

1. Host MIDI Output
2. `Virtual: Cosmic Microwave <UDP port> Out`
3. Sistem/hardware MIDI output'ları

`midiOutputPath` üst otoritedir: Host Only yalnız host buffer'ı, External Only yalnız
seçilen endpoint FIFO'sunu, Mirror açıkça ikisini besler. External Only host buffer'ı
fail-closed temizler. Harici kısa mesajlar 16384 kapasiteli FIFO'ya yazılır. 2 ms
high-resolution sender
`MidiOutput::sendMessageNow` çağrılarını yapar; audio thread OS MIDI device I/O yapmaz.

Bu nedenle host çıkışı block/sample pozisyonunu korurken harici/virtual çıkış timer
granülaritesine sahiptir. Hassas timestamp gerektiren kullanımda host yolu daha
deterministiktir.

## 11. Thread modeli

| Thread/context | İş | Senkronizasyon |
|---|---|---|
| OSC realtime callback | Parse, Expected Zone admission, telemetry, source atomic update, event enqueue. | Shared/exclusive client boundary + producer `SpinLock`; audio thread değil. |
| Audio processing | MIDI input copy, host clock, Safety/Conductor snapshot, bounded lifecycle, Time Field, Normal/MPE, macro ve seçili host/external yayın. | Fixed array/FIFO/scheduler, coherent atomics ve pre-reserved `MidiBuffer`; parse, dosya ve device I/O yok. |
| Message/UI | Editor 8 Hz telemetry/Preflight, simulator/watchdog, route/state/conductor publication; harici MIDI için 2 ms sender. | Atomics, kısa pending-state lock; destructive route işlemlerinde processor suspension. |
| External Node.js | Chaos proxy/capture/replay/generate. | Ayrı process/socket/file sınırı; plugin audio thread'ine girmez. |

Kapasiteler:

| Kaynak | Kapasite |
|---|---:|
| Source | 256 |
| Canlı touch/source | 1 (`finger0`); core rezervi 10 slot/source |
| Semantic MIDI state | 2560 |
| OSC lifecycle FIFO | 8192 |
| OSC latest-motion marker FIFO | 8192 (voice/axis/epoch başına en fazla bir bekleyen marker) |
| Audio block lifecycle drain | `min(64, max(1, block sample sayısı))` |
| CrowdTimeField block çıkışı | 64 |
| NoteEvent scratch | 64 |
| External MIDI FIFO | 16384 |
| MIDI scratch reserve | buffer başına 262144 byte |
| SharedPort client | 16 |
| Global Conductor | 16 instance / 4 izole grup / 100 ms tick / 1500 ms timeout |
| Crowd macro | 256 sabit slot / 5, 10, 20 veya 30 Hz |
| Atomic degree | element/mode başına en fazla 128 |
| Atomic pitch step | en fazla 768 |

`setMidiOutputOptionIndex` ve `panic`, immediate external reset sırasında processor'ı
geçici olarak suspend eder. `releaseResources` doğrudan device I/O yapmak yerine reset
isteğini message timer'a yayınlar.

## 12. State ve migration

### APVTS parametreleri

| ID | Değerler | Varsayılan |
|---|---|---|
| `midiOutputType` | Off / Normal MIDI / MPE MIDI | Normal MIDI |
| `midiOutputPath` | Host Only / External Only / Mirror | Host Only |
| `expectedZone` | Any / A..Z | Any |
| `exclusiveUdpPort` | bool | On |
| `safetyGovernorEnabled` | bool | On |
| `normalMidiRoutingMode` | Single Channel / Per Source 1-16 | Per Source 1-16 |
| `normalMidiChannel` | 1..16 | 1 |
| `mpeZone` | Lower / Upper | Lower |
| `mpePitchBendRange` | 2 / 12 / 24 / 48 st | 2 st |
| `mpeSendSetupMessages` | bool | On |
| `mpePitchMode` | Retrigger / Glide | Retrigger |
| `timeMode` | Flow / Grid / Ensemble | Ensemble |
| `clockSource` | Host / Internal | Host |
| `internalBpm` | 40..240 BPM | 120 |
| `gridDivision` | 1/4 / 1/8 / 1/16 / 1/32 | 1/16 |
| `maxAttacksPerStep` | 1..16 | 4 |
| `maxActiveVoices` | 1..16 | 16; MPE efektif en fazla 15 |
| `gatePercent` | %5..100 | %70 |
| `temporalSpread` | 1 / 2 / 4 / 8 / 16 | 4 |
| `crowdGovernorEnabled` | Manual / Adaptive | Adaptive |
| `conductorRole` | Off / Leader / Follower | Off |
| `conductorGroup` | 1..4 | 1 |
| `conductorAttackBudget` | 1..64 | 16 |
| `conductorVoiceBudget` | 1..128 | 64 |
| `crowdMacrosEnabled` | bool | Off |
| `crowdMacroChannel` | Ch 1..16 / Broadcast | Ch 1 |
| `crowdMacroDensityCc` | 0..127 | 20 |
| `crowdMacroCentroidXCc` | 0..127 | 21 |
| `crowdMacroCentroidYCc` | 0..127 | 22 |
| `crowdMacroMotionCc` | 0..127 | 23 |
| `crowdMacroRate` | 5 / 10 / 20 / 30 Hz | 10 Hz |
| `pitchSystem` | Tonal / Atomic | Atomic |
| `scaleRoot` | C..B | C |
| `scaleRootOctave` | 0..6 | 2 |
| `scaleMode` | 7 tonal map | Major |
| `spectralElement` | 29 element, H..Zn | Helium |
| `atomicScaleMode` | Core / Extended / Microtonal / Scientific / Raw 128 | Extended |
| `scaleOctaves` | 1..6 | 4 |

ValueTree ek alanları:

- `udpPort` (6060)
- `midiOutputOption` (Host)
- `cosmicMicrowaveSchema` (8)

Eski state migration:

- `normalMidiRoutingMode` yoksa legacy davranış için Single Channel eklenir;
- released schema-0 `normalMidiChannel` değerleri `1..16` integer formatından
  `0..15` Choice index formatına çevrilir;
- schema-2 MIDI-only session'lara açıkça Tonal atanır;
- released 1.x ilk yedi tonal seçimi korunur;
- released 1.x 36-choice Scale içindeki element seçimi Atomic'e çevrilir ve karşılık
  gelen element index'i `spectralElement` üzerine taşınır;
- eski element-engine session'ları Atomic'e taşınırken stabil `spectralElement` ve
  `atomicScaleMode` değerleri korunur;
- schema-4 Time Field parametreleri olmayan tüm eski state'lere Flow, Host, 120 BPM,
  1/16, attack 4, active 16, gate %70 ve spread 4 eklenir;
- schema-5 state kabul edilir ve kaldırılmış deneysel alanları atılır;
- schema 6 ve daha eski state'lere Manual Governor atanır; mevcut attack, active,
  spread, gate, clock ve mode değerleri değiştirilmez;
- schema 7 ve eski state'ler released davranışı korumak için Mirror, Expected Zone Any,
  Shared ownership, Safety Off, Conductor Off ve Macro Off alır;
- yeni/partial schema-8 state Host Only, Exclusive, Safety On; Conductor/Macro Off
  varsayımlarını alır ve schema 8 olarak damgalanır;
- bozuk/non-finite choice değerleri güvenli sınırlara clamp edilir;
- UDP port ve destination'ın dış dünyaya etkisi message timer üzerinden uygulanır.

## 13. UI mimarisi

`AudienceEditor` yalnızca `AudienceProcessor`, APVTS attachment'ları ve
`MidiAudienceModel` snapshot'larını kullanır.

Arayüz 1280x760 varsayılan, 1000x650 minimum boyutta iki sayfadır: **PERFORM** ve
**SHOW CONSOLE**. Görünür modüller:

- Header: build'den türetilen kalıcı versiyon etiketi (`v2.5.0`), SOURCES, TOUCHES,
  NOTES, MPE VOICES;
- OSC INPUT;
- SOURCE ROUTING + observed zones;
- SIMULATOR;
- 256-source / 16-channel SOURCE MATRIX;
- TIME FIELD: Flow/Grid/Ensemble, Host/Internal, BPM/division, tek Manual/Adaptive
  anahtarı, manual veya efektif attack/active/spread, gate ve
  PENDING/ACTIVE/MERGED telemetry;
- PITCH MAPPING: Tonal/Atomic selector, ortak root/octave/range ve moda göre
  Scale veya Element/Density;
- Normal/MPE mode-specific MIDI ROUTING;
- MIDI OUTPUT, Rescan, Panic;
- Routing Safety: Host/External/Mirror, Expected Zone, exclusive ownership;
- Safety Governor state/reason ile ingress/deadline/FIFO/queue telemetrisi;
- OSC, zone, ownership, MIDI route, Safety, Time Field ve Conductor için yedi satırlı
  Venue Preflight;
- Global Conductor role/group/budget/live quota;
- Crowd Expression enable/channel/rate/CC mapping ve suspension status;
- harici Chaos Lab CLI komutu ve audio-thread isolation açıklaması.

UI timer'ı 8 Hz'de bounded telemetry snapshot'ı çeker. Paint yolu network receiver'a,
MIDI device'a veya Chaos Lab dosya/socket'ine doğrudan dokunmaz. Source map hücreleri
seçim kontrolü değil, read-only görseldir.

## 14. Test kapsamı

CMake core ve entegrasyon CTest hedeflerini tanımlar; Node.js bulunursa Chaos Lab testi
de otomatik eklenir:

| Test | Kapsam |
|---|---|
| `AudienceMidiMappingTests` | Yardımcı `MidiEngine` mapping/channel/CC. |
| `AudienceMidiScaleModuleTests` | Yardımcı scale correction/remap/note-off pairing. |
| `AudienceMidiPitchTests` | Frequency -> MIDI note/bend hesabı. |
| `AudienceMpeOutputTests` | MPE setup/order/zones/allocation/steal/reset, 2560 ID, Normal source mapping/ref-count. |
| `AudienceOscBridgeTests` | Parser boundary/scaling/bundle/fan-out/telemetry/client cap. |
| `AudienceOscFingerRouterTests` | FIFO order/reset. |
| `AudienceMidiAudienceModelTests` | Finger masks/counts/boundary/source mapping (`0 -> 16`). |
| `AudienceMidiPitchMapTests` | Yedi pitch tablo, X sınırları, root/range clamp. |
| `AudienceAtomicScaleBuilderTests` | Offline wavelength normalize/seçim, density cap ve hostile numeric input. |
| `AudienceAtomicScaleMapTests` | Fixed exact-frequency map, 768-step sınırı ve hostile numeric input. |
| `AudienceAtomicScaleCatalogTests` | 29x5 generated katalog bütünlüğü, metadata ve mode cap'leri. |
| `AudienceAtomicMidiIntegrationTests` | Atomic exact-frequency map'in Normal/MPE renderer ile entegrasyonu. |
| `AudienceAdaptiveCrowdGovernorTests` | Beş exact profil, smoothing, hysteresis, MPE cap, hostile input, reset ve allocation-free update. |
| `AudienceCrowdTimeFieldTests` | Flow/Grid/Ensemble, host/internal/fallback clock, fairness, lane seed, short tap, gate, overflow, reset/rehydrate. |
| `AudienceCrowdMidiIntegrationTests` | Host PPQ sample offset'i ve timed path içinde değişmeyen source-channel ownership. |
| `AudiencePressureAwareSafetyGovernorTests` | Dört profil, her pressure signal, hostile input, recovery hold/hysteresis ve allocation-free update. |
| `AudienceGlobalConductorHubTests` | Registration/lifetime, group isolation, election, fair quota, stale fallback ve coherent read. |
| `AudienceCrowdExpressionMacrosTests` | Density/centroid/motion, rate/change-only, hostile position/clock, reset ve allocation-free scan. |
| `AudienceChaosLabTests` | Harici CLI'nin deterministik chaos, mapping ve bounded capture/replay yardımcıları. |
| `AudiencePluginStateMigrationTests` | Released channel/scale, schema-2 Tonal, schema-4 Flow, schema-5 cleanup, schema-6 Manual, schema-7 routing compatibility, schema-8 stamp, clamp/idempotence. |

Önemli kalan entegrasyon boşlukları:

- gerçek host/Ableton route otomasyonu;
- OS virtual endpoint yaşam döngüsü ve timestamp ölçümü;
- uzun süreli yüksek hızlı OSC soak testi;
- resizable editor snapshot/interaction regresyonu.

## 15. Operasyonel riskler ve teknik borç

### Yüksek önem

1. **Zone source collision:** Expected Zone Any kullanılan legacy/diagnostic durumda iki
   zone aynı porta gelirse aynı ID'ler ortak state kullanabilir. Çözüm upstream split +
   explicit Expected Zone + exclusive ownership'tür.
2. **MPE 15-channel limiti:** Timed modlarda active limit efektif 15'e clamp edilir.
   Flow'da 16. aktif source touch oldest-note steal tetikleyebilir; bu allocation sınırıdır.
3. **Event overflow:** Bütün modlar redundant U/V burst'ünü latest-value olarak
   coalesce eder; buna rağmen aşırı On/Off lifecycle burst'ü ayrı 8192-event priority
   FIFO'da safety reset üretebilir.

### Orta önem

4. **Harici MIDI timer granülaritesi:** Virtual/hardware route sample-accurate değildir;
   host MIDI yolu tercih edilebilir.
5. **Simulator/live namespace:** Simulator 0..255 pool'unu live OSC ile paylaşır;
   soundcheck sonrası Clear/Panic yapılmalı.
6. **Dynamic endpoint desteği:** `createNewDevice` OS tarafından reddedilebilir; UI host
   output'a fallback durumunu gösterir.
7. **Silent-shell host bağımlılığı:** Track devre dışı bırakılır veya host processing'i
   durdurursa `processBlock` çağrılmayabilir ve MIDI üretimi durur.

### Önerilen backlog

- Chaos Lab ile uzun süreli multi-zone soak ve Venue Preflight snapshot regresyonu.
- Ableton host MIDI vs virtual-port latency/jitter ölçümü.
- Simulator source aralığını konfigüre edilebilir bir test namespace'ine taşımak.
- Plugin validation ve farklı hostlarda MIDI-output lifecycle matrisi.

## 16. 2.5.0 yükseltme notu

1. Eski VST3 bundle'ını scanned plugin klasörü dışına yedekle.
2. Cosmic Microwave 2.5.0'ı kur ve host'u rescan et.
3. Önce Ableton set'in bir kopyasını aç.
4. Her instance için UDP port/Expected Zone/ownership, MIDI Output Path/endpoint,
   Time Field/clock, MIDI Format/source routing, Conductor ve Pitch map'i doğrula.
5. MIDI alan instrument track'lerini kur; bayrak ürünün kendisi ses üretmez.
6. Show Console Venue Preflight fail'lerini çöz, Safety NORMAL ve global kotaları/macro
   mapping'i doğrula; server bağlanmadan Simulator, her channel ve Panic'i test et.

Yeni session'lar Ensemble / Host / 1/16, Adaptive Governor, gate %70 ve Atomic / Helium /
Extended açılır. Manual politika attack 4, active 16 (MPE'de 15) ve spread 4 olarak
saklanır. Schema 6 ve eski session'lar mevcut davranışı korumak için Manual alır;
schema 3 ve daha eski session'lar ayrıca Time Field için Flow alır. Schema-5 içindeki
kaldırılmış deneysel alanlar yükseltmede atılır. Schema 7 ve eski state Mirror/Shared/
Safety-Off davranışını korur; yeni session Host Only/Exclusive/Safety-On ve schema 8'dir.
Global Conductor ile Crowd Expression opt-in'dir.
Önceki schema-2 Tonal ve released 1.x Atomic migration kuralları korunur. Atomic MPE
kullanılıyorsa receiver bend range performans öncesi yeniden doğrulanmalıdır.

Kullanıcı akışları için `docs/manual/README.md`, ayrıntılı İngilizce teknik referans
için kökteki `WIKI.md` kullanılmalıdır.
