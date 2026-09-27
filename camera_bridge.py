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

------------------------------------------------------------
Диагностика низкого FPS (~10 кадров/с) и запись без блокировки.
------------------------------------------------------------
Раньше запись на диск (--record-to) выполнялась СИНХРОННО в том же
цикле, что раздаёт кадры оператору: на каждый кадр — JPEG-декод
(cv2.imdecode) и запись через cv2.VideoWriter с кодеком MJPG (тоже
JPEG!), то есть двойной декод/энкод впустую, и всё это на пути,
от которого также зависит, как быстро следующий кадр дойдёт до
оператора. На слабом Raspberry Pi 3 это создавало узкое место:
скорость всего пайплайна упиралась в скорость записи на диск, а не
в реальную скорость камеры.

Теперь запись вынесена в отдельный поток с ограниченной очередью:
если писатель на диск не успевает, лишние кадры отбрасываются (не
пишутся в файл), но живое видео оператору при этом НЕ тормозится -
раздача кадров больше не ждёт запись.

Также добавлен отдельный счётчик FPS на входе (сколько кадров реально
приходит от камеры по WebSocket) - раньше логировался только общий
счётчик, из которого нельзя было понять, тормозит сама камера или
что-то на Pi. Если "входящий" FPS низкий сам по себе (без записи и
без операторского подключения) - дело в самой камере/сети, а не в
Pi; если высокий на входе, но что-то ниже по потоку не успевает -
дело в конкретном узком месте на Pi.
"""

import argparse
import logging
import queue
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

    def __init__(self, camera_ip: str, camera_port: int, reconnect_delay_s: float = 2.0):
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
            client_socket.settimeout(1.0)

            logger.info("ПК подключён (видео): %s", address)

            with self._lock:
                if self._client_socket is not None:
                    try:
                        self._client_socket.close()
                    except Exception:
                        pass

                self._client_socket = client_socket

    def broadcast(self, jpeg_frame: bytes) -> bool:
        """Возвращает True, если кадр реально был отправлен оператору."""
        with self._lock:
            client = self._client_socket

        if client is None:
            return False

        try:
            header = struct.pack(">I", len(jpeg_frame))
            client.sendall(header + jpeg_frame)
            return True
        except (socket.timeout, ConnectionError, OSError):
            self._drop_client(client)
            return False

    def _drop_client(self, dead_client: socket.socket):
        with self._lock:
            if self._client_socket is dead_client:
                self._client_socket = None

        try:
            dead_client.close()
        except Exception:
            pass


class ThreadedFrameRecorder:
    """
    Запись JPEG-кадров в AVI в ОТДЕЛЬНОМ потоке, через ограниченную
    очередь. Если писатель на диск не успевает за входящим потоком
    кадров, лишние кадры молча отбрасываются (в записи будет меньше
    кадров, чем реально было) - это осознанный компромисс: для
    оператора важнее низкая задержка живого видео, чем идеально
    полная запись архива.

    put_frame() никогда не блокирует вызывающий поток дольше, чем
    нужно на попытку положить кадр в очередь - это и есть развязка
    записи от раздачи видео оператору.
    """

    QUEUE_CAPACITY = 4

    def __init__(self, output_dir: str, segment_seconds: int = 600, fps: float = 20.0):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.segment_seconds = segment_seconds
        self.fps = fps

        self._queue: "queue.Queue[Optional[bytes]]" = queue.Queue(maxsize=self.QUEUE_CAPACITY)
        self._dropped_frames = 0
        self._written_frames = 0

        self._writer = None
        self._segment_started_at = 0.0
        self._frame_size = None

        self._thread = threading.Thread(target=self._writer_loop, daemon=True)
        self._thread.start()

    def put_frame(self, jpeg_frame: bytes):
        try:
            self._queue.put_nowait(jpeg_frame)
        except queue.Full:
            # Писатель отстаёт - выкидываем САМЫЙ СТАРЫЙ кадр в очереди
            # и кладём новый, чтобы очередь не копила растущую задержку.
            try:
                self._queue.get_nowait()
            except queue.Empty:
                pass

            try:
                self._queue.put_nowait(jpeg_frame)
            except queue.Full:
                pass

            self._dropped_frames += 1

    def stats(self):
        return {
            "written": self._written_frames,
            "dropped": self._dropped_frames,
            "queued": self._queue.qsize(),
        }

    def close(self):
        self._queue.put(None)  # сигнал остановки потоку записи
        self._thread.join(timeout=5.0)

    def _writer_loop(self):
        import cv2
        import numpy as np

        while True:
            jpeg_frame = self._queue.get()

            if jpeg_frame is None:
                break

            try:
                image = cv2.imdecode(
                    np.frombuffer(jpeg_frame, dtype=np.uint8),
                    cv2.IMREAD_COLOR,
                )

                if image is None:
                    continue

                height, width = image.shape[:2]
                self._rotate_segment_if_needed(cv2, (width, height))
                self._writer.write(image)
                self._written_frames += 1
            except Exception as error:
                logger.warning("Ошибка записи кадра: %s", error)

        if self._writer is not None:
            self._writer.release()
            self._writer = None

    def _rotate_segment_if_needed(self, cv2, frame_size):
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


class FpsCounter:
    """Считает частоту событий в скользящем окне ~report_period_s."""

    def __init__(self, report_period_s: float = 5.0):
        self.report_period_s = report_period_s
        self._count = 0
        self._window_start = time.monotonic()

    def tick(self) -> Optional[float]:
        """Увеличивает счётчик; возвращает FPS, если период истёк, иначе None."""
        self._count += 1
        now = time.monotonic()
        elapsed = now - self._window_start

        if elapsed < self.report_period_s:
            return None

        fps = self._count / elapsed
        self._count = 0
        self._window_start = now
        return fps


def run(
    camera_ip: str,
    camera_port: int,
    tcp_port: int,
    recorder: Optional[ThreadedFrameRecorder],
):
    camera = NanomsgCameraClient(camera_ip, camera_port)
    broadcaster = TcpFrameBroadcaster(tcp_port)
    broadcaster.start()

    incoming_fps_counter = FpsCounter()
    broadcast_fps_counter = FpsCounter()
    total_frames = 0
    last_frame_size = 0

    try:
        for frame in camera.frames():
            total_frames += 1
            last_frame_size = len(frame)

            incoming_fps = incoming_fps_counter.tick()

            sent = broadcaster.broadcast(frame)
            if sent:
                broadcast_fps_counter.tick()

            if recorder is not None:
                recorder.put_frame(frame)

            if incoming_fps is not None:
                rec_stats = recorder.stats() if recorder is not None else None
                logger.info(
                    "Кадров всего=%d, последний JPEG=%d байт, "
                    "FPS от камеры=%.1f%s",
                    total_frames,
                    last_frame_size,
                    incoming_fps,
                    (
                        f", запись: записано={rec_stats['written']} "
                        f"отброшено={rec_stats['dropped']} "
                        f"в очереди={rec_stats['queued']}"
                        if rec_stats is not None
                        else ""
                    ),
                )
    finally:
        if recorder is not None:
            recorder.close()


def main():
    parser = argparse.ArgumentParser(description="OmegaBot camera bridge")
    parser.add_argument("--camera-ip", required=True, help="IP-адрес камеры")
    parser.add_argument("--camera-port", type=int, default=5557)
    parser.add_argument("--tcp-port", type=int, default=5001)
    parser.add_argument("--record-to", default=None)
    parser.add_argument("--record-segment-seconds", type=int, default=600)

    args = parser.parse_args()

    recorder = None
    if args.record_to:
        recorder = ThreadedFrameRecorder(
            args.record_to,
            segment_seconds=args.record_segment_seconds,
        )

    run(args.camera_ip, args.camera_port, args.tcp_port, recorder)


if __name__ == "__main__":
    main()

"""
OmegaBot — мост между TrackingCam3 и оператором.

Камера публикует сообщения nanomsg PUB поверх WebSocket:
    ws://<camera-ip>:5557
    subprotocol: pub.sp.nanomsg.org

Для подключения к TrackingCam3 используется HTTP Origin:
    http://<camera-ip>

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
        origin = f"http://{self.camera_ip}"

        logger.info("Подключение к камере: %s", url)
        logger.info("Origin: %s", origin)

        self._socket = websocket.create_connection(
            url,
            subprotocols=[self.PROTOCOL],
            origin=origin,
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
            client_socket, address = self._server_socket.accept()

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
                >= self.segment_seconds
            )
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
                    "Видео работает: кадров=%d, "
                    "последний JPEG=%d байт.",
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
