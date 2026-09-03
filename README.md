# Gstreamer RTSP Rockchip server

RTSP-сервер на GStreamer поддерживает два источника:

- камеру V4L2 и ALSA-аудиоустройство;
- MKV-файл из каталога `mkv_files`.

По умолчанию видео кодируется программно через `x264enc` или `x265enc`.
Флаг `--mpp` включает аппаратные `mpph264enc`/`mpph265enc` из
[gstreamer-rockchip plugins](https://github.com/CUITzhaoqi/mirrors/tree/gstreamer-rockchip).
Для них необходим [rockchip-mpp](https://github.com/rockchip-linux/mpp).
В режиме MKV видеодорожка не декодируется и не перекодируется: исходный H.264/H.265
передаётся через соответствующий RTP payloader. Наличие видео и аудио в MKV
определяется автоматически. Аудиодорожка, если она есть, публикуется как PCMA 8 кГц mono.

### Элементы GStreamer для видео

Для захвата видео с камеры необходимы:

- `v4l2src` — захват видео с V4L2-устройства;
- `capsfilter` — установка разрешения и частоты кадров;
- `queue` — буферизация видеодорожки;
- `x264enc` или `x265enc` — программное кодирование по умолчанию;
- `mpph264enc` или `mpph265enc` — аппаратное кодирование при запуске с `--mpp`;
- `h264parse` или `h265parse` — разбор кодированного видеопотока;
- `rtph264pay` или `rtph265pay` — упаковка видео в RTP.

PTS кадров камеры нормализуются на временную сетку, заданную параметром `--fps`.
Нормализация выполняется pad probe перед видеокодировщиком, не добавляет элементов
между V4L2 и MPP, не копирует данные кадров и не нарушает DMABUF allocation. Небольшой
джиттер временных меток устраняется, а реальные пропуски кадров сохраняются без
создания дубликатов.

Для видеодорожки из MKV необходимы:

- `uridecodebin` — обнаружение и извлечение дорожек без декодирования видео;
- `filesrc` и `matroskademux` — чтение файла и разбор контейнера, автоматически
  подключаются внутри `uridecodebin`;
- `queue` — буферизация видеодорожки без сброса кадров;
- `h264parse` и `rtph264pay` — для исходного H.264;
- `h265parse` и `rtph265pay` — для исходного H.265.

В основном MKV mount point элементы декодирования видео, `videoconvert` и MPP-энкодер
не используются. Для уменьшенного mount point видеодорожка декодируется, проходит через
`videoconvert`, `videoscale` и `capsfilter`, после чего повторно кодируется через
`x264enc`/`x265enc` либо через MPP-энкодер при наличии `--mpp`.

### Элементы GStreamer для аудио

Для захвата аудио с устройства и публикации его как PCMA необходимы:

- `alsasrc` — захват аудио с ALSA-устройства;
- `queue` — буферизация аудиодорожки;
- `audioconvert` — преобразование формата сэмплов;
- `audioresample` — преобразование частоты дискретизации;
- `capsfilter` — приведение к S16LE, 8 кГц, mono;
- `alawenc` — кодирование в G.711 A-law (PCMA);
- `rtppcmapay` — упаковка PCMA в RTP.

После приведения аудио камеры к S16LE, 8 кГц, mono его PTS и duration нормализуются
по фактическому количеству сэмплов в буферах. Нормализация не изменяет аудиоданные,
сохраняет начальное смещение относительно видео и учитывает разрывы `DISCONT`.

Для аудиодорожки из MKV вместо `alsasrc` используется `uridecodebin` и декодер,
подходящий для исходного аудиокодека файла. Например, для G.711 A-law требуется
`alawdec`. Элементы `queue`, `audioconvert`, `audioresample`, `capsfilter`,
`alawenc` и `rtppcmapay` используются и в режиме MKV.

## Сборка

```sh
cmake -S . -B build
cmake --build build
```

## Режим камеры

H.264 используется по умолчанию:

```sh
./runapp.sh h264
```

Для H.265:

```sh
./runapp.sh h265
```

Для аппаратного MPP-кодирования добавьте `--mpp`:

```sh
./runapp.sh h264 --mpp
./runapp.sh h265 --mpp
```

Эквивалентный запуск без скрипта:

```sh
sudo ./build/rtsp_server --both --codec h264
sudo ./build/rtsp_server --both --codec h265
```

Можно публиковать только одну дорожку:

```sh
sudo ./build/rtsp_server --video --codec h264
sudo ./build/rtsp_server --audio
```

## Режим MKV

Поместите файл в каталог `mkv_files` в корне проекта и передайте скрипту его имя:

```sh
./runapp.sh mkv 1000_55_60.mkv
./runapp.sh mkv 0391_53_50.mkv
```

Дополнительные параметры сервера передаются после имени файла:

```sh
./runapp.sh mkv 0391_53_50.mkv --port 8555 --mount /record
```

Для передачи RTP только через interleaved TCP добавьте `--tcp-only`:

```sh
./runapp.sh mkv 0391_53_50.mkv --tcp-only
./runapp.sh camera h264 --tcp-only
```

Запуск без скрипта:

```sh
sudo ./build/rtsp_server --mkv 1000_55_60.mkv
```

Для MKV не нужно указывать `--video`, `--audio`, `--both` или `--codec`.
Сервер сам определяет состав дорожек и исходный видеокодек. В режиме passthrough
поддерживаются видеодорожки H.264 и H.265. Воспроизведение завершается при достижении
конца файла.

## Основной и уменьшенный потоки

По умолчанию сервер создаёт только основной mount point `/stream`. Для источника MKV
дополнительный уменьшенный поток включается флагом `--sub-resize`:

- `/stream` — основной поток в исходном разрешении;
- `/stream-low` — дополнительный поток с видео, уменьшенным до `640×360`.

Аудиодорожка публикуется в обоих потоках. Для MKV основной поток остаётся passthrough,
а уменьшенный поток декодируется, масштабируется и повторно кодируется в исходный
H.264 или H.265. По умолчанию используется `x264enc`/`x265enc`; с `--mpp` —
`mpph264enc`/`mpph265enc`.

Для камеры всегда создаётся только основной mount point. Флаг `--sub-resize` в camera-режиме
игнорируется, поскольку два независимых пайплайна не могут одновременно открыть одно
устройство V4L2. Для двух camera-потоков понадобился бы отдельный общий пайплайн с
`tee`, которого в текущей схеме сервера нет.

Вместо ресайза можно назначить второму маунту отдельный MKV-файл:

```sh
./runapp.sh mkv 0391_53_50.mkv --sub-mkv 1000_55_60.mkv
```

`--sub-mkv` автоматически включает второй маунт. Второй файл анализируется независимо:
он может использовать другой видеокодек H.264/H.265 и другой состав дорожек. Видео
передаётся passthrough без декодирования, изменения разрешения и повторного кодирования.
Обработка аудио остаётся такой же, как для основного MKV-потока.

Пути и разрешение второго потока можно изменить:

```sh
./runapp.sh mkv 1000_55_60.mkv \
  --sub-resize \
  --mount /stream \
  --secondary-mount /preview \
  --secondary-width 640 \
  --secondary-height 360
```

Получившиеся адреса:

```text
rtsp://HOST:8554/stream
rtsp://HOST:8554/preview
```

## Отладочный запуск

`runapp_debug.sh` принимает те же параметры, что и `runapp.sh`, и запускает сервер
с `GST_DEBUG=5`:

```sh
./runapp_debug.sh h264
./runapp_debug.sh mkv 1000_55_60.mkv
./runapp_debug.sh mkv 1000_55_60.mkv --tcp-only
```

Для выбора другого уровня отладки передайте стандартную переменную GStreamer
`GST_DEBUG` обычному скрипту запуска. `runapp.sh` сохраняет её при запуске сервера
через `sudo`:

```sh
GST_DEBUG=3 ./runapp.sh h264
GST_DEBUG=4 ./runapp.sh mkv 1000_55_60.mkv --tcp-only
GST_DEBUG='rtsp*:6,matroska*:5,*:3' ./runapp.sh mkv 0391_53_50.mkv
```

При запуске сервера напрямую переменную нужно передать после `sudo`:

```sh
sudo GST_DEBUG=3 ./build/rtsp_server --mkv 1000_55_60.mkv --tcp-only
```

При подключении сервер определяет mount point из RTSP-запроса и сохраняет его до
закрытия соединения:

```text
RTSP client 0x... selected mount=/stream (ip=127.0.0.1, request=/stream)
RTSP client 0x... disconnected (ip=127.0.0.1, mount=/stream)
```

## Параметры сервера

```text
--port PORT                 RTSP-порт, по умолчанию 8554
--mount PATH                RTSP mount path, по умолчанию /stream
--sub-resize                включить дополнительный MKV-поток с ресайзом
--sub-mkv FILE              отдельный MKV passthrough для второго mount point
--secondary-mount PATH      mount path дополнительного потока, по умолчанию /stream-low
--host HOST                 адрес, отображаемый в строке RTSP URL
--video-device DEVICE       устройство V4L2
--audio-device DEVICE       устройство ALSA
--width WIDTH               ширина видео с камеры
--height HEIGHT             высота видео с камеры
--secondary-width WIDTH     ширина resize-потока, по умолчанию 640
--secondary-height HEIGHT   высота resize-потока, по умолчанию 360
--fps FPS                   частота камеры и сетка нормализации PTS
--codec h264|h265           кодек режима камеры
--mpp                       использовать аппаратный MPP-энкодер вместо x264enc/x265enc
--mkv FILE                  MKV-файл из каталога mkv_files
--video                     только видео с камеры
--audio                     только аудио с устройства ALSA
--both                      видео и аудио с устройств
--video-pt PT               RTP payload type для видео
--audio-pt PT               RTP payload type для аудио
--low-latency               режим низкой задержки для устройств
--no-low-latency            отключить режим низкой задержки
--tcp-only                  разрешить только RTP over RTSP/TCP
--quiet-rtspclient-logs     отключить отладочные сообщения rtspclient
```

По умолчанию поток доступен по адресу:

```text
rtsp://0.0.0.0:8554/stream
```

Для MKV с флагом `--sub-resize` дополнительно доступен:

```text
rtsp://0.0.0.0:8554/stream-low
```
