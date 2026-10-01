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
const int DEFAULT_DRIVE_SPEED = 80;


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
// История перемещений (для автономного возврата)
// ============================================================
//
// Хранит завершённые отрезки движения (команда + длительность),
// начиная от последнего момента, когда связь с оператором была
// уверенно установлена. AutonomousReturn проходит эту историю в
// обратном порядке, инвертируя каждую команду, чтобы вернуть
// робота примерно туда, где связь ещё была.
//
// Это не полноценная локализация/SLAM, а простое "хлебное
// крошево" по времени выполнения команд - для набора без
// одометрии/картографирования этого достаточно, чтобы выполнить
// требование ТЗ по возврату в зону связи.
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
        if (durationMs == 0)
        {
            return;
        }

        if (count >= CAPACITY)
        {
            // Буфер полон - теряем самый старый отрезок пути.
            // Робот в этом случае вернётся не в точку самой первой
            // потери связи, а в ближайшую по времени зафиксированную -
            // приемлемый компромисс при ограниченной памяти Mega2560.
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

    void clear()
    {
        count = 0;
    }

private:
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
    bool armedForReturn;

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
          watchdogStopped(false),
          armedForReturn(false)
    {
    }

    void begin()
    {
        lastCommandMs = millis();
        commandStartMs = millis();
    }

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

        // Любой валидный байт (включая управляющие 'A'/'a'/'X'/'x')
        // считается подтверждением, что оператор на связи - иначе
        //, например, включение тарана прямо перед потерей сигнала
        // могло бы само по себе выглядеть как "давно не было команд".
        lastCommandMs = millis();

        if (isControlCommand(command))
        {
            // Управляющие байты (вооружить/разоружить авто-возврат,
            // включить/выключить таран) - это не команды движения:
            // они не идут в requestedCommand/историю и не трогают
            // моторы напрямую.
            applyControlCommand(command);
            return;
        }

        if (command != requestedCommand)
        {
            // Команда действительно изменилась (а не просто повторно
            // пришла та же самая "липкая" команда от оператора) -
            // фиксируем в истории, сколько прошёл предыдущий отрезок.
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

    void safetyPoll()
    {
        if (requestedCommand == 'F' && safety.isForwardBlocked())
        {
            motors.stop();
        }
    }

    void watchdogPoll()
    {
        unsigned long now = millis();

        if (
            requestedCommand != 'S' &&
            now - lastCommandMs > COMMAND_TIMEOUT_MS
        )
        {
            // Оператор пропал - фиксируем в истории отрезок,
            // который выполнялся в момент потери связи, иначе самый
            // "свежий" (и часто самый длинный) кусок пути никогда не
            // попадёт в историю для автономного возврата.
            recordSegment();

            motors.stop();
            requestedCommand = 'S';
            watchdogStopped = true;
        }
    }

    char getRequestedCommand() const
    {
        return requestedCommand;
    }

    bool wasStoppedByWatchdog() const
    {
        return watchdogStopped;
    }

    // Сколько миллисекунд прошло с последней команды оператора.
    // Используется AutonomousReturn, чтобы определить полную
    // потерю связи (порог заведомо больше моторного watchdog'а) и
    // момент, когда оператор снова на связи.
    unsigned long millisSinceLastCommand() const
    {
        return millis() - lastCommandMs;
    }

    MoveHistory &moveHistory()
    {
        return history;
    }

    // Вызывается, когда оператор снова взял управление после
    // автономного возврата: точка, в которой это произошло, сама
    // становится новой "зоной связи", поэтому старую историю пути
    // имеет смысл забыть и вести её заново с этого места.
    void resetHistoryAfterManualResume()
    {
        history.clear();
    }

    // "Вооружён" ли автономный возврат по сигналу 'A' от оператора -
    // ScoutReturn разрешает себе действовать при потере связи ТОЛЬКО
    // если это true. Если оператор ни разу не нажал кнопку "Вооружить"
    // перед пропажей связи, робот при простое НИЧЕГО не делает сам -
    // просто ждёт (как и раньше делал watchdog), что и исправляет
    // ложное срабатывание "робот стоит, команд нет, но связь есть".
    bool isArmedForReturn() const
    {
        return armedForReturn;
    }

    void armForReturn()
    {
        armedForReturn = true;
    }

    void disarmForReturn()
    {
        armedForReturn = false;
    }

private:
    void recordSegment()
    {
        if (requestedCommand != 'S')
        {
            history.push(requestedCommand, millis() - commandStartMs);
        }
    }

    bool isControlCommand(char command) const
    {
        switch (command)
        {
        case 'A':
        case 'a':
        case 'X':
        case 'x':
            return true;

        default:
            return false;
        }
    }

    void applyControlCommand(char command)
    {
        switch (command)
        {
        case 'A':
            armedForReturn = true;
            break;

        case 'a':
            armedForReturn = false;
            break;

        case 'X':
            safety.setRammingEnabled(true);
            break;

        case 'x':
            safety.setRammingEnabled(false);
            break;
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
        case 'A':
        case 'a':
        case 'X':
        case 'x':
            return true;

        default:
            return false;
        }
    }
};


// ============================================================
// Разведка с гарантированным возвратом при потере связи
// (раньше - AutonomousReturn)
// ============================================================
//
// РАНЬШЕ возврат запускался сам по себе, если оператор просто не
// присылал команд дольше LOST_SIGNAL_MS - это могло ложно
// сработать, если робот стоит на месте и оператор временно не шлёт
// команд, а связь на самом деле есть.
//
// ТЕПЕРЬ манёвр запускается ТОЛЬКО если оператор явно "вооружил"
// его командой 'A' (кнопка в интерфейсе) перед тем, как заехать в
// зону без связи. Если 'A' не присылали - при пропаже команд робот
// просто стоит и ждёт, как и раньше делал обычный watchdog, без
// самостоятельных действий. Это и есть исправление того самого
// ложного срабатывания.
//
// Сам манёвр - не точное повторение пройденного пути задним ходом
// (как было раньше через MoveHistory), а симметричный манёвр
// вперёд-разворот-вперёд:
//   1) PROBE    - проехать ещё немного вперёд ("осмотреть" зону);
//   2) TURN     - развернуться примерно на 180°;
//   3) RETURNING - проехать вперёд обратно, на то же время.
// На всём обратном пути работает передний УЗ-дальномер (в отличие
// от слепого движения задним ходом, у которого своего дальномера
// нет) - это безопаснее.
//
// После завершения манёвра (или если оператор восстановил связь
// посреди манёвра) робот автоматически "разоружается" - на
// следующую зону без связи кнопку нужно нажать заново.
// ============================================================

class ScoutReturn
{
private:
    static const unsigned long LOST_SIGNAL_MS = 3000;

    // "Немного вперёд" - длительность разведывательного участка и
    // участка возврата (симметрично, чтобы робот проехал обратно
    // примерно столько же, сколько проехал в зону).
    static const unsigned long PROBE_FORWARD_MS = 1500;

    // ВАЖНО: ориентировочное значение. Подберите на реальном роботе -
    // засеките секундомером, сколько миллисекунд на скорости
    // DEFAULT_DRIVE_SPEED нужно, чтобы повернуться примерно на 180
    // градусов (команда 'L'), и подставьте сюда.
    static const unsigned long TURN_180_MS = 900;

    enum State : uint8_t
    {
        WAITING,
        PROBE,
        TURN,
        RETURNING
    };

    MotorController &motors;
    SafetyController &safety;
    CommandProtocol &commandProtocol;

    State state;
    unsigned long phaseStartMs;

public:
    ScoutReturn(
        MotorController &motorController,
        SafetyController &safetyController,
        CommandProtocol &protocol
    )
        : motors(motorController),
          safety(safetyController),
          commandProtocol(protocol),
          state(WAITING),
          phaseStartMs(0)
    {
    }

    void poll()
    {
        bool operatorRecentlyActive =
            commandProtocol.millisSinceLastCommand() < LOST_SIGNAL_MS;

        if (state == WAITING)
        {
            if (
                commandProtocol.isArmedForReturn() &&
                !operatorRecentlyActive
            )
            {
                beginProbe();
            }

            return;
        }

        if (operatorRecentlyActive)
        {
            // Оператор снова на связи посреди манёвра.
            // CommandProtocol::poll() в этой же итерации loop() уже
            // применил его свежую команду к моторам - поэтому здесь
            // НЕ трогаем моторы, просто прекращаем манёвр и
            // разоружаем: на следующую зону нужно будет нажать
            // кнопку заново.
            commandProtocol.disarmForReturn();
            state = WAITING;
            return;
        }

        switch (state)
        {
        case PROBE:
            runProbe();
            break;

        case TURN:
            runTurn();
            break;

        case RETURNING:
            runReturning();
            break;

        default:
            break;
        }
    }

    bool isActive() const
    {
        return state != WAITING;
    }

    const char *stateName() const
    {
        switch (state)
        {
        case PROBE:
            return "PROBE";

        case TURN:
            return "TURN";

        case RETURNING:
            return "RETURN";

        default:
            return "TELEOP";
        }
    }

private:
    void beginProbe()
    {
        state = PROBE;
        phaseStartMs = millis();
        driveForward();
    }

    void runProbe()
    {
        bool blocked = safety.isForwardBlocked();

        if (blocked)
        {
            motors.stop();
        }
        else
        {
            driveForward();
        }

        // Упёрлись в препятствие (и таран не включён) или прошло
        // отведённое время - едем дальше по сценарию: цель манёвра -
        // вернуться, а не упрямо ждать, пока освободится путь вперёд.
        if (blocked || millis() - phaseStartMs >= PROBE_FORWARD_MS)
        {
            beginTurn();
        }
    }

    void beginTurn()
    {
        state = TURN;
        phaseStartMs = millis();
        turnInPlace();
    }

    void runTurn()
    {
        if (millis() - phaseStartMs >= TURN_180_MS)
        {
            beginReturning();
        }
    }

    void beginReturning()
    {
        state = RETURNING;
        phaseStartMs = millis();
        driveForward();
    }

    void runReturning()
    {
        if (safety.isForwardBlocked())
        {
            // Путь назад неожиданно перекрыт - стоим и ждём, не
            // тратя впустую время участка, пока не освободится.
            motors.stop();
            return;
        }

        driveForward();

        if (millis() - phaseStartMs >= PROBE_FORWARD_MS)
        {
            finish();
        }
    }

    void finish()
    {
        motors.stop();
        commandProtocol.disarmForReturn();
        state = WAITING;
    }

    void driveForward()
    {
        motors.setLeft(DEFAULT_DRIVE_SPEED);
        motors.setRight(-DEFAULT_DRIVE_SPEED);
    }

    void turnInPlace()
    {
        motors.setLeft(-DEFAULT_DRIVE_SPEED);
        motors.setRight(-DEFAULT_DRIVE_SPEED);
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
ScoutReturn scoutReturn(motors, safetyController, commandProtocol);

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
    Serial.print(commandProtocol.isArmedForReturn());

    Serial.print(",MODE,");
    Serial.print(scoutReturn.stateName());

    Serial.print(",HIST,");
    Serial.print(commandProtocol.moveHistory().size());

    // Опрос TrackingCam на Arduino временно отключён для диагностики.

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

    // К моменту, когда ScoutReturn решает взять управление,
    // CommandProtocol уже перевёл requestedCommand в 'S' (через
    // watchdogPoll), поэтому safetyPoll/watchdogPoll выше больше не
    // трогают моторы и не мешают автономному движению.
    scoutReturn.poll();

    unsigned long now = millis();

    if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
    {
        lastTelemetryMs = now;

        sendTelemetry();
    }
}
