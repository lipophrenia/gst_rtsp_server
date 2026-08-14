# Gstreamer RTSP Rockchip server

RTSP-сервер на GStreamer поддерживает два источника:

- камеру V4L2 и ALSA-аудиоустройство;
- MKV-файл из каталога `mkv_files`.

В режиме камеры видео аппаратно кодируется в H.264 или H.265 через [gstreamer-rockchip plugins](https://github.com/CUITzhaoqi/mirrors/tree/gstreamer-rockchip). Для их работы необходим [rockchip-mpp](https://github.com/rockchip-linux/mpp).
В режиме MKV видеодорожка не декодируется и не перекодируется: исходный H.264/H.265
передаётся через соответствующий RTP payloader. Наличие видео и аудио в MKV
определяется автоматически. Аудиодорожка, если она есть, публикуется как PCMA 8 кГц mono.

### Элементы GStreamer для видео

Для захвата видео с камеры необходимы:

- `v4l2src` — захват видео с V4L2-устройства;
- `capsfilter` — установка разрешения и частоты кадров;
- `queue` — буферизация видеодорожки;
- `mpph264enc` или `mpph265enc` — аппаратное кодирование через MPP;
- `h264parse` или `h265parse` — разбор кодированного видеопотока;
- `rtph264pay` или `rtph265pay` — упаковка видео в RTP.

Для видеодорожки из MKV необходимы:

- `uridecodebin` — обнаружение и извлечение дорожек без декодирования видео;
- `filesrc` и `matroskademux` — чтение файла и разбор контейнера, автоматически
  подключаются внутри `uridecodebin`;
- `queue` — буферизация видеодорожки без сброса кадров;
- `h264parse` и `rtph264pay` — для исходного H.264;
- `h265parse` и `rtph265pay` — для исходного H.265.

Элементы декодирования видео, `videoconvert` и MPP-энкодер в MKV-режиме не используются.

### Элементы GStreamer для аудио

Для захвата аудио с устройства и публикации его как PCMA необходимы:

- `alsasrc` — захват аудио с ALSA-устройства;
- `queue` — буферизация аудиодорожки;
- `audioconvert` — преобразование формата сэмплов;
- `audioresample` — преобразование частоты дискретизации;
- `capsfilter` — приведение к S16LE, 8 кГц, mono;
- `alawenc` — кодирование в G.711 A-law (PCMA);
- `rtppcmapay` — упаковка PCMA в RTP.

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

## Параметры сервера

```text
--port PORT                 RTSP-порт, по умолчанию 8554
--mount PATH                RTSP mount path, по умолчанию /stream
--host HOST                 адрес, отображаемый в строке RTSP URL
--video-device DEVICE       устройство V4L2
--audio-device DEVICE       устройство ALSA
--width WIDTH               ширина видео с камеры
--height HEIGHT             высота видео с камеры
--fps FPS                   частота кадров камеры
--codec h264|h265           кодек режима камеры
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
