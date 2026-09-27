
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

Команды оператора для качества и записи идут отдельным TCP-каналом
на порту 5002.

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

------------------------------------------------------------
Запись должна продолжаться и при потере связи с оператором, но
видео не идёт без открытой в браузере страницы модуля.
------------------------------------------------------------
Раньше единственный способ заставить TrackingCam3 публиковать кадры
в nanomsg PUB (порт 5557) - открыть в браузере его веб-страницу.
Без этого поток вообще не начинает идти, а значит и запись на
флешку (которая должна работать всегда, включая автономный режим
без оператора) зависит от того, открыт ли у кого-то браузер - то
есть фактически от человека, а не от самого робота.

Разобрался, откуда это идёт: официальный ROS-драйвер камеры
(репозиторий tc3-ros-package самого производителя) НЕ открывает
никакой браузер - он подключается через официальную библиотеку
`motorcortex-python` сразу к двум портам камеры (5558 - запрос／ответ,
5557 - публикация) и явно ПОДПИСЫВАЕТСЯ на параметр
"root/Processing/image". Именно эта подписка и "включает" поток на
стороне камеры - когда открывали браузер, его JS внутри страницы
делал ровно то же самое подключение и подписку, только незаметно.

Поэтому теперь есть второй, официальный способ получения кадров -
класс MotorcortexCameraClient. Он делает ту же подписку напрямую из
camera_bridge.py, без какого-либо браузера, и потому не зависит от
того, открыта ли у кого-то страница - camera_bridge.py на Pi может
работать (и, соответственно, писать видео на флешку) полностью
самостоятельно, даже когда связи с оператором нет вообще.

Требования для этого пути (отсутствуют по умолчанию, нужно поставить
один раз):
    pip3 install motorcortex-python
    файл motorcortex.crt рядом с camera_bridge.py (взят из
    официального репозитория tc3-ros-package - он уже приложен)

Выбирается флагом --transport:
    --transport nanomsg      (по умолчанию, старое поведение,
                               требует открытой страницы в браузере)
    --transport motorcortex  (новый способ, без браузера; нужно
                               поставить motorcortex-python и иметь
                               рядом motorcortex.crt)

ВАЖНО: путь через motorcortex-python скопирован из официального
рабочего драйвера производителя (та же логика: URL подключения,
пути параметров, логин/пароль), но на реальном модуле Julia он ещё
не тестировался - логин/пароль ("root"/"vectioneer") могут
отличаться от того, что стоит на конкретном экземпляре камеры, это
нужно проверить на месте. Если подключение не пройдёт с этими
данными - попробовать логин/пароль от веб-админки модуля
("root"/"12345") как альтернативу.
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
    """
    Подключение к PUB WebSocket камеры с автоматическим reconnect.

    ВНИМАНИЕ: этот способ работает, только пока у кого-то открыта
    страница веб-интерфейса камеры в браузере (см. пояснение в шапке
    файла) - для полностью автономной записи без участия человека
    используйте MotorcortexCameraClient (--transport motorcortex).
    """

    PROTOCOL = "pub.sp.nanomsg.org"

    def __init__(self, camera_ip: str, camera_port: int, reconnect_delay_s: float = 2.0):
        self.camera_ip = camera_ip
        self.camera_port = camera_port
        self.reconnect_delay_s = reconnect_delay_s
        self._socket = None

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
        import websocket  # ленивый импорт: не нужен при --transport motorcortex

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


class MotorcortexCameraClient:
    """
    Получение видео от TrackingCam3 через официальную библиотеку
    motorcortex-python - БЕЗ открытой страницы в браузере.

    Подключается к камере как это делает официальный ROS-драйвер
    (tc3-ros-package): request/reply на порту 5558 + подписка на
    публикацию на порту 5557, явно оформляя подписку на параметр
    "root/Processing/image". Именно эта подписка запускает поток на
    стороне камеры - раньше её неявно делал JS в открытой странице
    браузера, теперь её делает сам camera_bridge.py.

    Требует: pip3 install motorcortex-python, и файл сертификата
    (по умолчанию motorcortex.crt рядом с этим файлом).

    Библиотека сама переподключается (reconnect=True по умолчанию)
    при обрыве связи с камерой - см. документацию motorcortex-python.
    """

    def __init__(
        self,
        camera_ip: str,
        cert_path: str,
        login: str = "root",
        password: str = "vectioneer",
        req_port: int = 5558,
        sub_port: int = 5557,
        image_path: str = "root/Processing/image",
    ):
        self.camera_ip = camera_ip
        self.cert_path = cert_path
        self.login = login
        self.password = password
        self.req_port = req_port
        self.sub_port = sub_port
        self.image_path = image_path

        # Небольшая очередь между callback'ом подписки (вызывается
        # библиотекой из своего внутреннего потока) и генератором
        # frames() ниже (вызывается из основного потока run()).
        # Как и в ThreadedFrameRecorder - если основной цикл на
        # секунду отстал, лучше отбросить старый кадр, чем копить
        # задержку.
        self._frame_queue: "queue.Queue[bytes]" = queue.Queue(maxsize=4)

        self._req = None
        self._sub = None

    def _on_image(self, values):
        try:
            jpeg_bytes = bytes(values[0].value)
        except Exception as error:
            logger.warning("Не удалось прочитать кадр от motorcortex: %s", error)
            return

        try:
            self._frame_queue.put_nowait(jpeg_bytes)
        except queue.Full:
            try:
                self._frame_queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self._frame_queue.put_nowait(jpeg_bytes)
            except queue.Full:
                pass

    def _connect(self):
        import motorcortex

        parameter_tree = motorcortex.ParameterTree()
        motorcortex_types = motorcortex.MessageTypes()

        url = f"ws://{self.camera_ip}:{self.req_port}:{self.sub_port}"
        logger.info("Подключение к камере через motorcortex: %s", url)

        # reconnect=True (по умолчанию в библиотеке) - переподключение
        # при обрыве связи с камерой обрабатывает сама motorcortex-python.
        self._req, self._sub = motorcortex.connect(
            url,
            motorcortex_types,
            parameter_tree,
            certificate=self.cert_path,
            login=self.login,
            password=self.password,
            conn_timeout_ms=3000,
        )

        subscription = self._sub.subscribe([self.image_path], "camera_bridge", 1)
        subscription.get()
        subscription.notify(self._on_image)

        logger.info("Подписка на %s оформлена - видео пойдёт без браузера.", self.image_path)

    def frames(self):
        self._connect()

        while True:
            yield self._frame_queue.get()


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



class StreamProcessor:
    """Применяет выбранный оператором профиль качества к JPEG-потоку."""

    PROFILES = {
        "HIGH": (1.0, 85),
        "MEDIUM": (0.75, 70),
        "LOW": (0.5, 55),
    }

    def __init__(self):
        self._quality = "HIGH"
        self._lock = threading.Lock()

    def set_quality(self, quality: str) -> bool:
        quality = quality.upper()
        if quality not in self.PROFILES:
            return False

        with self._lock:
            self._quality = quality
        logger.info("Качество видеопотока: %s", quality)
        return True

    def get_quality(self) -> str:
        with self._lock:
            return self._quality

    def process(self, jpeg_frame: bytes) -> bytes:
        with self._lock:
            quality = self._quality

        if quality == "HIGH":
            return jpeg_frame

        import cv2
        import numpy as np

        image = cv2.imdecode(
            np.frombuffer(jpeg_frame, dtype=np.uint8),
            cv2.IMREAD_COLOR,
        )

        if image is None:
            return jpeg_frame

        scale, jpeg_quality = self.PROFILES[quality]
        if scale != 1.0:
            image = cv2.resize(
                image,
                None,
                fx=scale,
                fy=scale,
                interpolation=cv2.INTER_AREA,
            )

        success, encoded = cv2.imencode(
            ".jpg",
            image,
            [cv2.IMWRITE_JPEG_QUALITY, jpeg_quality],
        )

        if not success:
            return jpeg_frame

        return encoded.tobytes()


class VideoControlServer:
    """TCP-канал команд от операторского интерфейса."""

    def __init__(self, port: int, controller):
        self.port = port
        self.controller = controller
        self._server_socket: Optional[socket.socket] = None

    def start(self):
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind(("0.0.0.0", self.port))
        self._server_socket.listen(4)

        logger.info("Канал управления видео запущен на порту %d.", self.port)
        threading.Thread(target=self._accept_loop, daemon=True).start()

    def _accept_loop(self):
        assert self._server_socket is not None

        while True:
            client_socket, address = self._server_socket.accept()
            client_socket.settimeout(None)
            logger.info("ПК подключён (управление видео): %s", address)
            threading.Thread(
                target=self._client_loop,
                args=(client_socket,),
                daemon=True,
            ).start()

    def _client_loop(self, client_socket: socket.socket):
        try:
            client_socket.sendall(
                f"QUALITY {self.controller.get_quality()}\n".encode()
            )
            client_socket.sendall(
                f"RECORD {'ON' if self.controller.is_recording() else 'OFF'}\n".encode()
            )

            buffer = b""
            while True:
                data = client_socket.recv(4096)
                if not data:
                    return

                buffer += data

                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    response = self._handle_command(line.decode("utf-8", errors="replace").strip())
                    if response:
                        client_socket.sendall((response + "\n").encode())
        except (ConnectionError, OSError):
            pass
        finally:
            try:
                client_socket.close()
            except OSError:
                pass

    def _handle_command(self, command: str) -> str:
        parts = command.upper().split()

        if parts == ["STATUS"]:
            return (
                f"QUALITY {self.controller.get_quality()}\n"
                f"RECORD {'ON' if self.controller.is_recording() else 'OFF'}"
            )

        if len(parts) == 2 and parts[0] == "QUALITY":
            if self.controller.set_quality(parts[1]):
                return f"QUALITY {parts[1]}"
            return "ERROR Неизвестный профиль качества"

        if len(parts) == 2 and parts[0] == "RECORD":
            if parts[1] == "START":
                try:
                    self.controller.start_recording()
                    return "RECORD ON"
                except Exception as error:
                    return f"ERROR Не удалось начать запись: {error}"

            if parts[1] == "STOP":
                self.controller.stop_recording()
                return "RECORD OFF"

        return "ERROR Неизвестная команда"


class VideoController:
    """Общее состояние видеопотока, качества и записи."""

    def __init__(
        self,
        record_dir: Optional[str],
        segment_seconds: int,
        fps: float = 20.0,
    ):
        self.stream_processor = StreamProcessor()
        self.record_dir = Path(record_dir) if record_dir else None
        self.segment_seconds = segment_seconds
        self.fps = fps
        self._recorder: Optional[ThreadedFrameRecorder] = None
        self._recorder_lock = threading.Lock()

    def set_quality(self, quality: str) -> bool:
        return self.stream_processor.set_quality(quality)

    def get_quality(self) -> str:
        return self.stream_processor.get_quality()

    def is_recording(self) -> bool:
        with self._recorder_lock:
            return self._recorder is not None

    def start_recording(self):
        if self.record_dir is None:
            raise RuntimeError("Папка записи не задана")

        self.record_dir.mkdir(parents=True, exist_ok=True)

        with self._recorder_lock:
            if self._recorder is not None:
                return
            self._recorder = ThreadedFrameRecorder(
                str(self.record_dir),
                segment_seconds=self.segment_seconds,
                fps=self.fps,
            )

        logger.info("Запись видео запущена: %s", self.record_dir)

    def stop_recording(self):
        with self._recorder_lock:
            recorder = self._recorder
            self._recorder = None

        if recorder is not None:
            recorder.close()
            logger.info("Запись видео остановлена.")

    def process(self, jpeg_frame: bytes) -> bytes:
        processed_frame = self.stream_processor.process(jpeg_frame)

        with self._recorder_lock:
            recorder = self._recorder

        if recorder is not None:
            recorder.put_frame(processed_frame)

        return processed_frame


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
    camera,
    tcp_port: int,
    video_control_port: int,
    controller: VideoController,
):
    broadcaster = TcpFrameBroadcaster(tcp_port)
    broadcaster.start()

    control_server = VideoControlServer(video_control_port, controller)
    control_server.start()

    incoming_fps_counter = FpsCounter()
    broadcast_fps_counter = FpsCounter()
    total_frames = 0
    last_frame_size = 0

    try:
        for frame in camera.frames():
            total_frames += 1
            last_frame_size = len(frame)

            incoming_fps = incoming_fps_counter.tick()

            processed_frame = controller.process(frame)

            sent = broadcaster.broadcast(processed_frame)
            if sent:
                broadcast_fps_counter.tick()

            if incoming_fps is not None:
                logger.info(
                    "Кадров всего=%d, входной JPEG=%d байт, "
                    "выходной JPEG=%d байт, FPS от камеры=%.1f, "
                    "качество=%s, запись=%s",
                    total_frames,
                    last_frame_size,
                    len(processed_frame),
                    incoming_fps,
                    controller.get_quality(),
                    "ON" if controller.is_recording() else "OFF",
                )
    finally:
        controller.stop_recording()


def main():
    parser = argparse.ArgumentParser(description="OmegaBot camera bridge")
    parser.add_argument("--camera-ip", required=True, help="IP-адрес камеры")
    parser.add_argument("--tcp-port", type=int, default=5001)
    parser.add_argument("--control-port", type=int, default=5002)
    parser.add_argument("--record-to", default=None)
    parser.add_argument("--record-segment-seconds", type=int, default=600)

    parser.add_argument(
        "--transport",
        choices=["nanomsg", "motorcortex"],
        default="nanomsg",
        help=(
            "nanomsg (по умолчанию) - старый способ, требует открытой "
            "страницы камеры в браузере. motorcortex - официальный способ "
            "без браузера, нужен `pip3 install motorcortex-python` и файл "
            "сертификата (см. --motorcortex-cert)."
        ),
    )
    parser.add_argument("--camera-port", type=int, default=5557, help="Порт публикации (nanomsg)")
    parser.add_argument("--motorcortex-req-port", type=int, default=5558)
    parser.add_argument("--motorcortex-sub-port", type=int, default=5557)
    parser.add_argument(
        "--motorcortex-cert",
        default=str(Path(__file__).parent / "motorcortex.crt"),
        help="Путь к сертификату motorcortex.crt (нужен только для --transport motorcortex)",
    )
    parser.add_argument("--motorcortex-login", default="root")
    parser.add_argument("--motorcortex-password", default="vectioneer")

    args = parser.parse_args()

    controller = VideoController(
        args.record_to,
        segment_seconds=args.record_segment_seconds,
    )

    if args.transport == "motorcortex":
        camera = MotorcortexCameraClient(
            args.camera_ip,
            cert_path=args.motorcortex_cert,
            login=args.motorcortex_login,
            password=args.motorcortex_password,
            req_port=args.motorcortex_req_port,
            sub_port=args.motorcortex_sub_port,
        )
    else:
        camera = NanomsgCameraClient(args.camera_ip, args.camera_port)

    run(camera, args.tcp_port, args.control_port, controller)


if __name__ == "__main__":
    main()
