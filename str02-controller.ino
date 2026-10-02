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

    // Счётчик НАСТОЯЩИХ смен команды движения оператором (растёт
    // только в ветке "command != requestedCommand" ниже - то есть ни
    // повторная присылка той же "липкой" команды, ни автоостановка
    // по watchdog'у, ни forceStopForAutonomy() его не трогают).
    // ScoutReturn сверяет его, чтобы понять, что во время миссияового
    // осмотра оператор прислал НОВУЮ команду движения и забрал
    // управление на себя.
    unsigned long movementCommandSeq;

    // Одноразовые флаги от кнопки автономной миссии обследования ('A' - запустить,
    // 'a' - отменить). "Одноразовые" - значит ScoutReturn обязан их
    // "забрать" через consumeInspectionRequest()/consumeCancelRequest(),
    // иначе запрос запустится/отменится повторно на следующем poll().
    bool inspectionRequested;
    bool cancelRequested;
    bool autonomyActive;

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
          movementCommandSeq(0),
          inspectionRequested(false),
          cancelRequested(false),
          autonomyActive(false)
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
            // Управляющие байты (запуск/отмена автономной миссии обследования,
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
            // фиксируем в истории, сколько прошёл предыдущий отрезок,
            // и считаем это "настоящей" сменой команды.
            recordSegment();

            commandStartMs = millis();
            requestedCommand = command;
            ++movementCommandSeq;
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
            !autonomyActive &&
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

    // Текущее значение счётчика настоящих смен команды движения -
    // см. комментарий у поля movementCommandSeq.
    unsigned long movementCommandSequence() const
    {
        return movementCommandSeq;
    }

    // "Забрать" одноразовый запрос на запуск автономной миссии обследования
    // (байт 'A'). Возвращает true один раз - при повторном вызове,
    // пока новая 'A' не придёт, вернёт false.
    bool consumeInspectionRequest()
    {
        if (!inspectionRequested)
        {
            return false;
        }

        inspectionRequested = false;
        return true;
    }

    // "Забрать" одноразовый запрос на отмену автономной миссии обследования
    // (байт 'a'). Если осмотр сейчас не выполняется, ScoutReturn его
    // просто не вызывает, и флаг остаётся как есть до следующего
    // запуска миссии, где он сбрасывается явно (см. beginCircle()).
    bool consumeCancelRequest()
    {
        if (!cancelRequested)
        {
            return false;
        }

        cancelRequested = false;
        return true;
    }

    // Сбросить флаг отмены, не "забирая" его как настоящую отмену -
    // нужно перед стартом нового миссии, чтобы случайно не зависший
    // с прошлого раза флаг не оборвал его немедленно.
    void discardPendingCancelRequest()
    {
        cancelRequested = false;
    }

    // Принудительно остановить моторы и перевести requestedCommand в
    // 'S' от имени автономного манёвра (ScoutReturn), НЕ фиксируя
    // сегмент в MoveHistory - в отличие от watchdog'а, это не
    // "оператор пропал", а "начинается другой манёвр", и прошлый
    // недоезд оператора в историю попадать не должен.
    void forceStopForAutonomy()
    {
        requestedCommand = 'S';
        motors.stop();
    }

    void setAutonomyActive(bool active)
    {
        autonomyActive = active;
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
            if (!autonomyActive)
            {
                inspectionRequested = true;
            }
            break;

        case 'a':
            cancelRequested = true;
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
// Автономное поведение
// ============================================================
//
// Есть два независимых режима:
//
//   1) HISTORY - старый автоматический возврат по истории движения после
//      потери связи. Он сохраняется для совместимости с предыдущим
//      поведением робота.
//
//   2) INSPECTION - явная команда A из интерфейса. Робот проезжает
//      примерно 10 см вперёд, разворачивается примерно на 180 градусов
//      на месте и возвращается примерно на те же 10 см. Потеря связи
//      во время этой миссии не останавливает робота: она ожидаема.
//      Переднее движение при этом по-прежнему ограничивается УЗ-защитой.
//      Команда a отменяет миссию. Новая команда движения оператора также
//      немедленно возвращает управление оператору.
// ============================================================

class ScoutReturn
{
private:
    static const unsigned long LOST_SIGNAL_MS = 3000;

    // Ориентировочная последовательность обследования тупика.
    // Это не точные значения расстояния/угла: движение выполняется
    // ограниченное время, а передний УЗ-датчик при необходимости
    // остановит робота раньше.
    static const unsigned long INSPECTION_FORWARD_MS = 700;
    static const unsigned long INSPECTION_TURN_MS = 950;
    static const unsigned long INSPECTION_BACK_MS = 700;

    enum State : uint8_t
    {
        WAITING,
        INSPECTION_FORWARD,
        INSPECTION_TURN,
        INSPECTION_BACK,
        HISTORY
    };

    MotorController &motors;
    SafetyController &safety;
    CommandProtocol &commandProtocol;

    State state;
    unsigned long phaseStartMs;
    unsigned long inspectionStartSeq;

    // Текущий отыгрываемый (инвертированный) отрезок истории
    // хода и его длительность - используются только в HISTORY.
    char historyCommand;
    unsigned long historySegmentMs;

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
          phaseStartMs(0),
          inspectionStartSeq(0),
          historyCommand('S'),
          historySegmentMs(0)
    {
    }

    void poll()
    {
        if (state == INSPECTION_FORWARD ||
            state == INSPECTION_TURN ||
            state == INSPECTION_BACK)
        {
            if (commandProtocol.movementCommandSequence() != inspectionStartSeq)
            {
                // Новая команда движения от оператора означает, что он
                // забрал управление. Команда уже применена в
                // CommandProtocol::poll(), поэтому здесь просто
                // завершаем автономную миссию и не трогаем моторы.
                finishInspection(false);
                return;
            }

            if (commandProtocol.consumeCancelRequest())
            {
                finishInspection(true);
                return;
            }

            runInspection();
            return;
        }

        bool operatorRecentlyActive =
            commandProtocol.millisSinceLastCommand() < LOST_SIGNAL_MS;

        if (state == WAITING)
        {
            if (commandProtocol.consumeInspectionRequest())
            {
                beginInspection();
                return;
            }

            // Старое автоматическое возвращение по истории сохраняется.
            // Оно срабатывает только при настоящей потере связи.
            if (!operatorRecentlyActive && !commandProtocol.moveHistory().isEmpty())
            {
                beginHistory();
            }

            return;
        }

        // state == HISTORY
        if (operatorRecentlyActive)
        {
            // Оператор снова на связи: прекращаем автономный возврат и
            // начинаем новую историю движения с текущей точки.
            commandProtocol.resetHistoryAfterManualResume();
            commandProtocol.setAutonomyActive(false);
            state = WAITING;
            return;
        }

        runHistory();
    }

    bool isActive() const
    {
        return state != WAITING;
    }

    bool isInspectionActive() const
    {
        return state == INSPECTION_FORWARD ||
               state == INSPECTION_TURN ||
               state == INSPECTION_BACK;
    }

    const char *stateName() const
    {
        switch (state)
        {
        case INSPECTION_FORWARD:
            return "INSPECT_IN";

        case INSPECTION_TURN:
            return "INSPECT_TURN";

        case INSPECTION_BACK:
            return "INSPECT_OUT";

        case HISTORY:
            return "HISTORY";

        default:
            return "TELEOP";
        }
    }

private:
    void beginInspection()
    {
        // Команда A сама по себе считается командой оператора, поэтому
        // watchdog уже получил свежий timestamp. После старта миссии
        // watchdog временно не должен останавливать робота: потеря связи
        // внутри тупика является ожидаемой частью задания.
        commandProtocol.discardPendingCancelRequest();
        commandProtocol.forceStopForAutonomy();
        commandProtocol.setAutonomyActive(true);
        inspectionStartSeq = commandProtocol.movementCommandSequence();

        phaseStartMs = millis();
        state = INSPECTION_FORWARD;

        driveForward();
    }

    void runInspection()
    {
        switch (state)
        {
        case INSPECTION_FORWARD:
            runForwardPhase();
            break;

        case INSPECTION_TURN:
            runTurnPhase();
            break;

        case INSPECTION_BACK:
            runBackPhase();
            break;

        default:
            break;
        }
    }

    void runForwardPhase()
    {
        // Если перед роботом препятствие, УЗ-защита останавливает его.
        // Фаза не завершается, пока препятствие не исчезнет или не
        // закончится отведённое время.
        if (safety.isForwardBlocked())
        {
            motors.stop();
        }
        else
        {
            driveForward();
        }

        if (millis() - phaseStartMs >= INSPECTION_FORWARD_MS)
        {
            motors.stop();
            phaseStartMs = millis();
            state = INSPECTION_TURN;
            driveTurn();
        }
    }

    void runTurnPhase()
    {
        // Разворот выполняется на месте. Передний УЗ-датчик здесь не
        // блокирует движение: робот не едет вперёд, а разворачивается.
        driveTurn();

        if (millis() - phaseStartMs >= INSPECTION_TURN_MS)
        {
            motors.stop();
            phaseStartMs = millis();
            state = INSPECTION_BACK;
            driveBackward();
        }
    }

    void runBackPhase()
    {
        driveBackward();

        if (millis() - phaseStartMs >= INSPECTION_BACK_MS)
        {
            finishInspection(true);
        }
    }

    void finishInspection(bool stopMotors)
    {
        if (stopMotors)
        {
            motors.stop();
        }

        commandProtocol.setAutonomyActive(false);
        commandProtocol.resetHistoryAfterManualResume();
        state = WAITING;
    }

    void driveForward()
    {
        motors.setLeft(DEFAULT_DRIVE_SPEED);
        motors.setRight(-DEFAULT_DRIVE_SPEED);
    }

    void driveBackward()
    {
        motors.setLeft(-DEFAULT_DRIVE_SPEED);
        motors.setRight(DEFAULT_DRIVE_SPEED);
    }

    void driveTurn()
    {
        // Обе стороны вращаются в одну сторону относительно колёс,
        // поэтому робот разворачивается примерно на месте.
        motors.setLeft(DEFAULT_DRIVE_SPEED);
        motors.setRight(DEFAULT_DRIVE_SPEED);
    }

    bool popNextHistorySegment()
    {
        char command;
        unsigned long durationMs;

        if (!commandProtocol.moveHistory().popInverse(command, durationMs))
        {
            return false;
        }

        historyCommand = command;
        historySegmentMs = durationMs;
        phaseStartMs = millis();
        driveHistoryCommand();

        return true;
    }

    void beginHistory()
    {
        if (!popNextHistorySegment())
        {
            return;
        }

        commandProtocol.setAutonomyActive(true);
        state = HISTORY;
    }

    void runHistory()
    {
        if (historyCommand == 'F' && safety.isForwardBlocked())
        {
            motors.stop();
        }
        else
        {
            driveHistoryCommand();
        }

        if (millis() - phaseStartMs >= historySegmentMs)
        {
            if (!popNextHistorySegment())
            {
                motors.stop();
                commandProtocol.setAutonomyActive(false);
                state = WAITING;
            }
        }
    }

    void driveHistoryCommand()
    {
        switch (historyCommand)
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

    // Поле ARMED сохранено ради совместимости с интерфейсом (который мы
    // по просьбе Юли не трогаем): смысла "вооружён и ждёт потери связи"
    // больше нет (кнопка запускает автономная миссия немедленно), но само поле по-
    // прежнему "true", пока автономная миссия обследования реально выполняется - это и
    // держит кнопку в интерфейсе нажатой ровно на время манёвра.
    Serial.print(",ARMED,");
    Serial.print(scoutReturn.isInspectionActive());

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
