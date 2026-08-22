# Cosmic Microwave 2.1 Mimari Harita

Bu belge, `AudienceHarmonicSynth` hedefinin güncel 2.1 kaynak sınırına göre yeniden
yazılmıştır. Eski SpektraSynth mimarisinin ses üretim yolu artık bayrak ürünün çalışma
zamanına dahil değildir. Hangi dosyanın ürüne dahil olduğunu belirleyen otorite
`CMakeLists.txt` içindeki `target_sources(AudienceHarmonicSynth ...)` listesidir.

## 1. Genel bakış

Cosmic Microwave, izleyici sunucusundan OSC/UDP alan ve Normal MIDI veya MPE üreten
finger-aware bir JUCE ürünüdür.

Temel çalışma modeli:

```text
önceden ayrılmış Zone A -> UDP 6060 -> Cosmic Microwave 1 -> MIDI
önceden ayrılmış Zone B -> UDP 6061 -> Cosmic Microwave 2 -> MIDI
```

Her instance tek UDP portunu dinler ve kendi bağımsız MIDI kanal alanına sahiptir.
Plugin port numarasından zone tahmin etmez; zone harfini OSC adresinden gözlemler.

Bayrak ürün davranış olarak yalnızca MIDI üretir. Ableton yerleşimi ve eski VST3 class
kimliği korunabilsin diye sessiz mono/stereo instrument bus sözleşmesi devam eder.

## 2. Ürün hedefleri

| CMake hedefi | Ürün adı | Format | Rol |
|---|---|---|---|
| `AudienceHarmonicSynth` | Cosmic Microwave | VST3 + Standalone | Güncel finger-aware Normal MIDI/MPE bayrak ürün. |
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
Source/AtomicScaleMap.cpp
Source/AtomicScaleCatalog.cpp
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
| `AudienceProcessor` | APVTS, process lifecycle, MIDI thru, pitch map, finger durumu, Normal/MPE render, host/harici çıkış, state migration. | `PluginProcessor.*` |
| `CosmicStateMigration` | Released 1.x ve schema-2 state'leri schema 3'e güvenli taşıma. | `PluginStateMigration.*` |
| `AudienceEditor` | MIDI-only kontrol ve izleme arayüzü. | `PluginEditor.*` |
| `OscBridge` | Paylaşımlı UDP listener, OSC parse/validation, değer clamp, zone/traffic telemetrisi. | `OscBridge.*`, `OscWireFormat.h` |
| `MidiAudienceModel` | 256 source için atomic UI/control snapshot ve aktif finger maskesi. | `MidiAudienceModel.*` |
| `OscFingerRouter` | Control/OSC thread'lerinden audio thread'e sabit kapasiteli event aktarımı. | `OscFingerRouter.*` |
| `MidiPitchMap` | Yedi tonal 12-TET tablo ve normalize X lookup. | `MidiPitchMap.*` |
| `AtomicScaleCatalog` | 29 element x 5 density için immutable, önceden üretilmiş degree katalogu. | `AtomicScaleCatalog.*`, `AtomicScaleCatalogData.h` |
| `AtomicScaleMap` | En fazla 128 degree/768 pitch-step içeren exact-frequency lookup. | `AtomicScaleMap.*` |
| `MpeMidiOutput` | Normal/MPE note ownership, CC, RPN/MCM, kanal allocation, ref-count ve safety reset. | `MpeMidiOutput.*` |
| `Simulator` | UI thread üzerinde sahte source üretimi ve random movement. | `Simulator.*` |

## 5. Uçtan uca veri akışı

```text
OSC UDP callback                          Simulator / UI thread
       |                                          |
       +-----------> MidiAudienceModel <----------+
                            |
                    atomic source snapshot
                            |
                    OscFingerRouter FIFO
                            |
                    AUDIO PROCESS BLOCK
                            |
                    FingerMidiState[2560]
                            |
                    Pitch System lookup
                    /                 \
             MidiPitchMap       AtomicScaleMap
                            |
                     MpeMidiOutput
                      /           \
             host MidiBuffer    external MIDI FIFO
                                      |
                              60 Hz message timer
                                      |
                          virtual / hardware output

host MIDI input -> değişmeden MIDI thru (Output Off değilse)
audio buffer     -> sessiz uyumluluk çıkışı
```

### Process block sırası

`AudienceProcessor::processBlock` ana hatlarıyla:

1. Host MIDI input'u önceden reserve edilmiş scratch buffer'a kopyalar.
2. APVTS raw pointer değerlerinden Tonal `MidiPitchMap` veya Atomic `AtomicScaleMap`
   konfigürasyonunu günceller.
3. MIDI protokolü/kanal/zone/route değişimlerini karşılaştırır.
4. Gerekirse safety reset üretir ve aktif OSC finger'larını yeniden kurmak üzere
   retrigger işaretler.
5. Audio buffer'ı sessizler, host MIDI output buffer'ını yeniden kullanıma hazırlar.
6. Output açıksa host MIDI input'u değişmeden geçirir.
7. Bir block'ta en fazla 1024 OSC finger event'i drain eder.
8. Finger state değişimlerini `MpeMidiOutput::NoteEvent` dizisine çevirir.
9. Normal MIDI veya MPE mesajlarını host buffer'a yazar.
10. Aynı kısa MIDI mesajlarını seçilmiş harici endpoint için FIFO'ya kopyalar.

## 6. Kimlik modeli

### 6.1 Zone, source ve finger

Canonical adres:

```text
/cs/<zone>/<source>/finger<n>/<param>
```

| Alan | Aralık |
|---|---|
| Zone | `A..Z` |
| Source ID | `0..255` |
| Finger | `0..9` |
| Param | `u`, `v`, `on`, `off`, legacy `line` |

Bayrak üründe realtime voice anahtarı:

```text
voice_id = source_id * 10 + finger
```

Toplam 2560 sabit voice-state slotu vardır. Zone bu anahtarın parçası değildir; çünkü
instance'ın upstream'de tek zone'a ayrılmış olması beklenir.

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

Tüm finger'lar source'un Normal MIDI kanalını paylaşır; ancak note ownership anahtarı
finger bazında ayrıdır. `normalNoteRefCounts[16 * 128]`, aynı channel/note'u tutan son
semantic owner bırakana kadar fiziksel Note Off gönderilmesini erteler.

### 6.3 MPE kimliği

MPE kanalını source ID doğrudan belirlemez. Her aktif `source/finger` çifti bir member
channel alır:

| Zone | Master | Members |
|---|---:|---|
| Lower | 1 | 2..16 |
| Upper | 16 | 1..15 |

15 member doluysa en yaşlı aktif MPE note önce düzgün biçimde bırakılır, sonra kanalı
yeni finger'a atanır. Free channel taraması round-robin'dir ve yeni bırakılan kanalı
diğerlerinden sonra tekrar kullanır.

## 7. OSC ingress

### Parse ve scaling

`OscWireFormat.h` adresi bounded ve allocation-free biçimde parse eder:

- `/cs/`, zone ve param isimleri case-insensitive;
- `finger` literal'i lower-case ve tam eşleşmeli;
- source 256 ve finger10 geçersiz;
- `u/v`: `0..1` aralığına clamp;
- `line`: 127'ye böl, sonra clamp;
- `on`: numeric ve sıfır değilse aktif;
- `off`: argümansız veya finite numeric kabul edilir;
- NaN/Inf ve numeric olmayan argümanlar reddedilir;
- nested OSC bundle'lar recursive işlenir.

### SharedPort

Aynı process içinde aynı UDP portuna bağlanan `OscBridge` nesneleri bir `SharedPort`
paylaşır. Maksimum 16 client vardır. Callback client listesini `CriticalSection` ile
korur; bu thread audio thread değildir. Client taşması UI'da `PORT FULL` olarak görünür.

Üretim tasarımı yine bir zone/port/instance kuralıdır. SharedPort zone ayırma aracı
değildir.

### Overflow davranışı

`OscFingerRouter` 8192 event kapasitelidir. Producer'lar kısa bir `SpinLock` ile
serialize edilir; audio consumer lock almaz. FIFO dolarsa:

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
aktif finger'lar yeni tabloyla yeniden çözülür. Bir block'ta en fazla 256 retrigger
üretilerek büyük durum geçişi bounded tutulur.

## 9. MIDI emission

### Normal MIDI

- Note On velocity doğrudan V/Y'den gelir (`1..127`).
- CC74 doğrudan U/X'ten gelir (`0..127`).
- CC11 doğrudan V/Y'den gelir (`0..127`).
- OSC finger'ları için pitch wheel gönderilmez.
- Tonal map exact 12-TET note üretir; Atomic map exact target'ın en yakın semitone'unu
  gönderir.
- Fixed Channel veya Per source 1-16 seçilebilir.

Normal CC'ler channel-wide olduğu için Channel 1'i paylaşan source 1 ve 17 birbirinin
son CC74/CC11 değerini etkileyebilir. Per-finger expression izolasyonu için MPE gerekir.

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

Host stream her zaman korunur. Harici destination seçilirse aynı kısa mesajlar 8192
kapasiteli FIFO'ya yazılır. 60 Hz message timer `MidiOutput::sendMessageNow` çağrılarını
yapar; audio thread OS MIDI device I/O yapmaz.

Bu nedenle host çıkışı block/sample pozisyonunu korurken harici/virtual çıkış timer
granülaritesine sahiptir. Hassas timestamp gerektiren kullanımda host yolu daha
deterministiktir.

## 10. Thread modeli

| Thread/context | İş | Senkronizasyon |
|---|---|---|
| OSC realtime callback | Parse, telemetry, source atomic update, event enqueue. | Shared client `CriticalSection` + producer `SpinLock`; audio thread değil. |
| Audio processing | MIDI input copy, Tonal/Atomic fixed map seçimi, event drain, Normal/MPE host üretimi, external FIFO write. | Fixed array/FIFO ve pre-reserved `MidiBuffer`; katalog üretimi, parse ve device I/O yok. |
| Message/UI | Editor 8 Hz telemetry, simulator ~30 Hz, destination/state değişimi, 60 Hz external drain. | Atomics, kısa pending-state lock; destructive route işlemlerinde processor suspension. |

Kapasiteler:

| Kaynak | Kapasite |
|---|---:|
| Source | 256 |
| Finger/source | 10 |
| Semantic MIDI state | 2560 |
| OSC finger FIFO | 8192 |
| Audio block event drain | 1024 |
| NoteEvent scratch | 8192 |
| External MIDI FIFO | 8192 |
| MIDI scratch reserve | buffer başına 262144 byte |
| SharedPort client | 16 |
| Atomic degree | element/mode başına en fazla 128 |
| Atomic pitch step | en fazla 768 |

`setMidiOutputOptionIndex` ve `panic`, immediate external reset sırasında processor'ı
geçici olarak suspend eder. `releaseResources` doğrudan device I/O yapmak yerine reset
isteğini message timer'a yayınlar.

## 11. State ve migration

### APVTS parametreleri

| ID | Değerler | Varsayılan |
|---|---|---|
| `midiOutputType` | Off / Normal MIDI / MPE MIDI | Normal MIDI |
| `normalMidiRoutingMode` | Single Channel / Per Source 1-16 | Per Source 1-16 |
| `normalMidiChannel` | 1..16 | 1 |
| `mpeZone` | Lower / Upper | Lower |
| `mpePitchBendRange` | 2 / 12 / 24 / 48 st | 2 st |
| `mpeSendSetupMessages` | bool | On |
| `mpePitchMode` | Retrigger / Glide | Retrigger |
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
- `cosmicMicrowaveSchema` (3)

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
- bozuk/non-finite choice değerleri güvenli sınırlara clamp edilir;
- UDP port ve destination'ın dış dünyaya etkisi message timer üzerinden uygulanır.

## 12. UI mimarisi

`AudienceEditor` yalnızca `AudienceProcessor`, APVTS attachment'ları ve
`MidiAudienceModel` snapshot'larını kullanır.

Görünür modüller:

- Header: SOURCES, FINGERS, NOTES, MPE VOICES;
- OSC INPUT;
- SOURCE ROUTING + observed zones;
- SIMULATOR;
- 256-source / 16-channel SOURCE MATRIX;
- PITCH MAPPING: Tonal/Atomic selector, ortak root/octave/range ve moda göre
  Scale veya Element/Density;
- Normal/MPE mode-specific MIDI ROUTING;
- MIDI OUTPUT, Rescan, Panic.

UI timer'ı 8 Hz'de telemetry çeker. Paint yolu network receiver'a veya MIDI device'a
doğrudan dokunmaz. Source map hücreleri seçim kontrolü değil, read-only görseldir.

## 13. Test kapsamı

CMake on iki CTest hedefi tanımlar:

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
| `AudiencePluginStateMigrationTests` | Released channel/scale formatı, schema-2 Tonal koruma, 1.x Atomic recovery, clamp ve idempotence. |

Önemli kalan entegrasyon boşlukları:

- gerçek host/Ableton route otomasyonu;
- OS virtual endpoint yaşam döngüsü ve timestamp ölçümü;
- uzun süreli yüksek hızlı OSC soak testi;
- resizable editor snapshot/interaction regresyonu.

## 14. Operasyonel riskler ve teknik borç

### Yüksek önem

1. **Zone source collision:** İki zone aynı porta gelirse zone kimliği source anahtarında
   olmadığı için aynı ID'ler ortak state kullanır. Çözüm upstream split'tir.
2. **MPE 15-channel limiti:** 16. aktif finger oldest-note steal tetikler. Bu bir
   allocation sınırıdır, bug değildir.
3. **Event overflow:** 8192 event üstü burst safety reset üretir; upstream U/V rate
   limiti gerekir.

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

- Dropped OSC/external MIDI counter'larını görünür telemetry'ye eklemek.
- Ableton host MIDI vs virtual-port latency/jitter ölçümü.
- Simulator source aralığını konfigüre edilebilir bir test namespace'ine taşımak.
- Plugin validation ve farklı hostlarda MIDI-output lifecycle matrisi.

## 15. 2.1 yükseltme notu

1. Eski VST3 bundle'ını scanned plugin klasörü dışına yedekle.
2. Cosmic Microwave 2.1'i kur ve host'u rescan et.
3. Önce Ableton set'in bir kopyasını aç.
4. Her instance için UDP port, MIDI Format, source routing, destination, Pitch System
   ve Tonal/Atomic map'i doğrula.
5. MIDI alan instrument track'lerini kur; bayrak ürünün kendisi ses üretmez.
6. Audience server bağlanmadan önce Simulator, her channel ve Panic'i test et.

Yeni session'lar Atomic / Helium / Extended açılır. Schema-2 MIDI-only session'lar
Tonal'a, released 1.x spectral seçimler karşılık gelen elementle Atomic'e migrate olur.
Atomic MPE kullanılıyorsa receiver bend range performans öncesi yeniden doğrulanmalıdır.

Kullanıcı akışları için `docs/manual/README.md`, ayrıntılı İngilizce teknik referans
için kökteki `WIKI.md` kullanılmalıdır.
