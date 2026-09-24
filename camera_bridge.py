#!/usr/bin/env python3
"""
OmegaBot — мост между TrackingCam3 и оператором.

Камера публикует сообщения nanomsg PUB поверх WebSocket:
    ws://<camera-ip>:5557
    subprotocol: pub.sp.nanomsg.org

Формат проверенного сообщения с видеокадром:
    32 байта служебного заголовка
    JPEG 640x480
    остальные служебные данные

JPEG начинается с FF D8 на смещении 32. Для окончания кадра
используется первое FF D9 после начала JPEG.

Оператор получает кадры по TCP:
    [4 байта размера, big-endian][JPEG]
на порту 5001.
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


class CameraFrameParser:
    """Извлекает JPEG из проверенного бинарного сообщения TrackingCam3."""

    JPEG_OFFSET = 32
    JPEG_START = b"\xff\xd8"
    JPEG_END = b"\xff\xd9"

    @classmethod
    def extract_jpeg(cls, payload: bytes) -> Optional[bytes]:
        if len(payload) <= cls.JPEG_OFFSET:
            return None

        if payload[cls.JPEG_OFFSET:cls.JPEG_OFFSET + 2] != cls.JPEG_START:
            return None

        end = payload.find(cls.JPEG_END, cls.JPEG_OFFSET + 2)

        if end < 0:
            return None

        jpeg = payload[cls.JPEG_OFFSET:end + 2]

        if len(jpeg) < 100:
            return None

        return jpeg


class NanomsgCameraClient:
    """Подключение к PUB WebSocket камеры с автоматическим reconnect."""

    PROTOCOL = "pub.sp.nanomsg.org"

    def __init__(
        self,
        camera_ip: str,
        camera_port: int,
        reconnect_delay_s: float = 2.0,
    ):
        self.camera_ip = camera_ip
        self.camera_port = camera_port
        self.reconnect_delay_s = reconnect_delay_s
        self._socket: Optional[websocket.WebSocket] = None

    def frames(self):
        while True:
            try:
                self._connect()
                yield from self._receive_loop()

            except Exception as error:
                logger.warning(
                    "Соединение с камерой потеряно: %s",
                    error,
                )

            finally:
                self._disconnect()

            logger.info(
                "Переподключение через %.1f с...",
                self.reconnect_delay_s,
            )

            time.sleep(self.reconnect_delay_s)

    def _connect(self):
        url = f"ws://{self.camera_ip}:{self.camera_port}"

        logger.info(
            "Подключение к камере: %s",
            url,
        )

        self._socket = websocket.create_connection(
            url,
            subprotocols=[self.PROTOCOL],
            origin=f"http://{self.camera_ip}",
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
                raise ConnectionError(
                    "Камера закрыла соединение."
                )

            if not isinstance(data, (bytes, bytearray)):
                continue

            frame = CameraFrameParser.extract_jpeg(data)

            if frame is None:
                continue

            yield frame


class TcpFrameBroadcaster:
    """Отдаёт JPEG-кадры подключённому оператору по TCP."""

    def __init__(self, port: int):
        self.port = port
        self._server_socket: Optional[socket.socket] = None
        self._client_socket: Optional[socket.socket] = None
        self._lock = threading.Lock()

    def start(self):
        self._server_socket = socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM,
        )

        self._server_socket.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_REUSEADDR,
            1,
        )

        self._server_socket.bind(
            ("0.0.0.0", self.port)
        )

        self._server_socket.listen(1)

        logger.info(
            "Видео-сервер для оператора запущен на порту %d.",
            self.port,
        )

        thread = threading.Thread(
            target=self._accept_loop,
            daemon=True,
        )

        thread.start()

    def _accept_loop(self):
        assert self._server_socket is not None

        while True:
            client_socket, address = (
                self._server_socket.accept()
            )

            client_socket.setsockopt(
                socket.IPPROTO_TCP,
                socket.TCP_NODELAY,
                1,
            )

            client_socket.settimeout(1.0)

            logger.info(
                "ПК подключён (видео): %s",
                address,
            )

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
            header = struct.pack(
                ">I",
                len(jpeg_frame),
            )

            client.sendall(
                header + jpeg_frame
            )

        except (
            socket.timeout,
            ConnectionError,
            OSError,
        ):
            self._drop_client(client)

    def _drop_client(
        self,
        dead_client: socket.socket,
    ):
        with self._lock:
            if self._client_socket is dead_client:
                self._client_socket = None

        try:
            dead_client.close()
        except Exception:
            pass


class FrameRecorder:
    """Опциональная запись полученных JPEG-кадров в AVI."""

    def __init__(
        self,
        output_dir: str,
        segment_seconds: int = 600,
        fps: float = 20.0,
    ):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(
            parents=True,
            exist_ok=True,
        )

        self.segment_seconds = segment_seconds
        self.fps = fps

        self._writer = None
        self._segment_started_at = 0.0
        self._frame_size = None

    def record(self, jpeg_frame: bytes):
        import cv2
        import numpy as np

        image = cv2.imdecode(
            np.frombuffer(
                jpeg_frame,
                dtype=np.uint8,
            ),
            cv2.IMREAD_COLOR,
        )

        if image is None:
            return

        height, width = image.shape[:2]

        self._rotate_segment_if_needed(
            (width, height)
        )

        self._writer.write(image)

    def _rotate_segment_if_needed(
        self,
        frame_size,
    ):
        import cv2

        now = time.time()

        needs_new_segment = (
            self._writer is None
            or frame_size != self._frame_size
            or (
                now - self._segment_started_at
            ) >= self.segment_seconds
        )

        if not needs_new_segment:
            return

        if self._writer is not None:
            self._writer.release()

        timestamp = datetime.now().strftime(
            "%Y%m%d_%H%M%S"
        )

        output_path = (
            self.output_dir
            / f"omegabot_{timestamp}.avi"
        )

        fourcc = cv2.VideoWriter_fourcc(
            *"MJPG"
        )

        self._writer = cv2.VideoWriter(
            str(output_path),
            fourcc,
            self.fps,
            frame_size,
        )

        self._frame_size = frame_size
        self._segment_started_at = now

        logger.info(
            "Новый файл записи: %s",
            output_path,
        )

    def close(self):
        if self._writer is not None:
            self._writer.release()
            self._writer = None


def run(
    camera_ip: str,
    camera_port: int,
    tcp_port: int,
    recorder: Optional[FrameRecorder],
):
    camera = NanomsgCameraClient(
        camera_ip,
        camera_port,
    )

    broadcaster = TcpFrameBroadcaster(
        tcp_port
    )

    broadcaster.start()

    frame_counter = 0
    last_log_time = time.monotonic()

    try:
        for frame in camera.frames():
            broadcaster.broadcast(frame)

            if recorder is not None:
                recorder.record(frame)

            frame_counter += 1

            now = time.monotonic()

            if now - last_log_time >= 5.0:
                logger.info(
                    "Видео работает: кадров=%d, последний JPEG=%d байт.",
                    frame_counter,
                    len(frame),
                )

                last_log_time = now

    finally:
        if recorder is not None:
            recorder.close()


def main():
    parser = argparse.ArgumentParser(
        description="OmegaBot camera bridge"
    )

    parser.add_argument(
        "--camera-ip",
        required=True,
        help="IP-адрес камеры",
    )

    parser.add_argument(
        "--camera-port",
        type=int,
        default=5557,
    )

    parser.add_argument(
        "--tcp-port",
        type=int,
        default=5001,
    )

    parser.add_argument(
        "--record-to",
        default=None,
    )

    parser.add_argument(
        "--record-segment-seconds",
        type=int,
        default=600,
    )

    args = parser.parse_args()

    recorder = None

    if args.record_to:
        recorder = FrameRecorder(
            args.record_to,
            segment_seconds=args.record_segment_seconds,
        )

    run(
        args.camera_ip,
        args.camera_port,
        args.tcp_port,
        recorder,
    )


if __name__ == "__main__":
    main()
