#!/usr/bin/env python3
"""
OmegaBot — мост между камерой (nanomsg PUB поверх WebSocket) и оператором.
==========================================================================

Камера больше не раздаёт свою Wi-Fi сеть и не отдаёт видео по USB — она
теперь клиент вашей сети и публикует кадры через nanomsg PUB-сокет,
завёрнутый в WebSocket (порт 5557, сабпротокол "pub.sp.nanomsg.org").
Единственный штатный способ посмотреть видео — открыть http://<IP камеры>/
в браузере, что не годится ни для автоматики, ни для записи.

Этот скрипт запускается на Raspberry Pi и делает три вещи:
  1. Подключается к камере по WebSocket (переиспользует уже проверенный
     вами способ подключения — то же соединение, что в вашем тестовом
     скрипте), и сама переподключается при обрыве связи.
  2. Разбирает поток сообщений camera, вычленяет из них JPEG-кадры
     (ищет по магическим байтам начала/конца JPEG, а не по жёстко
     зашитому смещению — так надёжнее, т.к. мы не знаем формат
     служебных заголовков камеры на 100%).
  3. Отдаёт эти JPEG-кадры оператору тем же протоколом, что уже понимает
     video_client.cpp — [4 байта размер, big-endian][JPEG-данные] по TCP,
     порт 5001. Со стороны ПК-клиента менять ничего не нужно.
     Опционально пишет кадры на диск (см. класс FrameRecorder) —
     задел под требование "чёрный ящик" из общих требований проекта.

Почему на Python, а не на C++, как остальной проект: у вас уже есть
рабочее, проверенное на реальной камере подключение через
`websocket-client` (тестовый скрипт из чата). Переизобретать
nanomsg-over-websocket на C++ без библиотеки, которую можно было бы
сразу проверить на этом железе, — лишний риск. Если потребуется
единообразие языка с остальным проектом, этот мост можно будет
переписать на C++ (например, на связке libwebsockets + сырой разбор
кадра), когда протокол камеры будет проверен и стабилен.

Зависимости:
    pip install websocket-client opencv-python

Запуск:
    python3 camera_bridge.py --camera-ip 10.109.150.34

Автозапуск без ручного вмешательства — см. camera-bridge.service рядом.
"""

import argparse
import logging
import socket
import struct
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

import websocket


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("camera_bridge")


def extract_jpeg_frame(payload: bytes) -> Optional[bytes]:
    """
    Достаёт JPEG-изображение из сырого сообщения камеры, если оно там
    есть. Ищем магические байты начала (FF D8) и конца (FF D9) JPEG,
    а не полагаемся на конкретную длину заголовка камеры — так надёжнее
    (заголовок мог быть виден только частично в вашем тестовом логе).
    Возвращает None, если сообщение не похоже на кадр (это, скорее
    всего, одно из двух служебных сообщений — 201 или 32 байта).
    """
    start = payload.find(b"\xff\xd8")
    if start < 0:
        return None

    end = payload.rfind(b"\xff\xd9")
    if end < 0 or end < start:
        return None

    return payload[start:end + 2]


class NanomsgCameraClient:
    """
    Подключение к камере по WebSocket (nanomsg PUB, sub-протокол
    pub.sp.nanomsg.org) с автоматическим переподключением при обрыве —
    соответствует требованию "внезапная потеря связи -> продолжение
    работы" из общих требований проекта.
    """

    PROTOCOL = "pub.sp.nanomsg.org"

    def __init__(self, camera_ip: str, camera_port: int, reconnect_delay_s: float = 2.0):
        self.camera_ip = camera_ip
        self.camera_port = camera_port
        self.reconnect_delay_s = reconnect_delay_s
        self._socket: Optional[websocket.WebSocket] = None

    def frames(self):
        """
        Бесконечный генератор JPEG-кадров. Сам держит соединение живым:
        при обрыве — переподключается и продолжает отдавать кадры дальше,
        не роняя вызывающий код.
        """
        while True:
            try:
                self._connect()
                yield from self._receive_loop()
            except Exception as error:
                logger.warning("Соединение с камерой потеряно: %s", error)
            finally:
                self._disconnect()

            logger.info("Переподключение через %.1f с...", self.reconnect_delay_s)
            time.sleep(self.reconnect_delay_s)

    def _connect(self):
        url = f"ws://{self.camera_ip}:{self.camera_port}"
        logger.info("Подключение к камере: %s", url)

        self._socket = websocket.create_connection(
            url,
            subprotocols=[self.PROTOCOL],
            timeout=5,
        )

        logger.info("Камера подключена.")

    def _disconnect(self):
        if self._socket is not None:
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None

    def _receive_loop(self):
        assert self._socket is not None

        while True:
            data = self._socket.recv()

            if data is None:
                raise ConnectionError("Камера закрыла соединение.")

            if not isinstance(data, (bytes, bytearray)):
                # Текстовое служебное сообщение - не кадр, пропускаем.
                continue

            frame = extract_jpeg_frame(data)

            if frame is not None:
                yield frame


class TcpFrameBroadcaster:
    """
    TCP-сервер на порту 5001, отдающий кадры оператору тем же
    протоколом, что уже понимает video_client.cpp:
    [4 байта размер, big-endian][JPEG-данные].

    Отправка неблокирующая: если ПК-клиент не подключён или не
    успевает вычитывать поток, мы просто пропускаем кадр, а не
    зависаем — та же защита, что уже применена в labirinth_server.cpp
    для телеметрии.
    """

    def __init__(self, port: int):
        self.port = port
        self._server_socket: Optional[socket.socket] = None
        self._client_socket: Optional[socket.socket] = None
        self._lock = threading.Lock()

    def start(self):
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind(("0.0.0.0", self.port))
        self._server_socket.listen(1)

        logger.info("Видео-сервер для оператора запущен на порту %d.", self.port)

        thread = threading.Thread(target=self._accept_loop, daemon=True)
        thread.start()

    def _accept_loop(self):
        assert self._server_socket is not None

        while True:
            client_socket, address = self._server_socket.accept()
            client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            logger.info("ПК подключён (видео): %s", address)

            with self._lock:
                if self._client_socket is not None:
                    try:
                        self._client_socket.close()
                    except Exception:
                        pass
                self._client_socket = client_socket

    def broadcast(self, jpeg_frame: bytes):
        with self._lock:
            client = self._client_socket

        if client is None:
            return

        try:
            client.setblocking(False)
            header = struct.pack(">I", len(jpeg_frame))
            client.sendall(header + jpeg_frame)
        except (BlockingIOError, ConnectionError, OSError):
            # ПК не успевает читать, или отключился - пропускаем кадр,
            # не блокируем мост. Следующий кадр придёт почти сразу.
            self._drop_client(client)

    def _drop_client(self, dead_client: socket.socket):
        with self._lock:
            if self._client_socket is dead_client:
                self._client_socket = None
        try:
            dead_client.close()
        except Exception:
            pass


class FrameRecorder:
    """
    Пишет видео на диск - задел под требование "чёрный ящик" из общих
    требований проекта. Файлы ротируются по времени, чтобы не расти
    бесконечно. Требует opencv-python; если он недоступен или запись
    не нужна прямо сейчас, просто не создавайте этот объект (запись
    полностью опциональна).
    """

    def __init__(self, output_dir: str, segment_seconds: int = 600, fps: float = 20.0):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.segment_seconds = segment_seconds
        self.fps = fps

        self._writer = None
        self._segment_started_at = 0.0
        self._frame_size = None

    def record(self, jpeg_frame: bytes):
        import cv2
        import numpy as np

        image = cv2.imdecode(
            np.frombuffer(jpeg_frame, dtype=np.uint8),
            cv2.IMREAD_COLOR,
        )

        if image is None:
            return

        height, width = image.shape[:2]

        self._rotate_segment_if_needed((width, height))
        self._writer.write(image)

    def _rotate_segment_if_needed(self, frame_size):
        import cv2

        now = time.time()
        needs_new_segment = (
            self._writer is None
            or frame_size != self._frame_size
            or (now - self._segment_started_at) >= self.segment_seconds
        )

        if not needs_new_segment:
            return

        if self._writer is not None:
            self._writer.release()

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_path = self.output_dir / f"omegabot_{timestamp}.avi"

        fourcc = cv2.VideoWriter_fourcc(*"MJPG")
        self._writer = cv2.VideoWriter(str(output_path), fourcc, self.fps, frame_size)
        self._frame_size = frame_size
        self._segment_started_at = now

        logger.info("Новый файл записи: %s", output_path)

    def close(self):
        if self._writer is not None:
            self._writer.release()
            self._writer = None


def run(camera_ip: str, camera_port: int, tcp_port: int, recorder: Optional[FrameRecorder]):
    camera = NanomsgCameraClient(camera_ip, camera_port)
    broadcaster = TcpFrameBroadcaster(tcp_port)
    broadcaster.start()

    try:
        for frame in camera.frames():
            broadcaster.broadcast(frame)

            if recorder is not None:
                recorder.record(frame)
    finally:
        if recorder is not None:
            recorder.close()


def main():
    parser = argparse.ArgumentParser(description="OmegaBot camera bridge")
    parser.add_argument("--camera-ip", required=True, help="IP-адрес камеры в вашей сети")
    parser.add_argument("--camera-port", type=int, default=5557)
    parser.add_argument("--tcp-port", type=int, default=5001, help="Порт для video_client.cpp")
    parser.add_argument("--record-to", default=None, help="Папка для записи видео (опционально)")
    parser.add_argument("--record-segment-seconds", type=int, default=600)

    args = parser.parse_args()

    recorder = None
    if args.record_to:
        recorder = FrameRecorder(args.record_to, segment_seconds=args.record_segment_seconds)

    run(args.camera_ip, args.camera_port, args.tcp_port, recorder)


if __name__ == "__main__":
    main()
