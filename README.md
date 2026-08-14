# RTSP server

RTSP-сервер на GStreamer поддерживает два источника:

- камеру V4L2 и ALSA-аудиоустройство;
- MKV-файл из каталога `mkv_files`.

В режиме камеры видео аппаратно кодируется в H.264 или H.265 через GStreamer Rockchip MPP.
В режиме MKV видеодорожка не декодируется и не перекодируется: исходный H.264/H.265
передаётся через соответствующий RTP payloader. Наличие видео и аудио в MKV
определяется автоматически. Аудиодорожка, если она есть, публикуется как PCMA 8 кГц mono.

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

Для выбранного видеокодека должны быть доступны элементы GStreamer:
`mpph264enc`, `h264parse`, `rtph264pay` или
`mpph265enc`, `h265parse`, `rtph265pay`.

## Режим MKV

Поместите файл в каталог `mkv_files` и передайте скрипту его имя:

```sh
./runapp.sh mkv 1000_55_60.mkv
./runapp.sh mkv 0391_53_50.mkv
```

Дополнительные параметры сервера передаются после имени файла:

```sh
./runapp.sh mkv 0391_53_50.mkv --port 8555 --mount /record
```

Запуск без скрипта:

```sh
sudo ./build/rtsp_server --mkv 1000_55_60.mkv
```

Для MKV не нужно указывать `--video`, `--audio`, `--both` или `--codec`.
Сервер сам определяет состав дорожек и исходный видеокодек. В режиме passthrough
поддерживаются видеодорожки H.264 и H.265. Воспроизведение завершается при достижении
конца файла.

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
--quiet-rtspclient-logs     отключить отладочные сообщения rtspclient
```

По умолчанию поток доступен по адресу:

```text
rtsp://192.168.0.5:8554/stream
```
