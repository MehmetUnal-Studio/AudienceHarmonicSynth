# Cosmic Microwave v2.8.0 Capture/Replay Chaos Lab

`tools/cosmic-chaos-lab.mjs`, Cosmic Microwave OSC trafiğini kaydetmek, aynı zaman
akışıyla yeniden oynatmak ve kontrollü ağ hataları üretmek için bağımsız bir Node.js
CLI'dır. Harici paket kullanmaz; JUCE/C++ çalışma zamanına ve Max/MSP dosyalarına
dokunmaz.

Bu araç plugin'in parçası değildir. Ayrı bir Terminal/Node.js prosesi olarak çalışır;
Cosmic Microwave audio thread'i dosya açmaz, capture yazmaz, replay saati işletmez ve
Chaos Lab adına ek socket oluşturmaz. Show Console yalnız doğru komutu ve bu mimari
sınırı gösterir.

Node.js 20 veya daha yenisi önerilir.

## Proxy + capture

```bash
node tools/cosmic-chaos-lab.mjs proxy \
  --map 7000=127.0.0.1:6060 \
  --map 7001=127.0.0.1:6061 \
  --capture captures/venue-a.ndjson \
  --seed venue-a \
  --drop 0.01 --duplicate 0.005 --reorder 8 --jitter 20
```

Her `--map`, bir UDP listen portunu tek bir hedefe bağlar. Gelen ham datagram önce
kaydedilir, chaos politikası bundan sonra forwarding kopyasına uygulanır. Böylece
capture dosyası deneyin bozulmamış kaynağı olarak kalır.

Var olan capture dosyası varsayılan olarak değiştirilmez. Bilerek üzerine yazmak için
`--overwrite` gerekir.

Her NDJSON satırı şu alanları taşır:

```json
{"v":1,"deltaMicros":1234,"wallTime":"2026-08-23T12:34:56.789Z","port":7000,"address":"/cs/A/7/finger0/u","payload":"L2NzL0Ev...","remoteAddress":"127.0.0.1","remotePort":51234}
```

- `deltaMicros`: capture başlangıcından beri monotonic saat farkı;
- `wallTime`: gözlem anının UTC/ISO duvar saati;
- `port`: datagramın geldiği listen portu; replay mapping anahtarıdır;
- `address`: ilk OSC adresi, bundle için `#bundle`, bozuk paket için `<invalid>`;
- `payload`: datagramın kayıpsız base64 gösterimi.

## Replay

```bash
node tools/cosmic-chaos-lab.mjs replay \
  --input captures/venue-a.ndjson \
  --map 7000=127.0.0.1:6060 \
  --map 7001=127.0.0.1:6061 \
  --speed 1 --loop
```

Replay capture'ı stream eder; tüm dosyayı belleğe almaz. `--speed` aralığı `0.25..16`
ve varsayılanı `1`'dir. Capture içindeki her portun bir `--map` hedefi olmalıdır.
`deltaMicros` değerlerinin geriye gitmesi veya bozuk/base64 olmayan kayıtlar fail-closed
hata üretir.

## Production OSC generator

```bash
node tools/cosmic-chaos-lab.mjs generate \
  --target 127.0.0.1:6060 \
  --zone-count 4 --sources 64 --hz 30 \
  --seed rehearsal-03 --duration-ms 60000
```

`--zones A,C,F` ile açık zone listesi de verilebilir. Generator üretim adresini
kullanır:

```text
/cs/<A-Z>/<0..255>/finger<0..9>/{on,u,v,off}
```

Canlı Cosmic Microwave tek-dokunuş sözleşmesi yalnız `finger0` kabul eder; generator'ın
varsayılan `--finger 0` değeri üretim testidir. `--finger 1..9` yalnız fail-closed/negatif
girdi testleri içindir ve plugin tarafından source state'e alınmaz.

Başlangıçta her zone/source için int32 `on 1`, her frame'de float32 `u` ve `v`, güvenli
kapanışta argümansız `off` gönderilir. `on/off` lifecycle paketleri kuyruk baskısında
motion paketlerinden önceliklidir; tekrarlanan `u/v` değerlerinde yalnız en yeni değer
tutulur. Seed aynı olduğunda üretilen hareket eğrileri aynıdır.

## Chaos politikaları

Tüm kararlar `--seed` ile deterministiktir:

- `--drop P`: bağımsız packet-loss olasılığı (`0..1`);
- `--duplicate P`: duplicate olasılığı (`0..1`);
- `--reorder N`: paketleri en fazla `N` elemanlı pencerelerde shuffle eder;
- `--jitter MS`: `0..MS` arasında ek gecikme verir;
- `--burst P:N`: `P` olasılıkla başlayan `N` paketlik burst-loss üretir.

Örnek: `--burst 0.02:4`, uygun her pakette yüzde iki olasılıkla dört paketlik kayıp
başlatır. `--burst 0` kapalıdır.

## Sınırlar ve kapanış

`--max-queue` (varsayılan `8192`) dosya/ağ kullanıcı-alanı kuyruklarını, `--max-inflight`
(varsayılan `64`) eşzamanlı UDP send sayısını sınırlar. Capture yazarı disk
backpressure'ında bounded kalır ve yalnız capture kopyalarını drop eder; forwarding
devam eder. Ağ kuyruğu dolduğunda motion önce drop/coalesce edilir.

`SIGINT`/`SIGTERM` yeni girişi durdurur, açık socket ve stream'leri kapatır, mümkün olan
pending işleri bounded süre içinde drain eder. Generator gecikmiş motion/On paketlerini
iptal edip aktif bütün zone/source'lara öncelikli `off` yollar.

Testler:

```bash
node --test Tests/ChaosLabTests.mjs
```
