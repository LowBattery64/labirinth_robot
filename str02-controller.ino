// ============================================================
// OmegaBot — контроллер робота на плате ARP-DEK-STR-02
// ============================================================

#include "TrackingCamDxlUart.h"


// ============================================================
// Распиновка робота
// ============================================================

class Pins
{
public:
    static const uint8_t M1_DIR = 45;
    static const uint8_t M1_SPEED = 44;

    static const uint8_t M2_DIR = 47;
    static const uint8_t M2_SPEED = 46;

    static const uint8_t IR_FRONT = 2;

    static const uint8_t US_CENTER_TRIG = 3;
    static const uint8_t US_CENTER_ECHO = 4;

    static const uint8_t US_LEFT_TRIG = 5;
    static const uint8_t US_LEFT_ECHO = 6;

    static const uint8_t US_RIGHT_TRIG = 7;
    static const uint8_t US_RIGHT_ECHO = 8;

    static const uint8_t ENCODER_LEFT = 9;
    static const uint8_t ENCODER_RIGHT = 10;
};


// ============================================================
// Общие константы движения
// ============================================================

// Используется и обычным телеуправлением (CommandProtocol), автономным возвратом (AutonomousReturn) 
const int DEFAULT_DRIVE_SPEED = 100;


// ============================================================
// Управление моторами
// ============================================================

class MotorController
{
public:
 // Инициализирует управление моторами
    void begin()
    {
        pinMode(Pins::M1_DIR, OUTPUT);
        pinMode(Pins::M1_SPEED, OUTPUT);

        pinMode(Pins::M2_DIR, OUTPUT);
        pinMode(Pins::M2_SPEED, OUTPUT);

        stop();
    }

    // Устанавливает скорость левого мотора
    void setLeft(int speed)
    {
        setMotor(Pins::M1_DIR, Pins::M1_SPEED, speed);
    }

    // Устанавливает скорость правого мотора
    void setRight(int speed)
    {
        setMotor(Pins::M2_DIR, Pins::M2_SPEED, speed);
    }

    // Останавливает оба мотора
    void stop()
    {
        setLeft(0);
        setRight(0);
    }

private:
    void setMotor(uint8_t dirPin, uint8_t speedPin, int speed)
    {
        speed = constrain(speed, -255, 255);

        digitalWrite(dirPin, speed >= 0 ? HIGH : LOW);
        analogWrite(speedPin, abs(speed));
    }
};


// ============================================================
// ИК-датчик
// ============================================================

class IRSensorArray
{
public:
    void begin()
    {
        pinMode(Pins::IR_FRONT, INPUT);
    }

    bool frontBlocked() const
    {
        return digitalRead(Pins::IR_FRONT) == HIGH;
    }
};


// ============================================================
// УЗ-дальномеры
// ============================================================

class UltrasonicSensor
{
private:
    uint8_t trigPin;
    uint8_t echoPin;

public:
    UltrasonicSensor(uint8_t trig, uint8_t echo)
        : trigPin(trig),
          echoPin(echo)
    {
    }

    void begin()
    {
        pinMode(trigPin, OUTPUT);
        pinMode(echoPin, INPUT);
    }

    // Измеряет расстояние до препятствия в сантиметрах
    long readCm() const
    {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);

        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);

        digitalWrite(trigPin, LOW);

        unsigned long duration =
            pulseIn(echoPin, HIGH, 20000UL);

        if (duration == 0)
        {
            return -1;
        }

        return duration / 29 / 2;
    }
};


class UltrasonicArray
{
private:
    UltrasonicSensor center;
    UltrasonicSensor left;
    UltrasonicSensor right;

public:
    UltrasonicArray()
        : center(Pins::US_CENTER_TRIG, Pins::US_CENTER_ECHO),
          left(Pins::US_LEFT_TRIG, Pins::US_LEFT_ECHO),
          right(Pins::US_RIGHT_TRIG, Pins::US_RIGHT_ECHO)
    {
    }

    void begin()
    {
        center.begin();
        left.begin();
        right.begin();
    }

    // Считывает расстояния со всех ультразвуковых датчиков
    void readAll(long &centerCm, long &leftCm, long &rightCm) const
    {
        centerCm = center.readCm();

        delay(10);

        leftCm = left.readCm();

        delay(10);

        rightCm = right.readCm();
    }

    long readCenterCm() const
    {
        return center.readCm();
    }

};


// ============================================================
// Энкодеры
// ============================================================

class SpeedSensorArray
{
private:
    bool lastLeftState;
    bool lastRightState;

    volatile unsigned long leftPulses;
    volatile unsigned long rightPulses;

public:
    SpeedSensorArray()
        : lastLeftState(false),
          lastRightState(false),
          leftPulses(0),
          rightPulses(0)
    {
    }

    void begin()
    {
        pinMode(Pins::ENCODER_LEFT, INPUT);
        pinMode(Pins::ENCODER_RIGHT, INPUT);

        lastLeftState =
            digitalRead(Pins::ENCODER_LEFT) == HIGH;

        lastRightState =
            digitalRead(Pins::ENCODER_RIGHT) == HIGH;
    }

   // Обновляет счётчики импульсов энкодеров
    void poll()
    {
        bool leftState =
            digitalRead(Pins::ENCODER_LEFT) == HIGH;

        if (leftState && !lastLeftState)
        {
            leftPulses++;
        }

        lastLeftState = leftState;

        bool rightState =
            digitalRead(Pins::ENCODER_RIGHT) == HIGH;

        if (rightState && !lastRightState)
        {
            rightPulses++;
        }

        lastRightState = rightState;
    }

    // Возвращает число импульсов левого энкодера
    unsigned long leftPulseCount() const
    {
        return leftPulses;
    }

    unsigned long rightPulseCount() const
    {
        return rightPulses;
    }

    // Сбрасывает счётчики энкодеров
    void resetCounters()
    {
        leftPulses = 0;
        rightPulses = 0;
    }
};


// ============================================================
// TrackingCam
// ============================================================

class CameraTracker
{
private:
    static const uint8_t CAM_ID = 51;
    static const uint8_t SERIAL_PORT = 1;
    static const uint32_t BAUD_RATE = 115200;
    static const uint8_t TIMEOUT_MS = 30;

    static const uint8_t MAX_OBJECTS = 5;

    TrackingCamDxlUart cam;

public:
    // Инициализирует TrackingCam.
    void begin()
    {
        cam.init(
            CAM_ID,
            SERIAL_PORT,
            BAUD_RATE,
            TIMEOUT_MS
        );
    }

    // Считывает обнаруженные камерой объекты
    uint8_t readObjects()
    {
        return cam.readObjects(MAX_OBJECTS);
    }

    uint8_t readBlobs()
    {
        return cam.readBlobs(MAX_OBJECTS);
    }

    // Проверяет наличие обнаруженного объекта
    bool hasObject() const
    {
        return cam.obj[0].obj_size > 0;
    }

    int objectX() const
    {
        return cam.obj[0].cx;
    }

    int objectY() const
    {
        return cam.obj[0].cy;
    }
};



// ============================================================
// Безопасность движения
// ============================================================

class SafetyController
{
private:
    UltrasonicArray &ultrasonicSensors;

    static const long FRONT_OBSTACLE_DISTANCE_CM = 25;
    static const unsigned long CHECK_PERIOD_MS = 50;

    unsigned long lastCheckMs;
    bool obstacleAhead;

public:
    explicit SafetyController(UltrasonicArray &sensorArray)
        : ultrasonicSensors(sensorArray),
          lastCheckMs(0),
          obstacleAhead(false)
    {
    }

    void begin()
    {
        lastCheckMs = millis();
        update();
    }

    // Периодически обновляет состояние переднего препятствия
    void poll()
    {
        unsigned long now = millis();

        if (now - lastCheckMs < CHECK_PERIOD_MS)
        {
            return;
        }

        lastCheckMs = now;
        update();
    }

    // Возвращает состояние передней зоны движения
    bool isForwardBlocked() const
    {
        return obstacleAhead;
    }

private:
    // Обновляет состояние препятствия по центральному дальномеру
    void update()
    {
        long distanceCm = ultrasonicSensors.readCenterCm();

        obstacleAhead =
            distanceCm > 0 &&
            distanceCm <= FRONT_OBSTACLE_DISTANCE_CM;
    }
};


// ============================================================
// История перемещений (для автономного возврата)
// ============================================================
//
// Хранит завершённые отрезки движения (команда + длительность),
// начиная от последнего момента, когда связь с оператором была
// уверенно установлена. AutonomousReturn проходит эту историю в
// обратном порядке, инвертируя каждую команду, чтобы вернуть
// робота примерно туда, где связь ещё была.
// ============================================================

class MoveHistory
{
public:
    struct Move
    {
        char command;
        unsigned long durationMs;
    };

private:
    static const uint8_t CAPACITY = 64;

    Move moves[CAPACITY];
    uint8_t count;

public:
    MoveHistory()
        : count(0)
    {
    }

   // Добавляет выполненный участок движения в историю
    void push(char command, unsigned long durationMs)
    {
        if (durationMs == 0)
        {
            return;
        }

        if (count >= CAPACITY)
        {
            // Буфер полон - теряем самый старый отрезок пути.
            for (uint8_t i = 1; i < CAPACITY; i++)
            {
                moves[i - 1] = moves[i];
            }

            count = CAPACITY - 1;
        }

        moves[count].command = command;
        moves[count].durationMs = durationMs;
        count++;
    }

    // Извлекает последний участок пути для обратного движения
    bool popInverse(char &inverseCommand, unsigned long &durationMs)
    {
        if (count == 0)
        {
            return false;
        }

        count--;

        inverseCommand = invert(moves[count].command);
        durationMs = moves[count].durationMs;

        return true;
    }

    bool isEmpty() const
    {
        return count == 0;
    }

    uint8_t size() const
    {
        return count;
    }

    // Очищает историю движения
    void clear()
    {
        count = 0;
    }

private:
    // Возвращает команду, обратную указанному движению
    static char invert(char command)
    {
        switch (command)
        {
        case 'F':
            return 'B';

        case 'B':
            return 'F';

        case 'L':
            return 'R';

        case 'R':
            return 'L';

        default:
            return 'S';
        }
    }
};


// ============================================================
// Команды управления
// ============================================================

class CommandProtocol
{
private:
    static const unsigned long COMMAND_TIMEOUT_MS = 500;

    MotorController &motors;
    SafetyController &safety;

    MoveHistory history;

    unsigned long lastCommandMs;
    unsigned long commandStartMs;
    char requestedCommand;
    bool watchdogStopped;

public:
    CommandProtocol(
        MotorController &motorController,
        SafetyController &safetyController
    )
        : motors(motorController),
          safety(safetyController),
          lastCommandMs(0),
          commandStartMs(0),
          requestedCommand('S'),
          watchdogStopped(false)
    {
    }

    // Запускает контроль команд и watchdog
    void begin()
    {
        lastCommandMs = millis();
        commandStartMs = millis();
    }

    // Принимает и выполняет команды оператора
    void poll()
    {
        if (!Serial.available())
        {
            return;
        }

        char command = Serial.read();

        if (!isValidCommand(command))
        {
            return;
        }

        lastCommandMs = millis();

        if (command != requestedCommand)
        {

            recordSegment();

            commandStartMs = millis();
            requestedCommand = command;
        }

        watchdogStopped = false;

        switch (command)
        {
        case 'F':
            if (safety.isForwardBlocked())
            {
                motors.stop();
            }
            else
            {
                motors.setLeft(DEFAULT_DRIVE_SPEED);
                motors.setRight(-DEFAULT_DRIVE_SPEED);
            }
            break;

        case 'B':
            motors.setLeft(-DEFAULT_DRIVE_SPEED);
            motors.setRight(DEFAULT_DRIVE_SPEED);
            break;

        case 'L':
            motors.setLeft(-DEFAULT_DRIVE_SPEED);
            motors.setRight(-DEFAULT_DRIVE_SPEED);
            break;

        case 'R':
            motors.setLeft(DEFAULT_DRIVE_SPEED);
            motors.setRight(DEFAULT_DRIVE_SPEED);
            break;

        case 'S':
            motors.stop();
            break;
        }
    }

    // Останавливает движение вперёд при появлении препятствия
    void safetyPoll()
    {
        if (requestedCommand == 'F' && safety.isForwardBlocked())
        {
            motors.stop();
        }
    }

    // Останавливает робота при потере команд от оператора
    void watchdogPoll()
    {
        unsigned long now = millis();

        if (
            requestedCommand != 'S' &&
            now - lastCommandMs > COMMAND_TIMEOUT_MS
        )
        {
            // Оператор пропал - фиксируем в истории отрезок,который выполнялся в момент потери связи
            recordSegment();
 
            motors.stop();
            requestedCommand = 'S';
            watchdogStopped = true;
        }
    }

    // Возвращает текущую команду управления
    char getRequestedCommand() const
    {
        return requestedCommand;
    }

    // Проверяет, сработал ли watchdog
    bool wasStoppedByWatchdog() const
    {
        return watchdogStopped;
    }

    // Сколько миллисекунд прошло с последней команды оператора
    // Используется AutonomousReturn, чтобы определить полную потерю связи  и момент, когда оператор снова на связи.
    unsigned long millisSinceLastCommand() const
    {
        return millis() - lastCommandMs;
    }

    // Возвращает историю выполненных движений
    MoveHistory &moveHistory()
    {
        return history;
    }

    // Очищает историю после возврата ручного управления.
    void resetHistoryAfterManualResume()
    {
        history.clear();
    }

private:
    // Сохраняет завершённый участок движения
    void recordSegment()
    {
        if (requestedCommand != 'S')
        {
            history.push(requestedCommand, millis() - commandStartMs);
        }
    }

    // Проверяет допустимость команды управления
    bool isValidCommand(char command) const
    {
        switch (command)
        {
        case 'F':
        case 'B':
        case 'L':
        case 'R':
        case 'S':
            return true;

        default:
            return false;
        }
    }
};


// ============================================================
// Автономный возврат при потере связи
// ============================================================
//
// Если оператор не подавал команд дольше LOST_SIGNAL_MS (это
// заведомо больше моторного watchdog'а в CommandProtocol, чтобы
// сначала сработала обычная безопасная остановка, и только потом -
// автономное поведение), робот считает связь потерянной и начинает
// проходить записанную историю движений в обратном порядке и с
// инвертированными командами, пока не вернётся примерно туда, где
// связь ещё была уверенно установлена, либо пока не восстановится
// связь с оператором - в этом случае возврат немедленно
// прерывается и управление отдаётся обратно оператору.
// ============================================================

class AutonomousReturn
{
private:
    static const unsigned long LOST_SIGNAL_MS = 3000;

    MotorController &motors;
    SafetyController &safety;
    CommandProtocol &commandProtocol;

    bool active;
    char currentCommand;
    unsigned long currentDurationMs;
    unsigned long segmentStartMs;

public:
    AutonomousReturn(
        MotorController &motorController,
        SafetyController &safetyController,
        CommandProtocol &protocol
    )
        : motors(motorController),
          safety(safetyController),
          commandProtocol(protocol),
          active(false),
          currentCommand('S'),
          currentDurationMs(0),
          segmentStartMs(0)
    {
    }

    // Управляет автономным возвратом при потере связи
    void poll()
    {
        bool operatorRecentlyActive =
            commandProtocol.millisSinceLastCommand() < LOST_SIGNAL_MS;

        if (!active)
        {
            if (
                !operatorRecentlyActive &&
                !commandProtocol.moveHistory().isEmpty()
            )
            {
                beginReturn();
            }

            return;
        }

        if (operatorRecentlyActive)
        {
            // Оператор снова на связи. CommandProtocol::poll() в этой
            // же итерации loop() уже успел обработать его свежую
            // команду и выставить моторы соответствующим образом
            // (millisSinceLastCommand не может обновиться иначе) -
            // поэтому здесь НЕ вызываем motors.stop(), чтобы не
            // затереть эту команду обратно в остановку. Мы лишь
            // отдаём управление обратно и считаем текущее место
            // новой "точкой связи".
            active = false;
            commandProtocol.resetHistoryAfterManualResume();
            return;
        }

        runCurrentSegment();
    }

    // Проверяет, выполняется ли автономный возврат
    bool isActive() const
    {
        return active;
    }

private:
    // Запускает движение по обратной истории
    void beginReturn()
    {
        active = true;
        advanceToNextSegment();
    }

    // Переходит к следующему участку обратного пути
    void advanceToNextSegment()
    {
        if (!commandProtocol.moveHistory().popInverse(currentCommand, currentDurationMs))
        {
            // История пройдена целиком - останавливаемся и ждём,
            // пока не восстановится связь либо кто-то не заберёт
            // робота вручную.
            motors.stop();
            active = false;
            return;
        }

        segmentStartMs = millis();
        applyCommand(currentCommand);
    }
    // Выполняет текущий участок автономного пути
    void runCurrentSegment()
    {
        if (currentCommand == 'F' && safety.isForwardBlocked())
        {
            // Путь возврата неожиданно перекрыт - стоим и ждём,
            // не тратя время сегмента впустую, пока не освободится.
            motors.stop();
            segmentStartMs = millis();
            return;
        }

        if (millis() - segmentStartMs >= currentDurationMs)
        {
            advanceToNextSegment();
        }
    }
    // Применяет команду движения во время автономного возврата
    void applyCommand(char command)
    {
        switch (command)
        {
        case 'F':
            if (safety.isForwardBlocked())
            {
                motors.stop();
            }
            else
            {
                motors.setLeft(DEFAULT_DRIVE_SPEED);
                motors.setRight(-DEFAULT_DRIVE_SPEED);
            }
            break;

        case 'B':

            motors.setLeft(-DEFAULT_DRIVE_SPEED);
            motors.setRight(DEFAULT_DRIVE_SPEED);
            break;

        case 'L':
            motors.setLeft(-DEFAULT_DRIVE_SPEED);
            motors.setRight(-DEFAULT_DRIVE_SPEED);
            break;

        case 'R':
            motors.setLeft(DEFAULT_DRIVE_SPEED);
            motors.setRight(DEFAULT_DRIVE_SPEED);
            break;

        default:
            motors.stop();
            break;
        }
    }
};


// ============================================================
// Объекты системы
// ============================================================

MotorController motors;
IRSensorArray irSensors;
UltrasonicArray ultrasonicSensors;
SpeedSensorArray speedSensors;
CameraTracker camera;
SafetyController safetyController(ultrasonicSensors);
CommandProtocol commandProtocol(motors, safetyController);
AutonomousReturn autonomousReturn(motors, safetyController, commandProtocol);

unsigned long lastTelemetryMs = 0;

const unsigned long TELEMETRY_PERIOD_MS = 200;


// ============================================================
// Телеметрия
// ============================================================

// Отправляет текущую телеметрию робота оператору
void sendTelemetry()
{
    long usCenter;
    long usLeft;
    long usRight;

    ultrasonicSensors.readAll(
        usCenter,
        usLeft,
        usRight
    );

    Serial.print("T,IR,");
    Serial.print(irSensors.frontBlocked());

    Serial.print(",US,");
    Serial.print(usCenter);
    Serial.print(',');
    Serial.print(usLeft);
    Serial.print(',');
    Serial.print(usRight);

    Serial.print(",ENC,");
    Serial.print(speedSensors.leftPulseCount());
    Serial.print(',');
    Serial.print(speedSensors.rightPulseCount());

    Serial.print(",SAFE,");
    Serial.print(safetyController.isForwardBlocked());

    Serial.print(",CMD,");
    Serial.print(commandProtocol.getRequestedCommand());

    Serial.print(",WD,");
    Serial.print(commandProtocol.wasStoppedByWatchdog());

    Serial.print(",MODE,");
    Serial.print(autonomousReturn.isActive() ? "AUTO" : "TELEOP");

    Serial.print(",HIST,");
    Serial.print(commandProtocol.moveHistory().size());



    Serial.println();
}


// ============================================================
// Инициализация
// ============================================================

void setup()
{
    Serial.begin(115200);

    motors.begin();

    irSensors.begin();

    ultrasonicSensors.begin();

    speedSensors.begin();

    safetyController.begin();

    commandProtocol.begin();

    camera.begin();
}


// ============================================================
// Основной цикл
// ============================================================

void loop()
{
    commandProtocol.poll();

    speedSensors.poll();

    safetyController.poll();

    commandProtocol.safetyPoll();

    commandProtocol.watchdogPoll();

    // К моменту, когда AutonomousReturn решает взять управление,
    // CommandProtocol уже перевёл requestedCommand в 'S' (через
    // watchdogPoll), поэтому safetyPoll/watchdogPoll выше больше не
    // трогают моторы и не мешают автономному движению.
    autonomousReturn.poll();

    unsigned long now = millis();

    if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
    {
        lastTelemetryMs = now;

        sendTelemetry();
    }
}
