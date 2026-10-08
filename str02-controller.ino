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

// Используется и обычным телеуправлением (CommandProtocol), и
// автономным возвратом (AutonomousReturn) - обе логики должны
// давать одинаковую по величине скорость, иначе пройденное
// расстояние при возврате не будет соответствовать записанному.
const int DEFAULT_DRIVE_SPEED = 120;


// ============================================================
// Управление моторами
// ============================================================

class MotorController
{
public:
    void begin()
    {
        pinMode(Pins::M1_DIR, OUTPUT);
        pinMode(Pins::M1_SPEED, OUTPUT);

        pinMode(Pins::M2_DIR, OUTPUT);
        pinMode(Pins::M2_SPEED, OUTPUT);

        stop();
    }

    void setLeft(int speed)
    {
        setMotor(Pins::M1_DIR, Pins::M1_SPEED, speed);
    }

    void setRight(int speed)
    {
        setMotor(Pins::M2_DIR, Pins::M2_SPEED, speed);
    }

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

    unsigned long leftPulseCount() const
    {
        return leftPulses;
    }

    unsigned long rightPulseCount() const
    {
        return rightPulses;
    }

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
    void begin()
    {
        cam.init(
            CAM_ID,
            SERIAL_PORT,
            BAUD_RATE,
            TIMEOUT_MS
        );
    }

    uint8_t readObjects()
    {
        return cam.readObjects(MAX_OBJECTS);
    }

    uint8_t readBlobs()
    {
        return cam.readBlobs(MAX_OBJECTS);
    }

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
    bool rammingEnabled;

public:
    explicit SafetyController(UltrasonicArray &sensorArray)
        : ultrasonicSensors(sensorArray),
          lastCheckMs(0),
          obstacleAhead(false),
          rammingEnabled(false)
    {
    }

    void begin()
    {
        lastCheckMs = millis();
        update();
    }

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

    // Препятствие реально блокирует движение, только если таран
    // ВЫКЛЮЧЕН. При включённом таране метод всегда возвращает false -
    // весь остальной код (CommandProtocol, ScoutReturn) ничего не
    // знает про таран и просто спрашивает isForwardBlocked(), как и
    // раньше.
    bool isForwardBlocked() const
    {
        return obstacleAhead && !rammingEnabled;
    }

    // Сырое показание дальномера без учёта тарана - на случай, если
    // понадобится показать оператору, что препятствие физически есть,
    // даже когда он сам отключил защиту.
    bool rawObstacleAhead() const
    {
        return obstacleAhead;
    }

    void setRammingEnabled(bool enabled)
    {
        rammingEnabled = enabled;
    }

    bool isRammingEnabled() const
    {
        return rammingEnabled;
    }

private:
    void update()
    {
        long distanceCm = ultrasonicSensors.readCenterCm();

        obstacleAhead =
            distanceCm > 0 &&
            distanceCm <= FRONT_OBSTACLE_DISTANCE_CM;
    }
};


// ============================================================
// История перемещений для резервного возврата
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

    void push(char command, unsigned long durationMs)
    {
        if (command == 'S' || durationMs == 0)
        {
            return;
        }

        if (count >= CAPACITY)
        {
            for (uint8_t i = 1; i < CAPACITY; ++i)
            {
                moves[i - 1] = moves[i];
            }

            count = CAPACITY - 1;
        }

        moves[count].command = command;
        moves[count].durationMs = durationMs;
        ++count;
    }

    bool popInverse(char &command, unsigned long &durationMs)
    {
        if (count == 0)
        {
            return false;
        }

        --count;

        command = invert(moves[count].command);
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

    void clear()
    {
        count = 0;
    }

private:
    static char invert(char command)
    {
        switch (command)
        {
        case 'F': return 'B';
        case 'B': return 'F';
        case 'L': return 'R';
        case 'R': return 'L';
        default:  return 'S';
        }
    }
};


// ============================================================
// Выполнение пользовательского пакета команд
// ============================================================
//
// Пакет задаётся оператором в интерфейсе и имеет вид:
//
//   P,F:700;R:1900;B:700
//
// где каждая пара означает команду и время её выполнения в миллисекундах.
// Пакет целиком передаётся на Arduino до заезда в зону без связи.
// После получения Arduino хранит его в памяти и выполняет самостоятельно.
// Поэтому потеря TCP-связи не прерывает пакет.
//
// Пакет должен содержать полный маршрут: например, движение в зону без
// связи, разворот и обратное движение. Никакой фиксированной
// последовательности внутри Arduino нет.
// ============================================================

class CommandPacketExecutor
{
public:
    struct Step
    {
        char command;
        unsigned long durationMs;
    };

private:
    static const uint8_t MAX_STEPS = 24;

    MotorController &motors;
    SafetyController &safety;
    MoveHistory &history;

    Step steps[MAX_STEPS];
    uint8_t stepCount;
    uint8_t currentStep;
    unsigned long stepStartMs;
    bool active;

public:
    CommandPacketExecutor(
        MotorController &motorController,
        SafetyController &safetyController,
        MoveHistory &moveHistory
    )
        : motors(motorController),
          safety(safetyController),
          history(moveHistory),
          stepCount(0),
          currentStep(0),
          stepStartMs(0),
          active(false)
    {
    }

    bool loadAndStart(const String &packet)
    {
        if (packet.length() < 3 || packet[0] != 'P' || packet[1] != ',')
        {
            return false;
        }

        Step parsed[MAX_STEPS];
        uint8_t parsedCount = 0;

        int tokenStart = 2;

        while (tokenStart < packet.length())
        {
            int tokenEnd = packet.indexOf(';', tokenStart);

            if (tokenEnd < 0)
            {
                tokenEnd = packet.length();
            }

            String token = packet.substring(tokenStart, tokenEnd);
            token.trim();

            int separator = token.indexOf(':');

            if (separator <= 0)
            {
                return false;
            }

            char command = token.charAt(0);
            String durationText = token.substring(separator + 1);
            durationText.trim();

            if (!isMovementCommand(command) || durationText.length() == 0)
            {
                return false;
            }

            unsigned long durationMs = durationText.toInt();

            if (durationMs == 0 || parsedCount >= MAX_STEPS)
            {
                return false;
            }

            parsed[parsedCount].command = command;
            parsed[parsedCount].durationMs = durationMs;
            ++parsedCount;

            tokenStart = tokenEnd + 1;
        }

        if (parsedCount == 0)
        {
            return false;
        }

        motors.stop();
        history.clear();

        for (uint8_t i = 0; i < parsedCount; ++i)
        {
            steps[i] = parsed[i];
        }

        stepCount = parsedCount;
        currentStep = 0;
        stepStartMs = millis();
        active = true;

        applyCurrentStep();

        return true;
    }

    void poll()
    {
        if (!active)
        {
            return;
        }

        if (currentStep >= stepCount)
        {
            finish();
            return;
        }

        applyCurrentStep();

        if (millis() - stepStartMs >= steps[currentStep].durationMs)
        {
            history.push(
                steps[currentStep].command,
                steps[currentStep].durationMs
            );

            ++currentStep;
            stepStartMs = millis();

            if (currentStep >= stepCount)
            {
                finish();
            }
            else
            {
                applyCurrentStep();
            }
        }
    }

    void cancel()
    {
        if (!active)
        {
            return;
        }

        if (currentStep < stepCount)
        {
            history.push(
                steps[currentStep].command,
                millis() - stepStartMs
            );
        }

        active = false;
        stepCount = 0;
        currentStep = 0;
        motors.stop();
    }

    bool isActive() const
    {
        return active;
    }

private:
    static bool isMovementCommand(char command)
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

    void applyCurrentStep()
    {
        if (currentStep >= stepCount)
        {
            return;
        }

        switch (steps[currentStep].command)
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

    void finish()
    {
        motors.stop();
        active = false;
        stepCount = 0;
        currentStep = 0;
    }
};


// ============================================================
// Команды управления
// ============================================================

class CommandProtocol
{
private:
    static const unsigned long COMMAND_TIMEOUT_MS = 500;
    static const uint8_t SERIAL_LINE_BUFFER_SIZE = 160;

    MotorController &motors;
    SafetyController &safety;
    CommandPacketExecutor &packetExecutor;
    MoveHistory &history;

    unsigned long lastCommandMs;
    unsigned long commandStartMs;
    char requestedCommand;
    bool watchdogStopped;

    char serialLine[SERIAL_LINE_BUFFER_SIZE];
    uint8_t serialLineLength;

public:
    CommandProtocol(
        MotorController &motorController,
        SafetyController &safetyController,
        CommandPacketExecutor &executor,
        MoveHistory &moveHistory
    )
        : motors(motorController),
          safety(safetyController),
          packetExecutor(executor),
          history(moveHistory),
          lastCommandMs(0),
          requestedCommand('S'),
          watchdogStopped(false),
          serialLineLength(0)
    {
    }

    void begin()
    {
        lastCommandMs = millis();
        commandStartMs = millis();
    }

    void poll()
    {
        while (Serial.available())
        {
            char received = Serial.read();

            if (received == '\n' || received == '\r')
            {
                if (serialLineLength > 0)
                {
                    serialLine[serialLineLength] = '\0';
                    processLine(String(serialLine));
                    serialLineLength = 0;
                }

                continue;
            }

            if (serialLineLength < SERIAL_LINE_BUFFER_SIZE - 1)
            {
                serialLine[serialLineLength++] = received;
            }
            else
            {
                serialLineLength = 0;
            }
        }
    }

    void safetyPoll()
    {
        if (
            !packetExecutor.isActive() &&
            requestedCommand == 'F' &&
            safety.isForwardBlocked()
        )
        {
            motors.stop();
        }
    }

    void watchdogPoll()
    {
        if (packetExecutor.isActive())
        {
            return;
        }

        unsigned long now = millis();

        if (
            requestedCommand != 'S' &&
            now - lastCommandMs > COMMAND_TIMEOUT_MS
        )
        {
            recordCurrentSegment();
            motors.stop();
            requestedCommand = 'S';
            watchdogStopped = true;
        }
    }

    void packetPoll()
    {
        packetExecutor.poll();
    }

    char getRequestedCommand() const
    {
        return requestedCommand;
    }

    bool wasStoppedByWatchdog() const
    {
        return watchdogStopped;
    }

    bool packetActive() const
    {
        return packetExecutor.isActive();
    }

    bool hasHistory() const
    {
        return !history.isEmpty();
    }

    MoveHistory &moveHistory()
    {
        return history;
    }

    unsigned long millisSinceLastCommand() const
    {
        return millis() - lastCommandMs;
    }

    void clearHistory()
    {
        history.clear();
    }

    const char *modeName() const
    {
        return packetExecutor.isActive() ? "PACKET" : "TELEOP";
    }

private:
    void processLine(String line)
    {
        line.trim();

        if (line.length() == 0)
        {
            return;
        }

        lastCommandMs = millis();
        watchdogStopped = false;

        if (line.startsWith("P,"))
        {
            // Пакет принимается целиком. После успешного запуска
            // дальнейшее выполнение не зависит от TCP-связи.
            if (packetExecutor.loadAndStart(line))
            {
                requestedCommand = 'S';
            }

            return;
        }

        if (line.length() != 1)
        {
            return;
        }

        processSingleCommand(line.charAt(0));
    }

    void processSingleCommand(char command)
    {
        if (!isValidCommand(command))
        {
            return;
        }

        // Любая ручная команда оператора отменяет выполняемый пакет.
        if (packetExecutor.isActive())
        {
            packetExecutor.cancel();
        }

        switch (command)
        {
        case 'X':
            safety.setRammingEnabled(true);
            return;

        case 'x':
            safety.setRammingEnabled(false);
            return;

        default:
            break;
        }

        if (command != requestedCommand)
        {
            recordCurrentSegment();
            commandStartMs = millis();
        }

        requestedCommand = command;

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

    void recordCurrentSegment()
    {
        if (requestedCommand != 'S')
        {
            history.push(
                requestedCommand,
                millis() - commandStartMs
            );
        }
    }

    bool isValidCommand(char command) const
    {
        switch (command)
        {
        case 'F':
        case 'B':
        case 'L':
        case 'R':
        case 'S':
        case 'X':
        case 'x':
            return true;

        default:
            return false;
        }
    }
};


// ============================================================
// Резервный автономный возврат по истории движения
// ============================================================

class HistoryReturn
{
private:
    static const unsigned long LOST_SIGNAL_MS = 3000;

    MotorController &motors;
    SafetyController &safety;
    CommandProtocol &commandProtocol;

    enum State : uint8_t
    {
        WAITING,
        RETURNING
    };

    State state;
    unsigned long phaseStartMs;
    char currentCommand;
    unsigned long currentDurationMs;

public:
    HistoryReturn(
        MotorController &motorController,
        SafetyController &safetyController,
        CommandProtocol &protocol
    )
        : motors(motorController),
          safety(safetyController),
          commandProtocol(protocol),
          state(WAITING),
          phaseStartMs(0),
          currentCommand('S'),
          currentDurationMs(0)
    {
    }

    void poll()
    {
        if (commandProtocol.packetActive())
        {
            return;
        }

        const bool operatorRecentlyActive =
            commandProtocol.millisSinceLastCommand() < LOST_SIGNAL_MS;

        if (state == WAITING)
        {
            if (!operatorRecentlyActive && commandProtocol.hasHistory())
            {
                beginReturn();
            }

            return;
        }

        if (operatorRecentlyActive)
        {
            motors.stop();
            commandProtocol.clearHistory();
            state = WAITING;
            return;
        }

        runReturn();
    }

    const char *stateName() const
    {
        return state == RETURNING ? "HISTORY" : "TELEOP";
    }

private:
    void beginReturn()
    {
        if (!loadNext())
        {
            state = WAITING;
            return;
        }

        state = RETURNING;
    }

    bool loadNext()
    {
        if (!commandProtocol.moveHistory().popInverse(
                currentCommand,
                currentDurationMs))
        {
            motors.stop();
            state = WAITING;
            return false;
        }

        phaseStartMs = millis();
        applyCurrentCommand();

        return true;
    }

    void runReturn()
    {
        if (currentCommand == 'F' && safety.isForwardBlocked())
        {
            motors.stop();
        }
        else
        {
            applyCurrentCommand();
        }

        if (millis() - phaseStartMs >= currentDurationMs)
        {
            if (!loadNext())
            {
                motors.stop();
            }
        }
    }

    void applyCurrentCommand()
    {
        switch (currentCommand)
        {
        case 'F':
            motors.setLeft(DEFAULT_DRIVE_SPEED);
            motors.setRight(-DEFAULT_DRIVE_SPEED);
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
MoveHistory moveHistory;
CommandPacketExecutor commandPacketExecutor(
    motors,
    safetyController,
    moveHistory
);
CommandProtocol commandProtocol(
    motors,
    safetyController,
    commandPacketExecutor,
    moveHistory
);
HistoryReturn historyReturn(
    motors,
    safetyController,
    commandProtocol
);

unsigned long lastTelemetryMs = 0;

const unsigned long TELEMETRY_PERIOD_MS = 200;


// ============================================================
// Телеметрия
// ============================================================

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

    Serial.print(",RAM,");
    Serial.print(safetyController.isRammingEnabled());

    Serial.print(",CMD,");
    Serial.print(commandProtocol.getRequestedCommand());

    Serial.print(",WD,");
    Serial.print(commandProtocol.wasStoppedByWatchdog());

    Serial.print(",ARMED,");
    Serial.print(commandProtocol.packetActive());

    Serial.print(",MODE,");
    Serial.print(
        commandPacketExecutor.isActive()
            ? "PACKET"
            : historyReturn.stateName()
    );

    Serial.print(",HIST,");
    Serial.print(moveHistory.size());

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
    commandProtocol.packetPoll();
    historyReturn.poll();

    unsigned long now = millis();

    if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
    {
        lastTelemetryMs = now;
        sendTelemetry();
    }
}
