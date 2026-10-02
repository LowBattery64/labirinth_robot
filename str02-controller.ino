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
    // ScoutReturn сверяет его, чтобы понять, что во время кругового
    // осмотра оператор прислал НОВУЮ команду движения и забрал
    // управление на себя.
    unsigned long movementCommandSeq;

    // Одноразовые флаги от кнопки кругового осмотра ('A' - запустить,
    // 'a' - отменить). "Одноразовые" - значит ScoutReturn обязан их
    // "забрать" через consumeCircleRequest()/consumeCancelRequest(),
    // иначе запрос запустится/отменится повторно на следующем poll().
    bool circleRequested;
    bool cancelRequested;

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
          circleRequested(false),
          cancelRequested(false)
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
            // Управляющие байты (запуск/отмена кругового осмотра,
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

    // "Забрать" одноразовый запрос на запуск кругового осмотра
    // (байт 'A'). Возвращает true один раз - при повторном вызове,
    // пока новая 'A' не придёт, вернёт false.
    bool consumeCircleRequest()
    {
        if (!circleRequested)
        {
            return false;
        }

        circleRequested = false;
        return true;
    }

    // "Забрать" одноразовый запрос на отмену кругового осмотра
    // (байт 'a'). Если осмотр сейчас не выполняется, ScoutReturn его
    // просто не вызывает, и флаг остаётся как есть до следующего
    // запуска круга, где он сбрасывается явно (см. beginCircle()).
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
    // нужно перед стартом нового круга, чтобы случайно не зависший
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
            circleRequested = true;
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
// Два независимых автономных манёвра, не мешающих друг другу:
//
//   1) ИСТОРИЯ ХОДА (HISTORY, раньше - AutonomousReturn). Включается
//      АВТОМАТИЧЕСКИ, когда оператор не присылал ни одной команды
//      дольше LOST_SIGNAL_MS (настоящая потеря связи) - старое
//      поведение "вернуться туда, откуда приехали", посегментно
//      отыгрывая MoveHistory в обратном порядке (инвертируя каждую
//      команду: F<->B, L<->R). Прерывается немедленно, как только
//      оператор снова на связи - манёвр не доигрывается до конца,
//      просто отдаёт управление обратно. Точка восстановления связи
//      становится новой "зоной связи" - история пути обнуляется,
//      чтобы при следующей потере связи робот не пытался проехать
//      тот же путь заново.
//
//   2) КРУГОВОЙ ОСМОТР (CIRCLE). Запускается НЕМЕДЛЕННО нажатием
//      кнопки в интерфейсе (байт 'A') - НЕЗАВИСИМО от того, есть
//      сейчас связь с оператором или нет. Робот проезжает полный
//      круг (360°) и возвращается туда, откуда начал манёвр (видео
//      при этом продолжает писаться, как и всегда - запись идёт на
//      Pi независимо от связи с оператором). В отличие от HISTORY,
//      сам факт "оператор на связи" круг НЕ прерывает - оператор мог
//      быть на связи всё это время, в этом и смысл кнопки. Прерывают
//      его только: 1) НОВАЯ команда движения оператора (взял
//      управление на себя) или 2) явная отмена той же кнопкой (байт
//      'a'). Препятствие впереди - не отмена, а пауза: круг стоит и
//      ждёт, иначе робот не вернётся точно в начальную точку.
// ============================================================

class ScoutReturn
{
private:
    static const unsigned long LOST_SIGNAL_MS = 3000;

    // ВАЖНО: ориентировочные значения. Подберите на реальном роботе -
    // при разнице скоростей колёс DEFAULT_DRIVE_SPEED/CIRCLE_INNER_SPEED
    // засеките секундомером, сколько миллисекунд нужно, чтобы робот
    // проехал ровно полный круг (360°) и вернулся в точку старта, и
    // подставьте сюда. CIRCLE_INNER_SPEED задаёт "крутизну" дуги -
    // чем он меньше относительно DEFAULT_DRIVE_SPEED, тем круг уже.
    static const int CIRCLE_INNER_SPEED = DEFAULT_DRIVE_SPEED / 2;
    static const unsigned long CIRCLE_360_MS = 20000;

    enum State : uint8_t
    {
        WAITING,
        CIRCLE,
        HISTORY
    };

    MotorController &motors;
    SafetyController &safety;
    CommandProtocol &commandProtocol;

    State state;
    unsigned long phaseStartMs;

    // Снимок movementCommandSequence() на момент старта круга и
    // накопленное "чистое" (без пауз на препятствии) время круга -
    // используются только в состоянии CIRCLE.
    unsigned long circleStartSeq;
    unsigned long circleElapsedMs;
    unsigned long lastCirclePollMs;

    // Текущий отыгрываемый (уже инвертированный) отрезок истории
    // хода и его длительность - используются только в состоянии
    // HISTORY.
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
          circleStartSeq(0),
          circleElapsedMs(0),
          lastCirclePollMs(0),
          historyCommand('S'),
          historySegmentMs(0)
    {
    }

    void poll()
    {
        if (state == CIRCLE)
        {
            if (commandProtocol.movementCommandSequence() != circleStartSeq)
            {
                // Оператор прислал НОВУЮ команду движения - в этой же
                // итерации loop() CommandProtocol::poll() уже применил
                // её к моторам, поэтому здесь моторы не трогаем, просто
                // прекращаем манёвр.
                state = WAITING;
                return;
            }

            if (commandProtocol.consumeCancelRequest())
            {
                // Явная отмена кнопкой - никто управление не
                // перехватывал, поэтому останавливаем моторы сами.
                motors.stop();
                state = WAITING;
                return;
            }

            runCircle();
            return;
        }

        bool operatorRecentlyActive =
            commandProtocol.millisSinceLastCommand() < LOST_SIGNAL_MS;

        if (state == WAITING)
        {
            if (commandProtocol.consumeCircleRequest())
            {
                // Круговой осмотр - НЕЗАВИСИМо от связи с оператором.
                beginCircle();
                return;
            }

            if (!operatorRecentlyActive && !commandProtocol.moveHistory().isEmpty())
            {
                beginHistory();
            }

            return;
        }

        // state == HISTORY
        if (operatorRecentlyActive)
        {
            // Оператор снова на связи посреди манёвра. Моторы здесь не
            // трогаем по той же причине, что и для CIRCLE выше. Точка
            // восстановления связи - новая "зона связи", поэтому
            // стираем историю пути.
            commandProtocol.resetHistoryAfterManualResume();
            state = WAITING;
            return;
        }

        runHistory();
    }

    bool isActive() const
    {
        return state != WAITING;
    }

    // Используется только для телеметрии (поле ARMED) - чтобы кнопка в
    // интерфейсе оставалась "нажатой" ровно пока круг реально выполняется,
    // и сама отпускалась по завершении/отмене/перехвату, без каких-либо
    // изменений в самом интерфейсе.
    bool isCircleActive() const
    {
        return state == CIRCLE;
    }

    const char *stateName() const
    {
        switch (state)
        {
        case CIRCLE:
            return "CIRCLE";

        case HISTORY:
            return "HISTORY";

        default:
            return "TELEOP";
        }
    }

private:
    void beginCircle()
    {
        // Сбрасываем возможную "зависшую" отмену с прошлого круга,
        // чтобы она не оборвала этот немедленно.
        commandProtocol.discardPendingCancelRequest();
        commandProtocol.forceStopForAutonomy();

        circleStartSeq = commandProtocol.movementCommandSequence();
        circleElapsedMs = 0;
        lastCirclePollMs = millis();

        state = CIRCLE;
        driveCircle();
    }

    void runCircle()
    {
        unsigned long now = millis();
        unsigned long delta = now - lastCirclePollMs;
        lastCirclePollMs = now;

        if (safety.isForwardBlocked())
        {
            // Пауза, а не отмена - время, проведённое в ожидании,
            // не засчитывается в пройденный круг, иначе манёвр
            // закончится раньше, чем робот реально проедет круг.
            motors.stop();
        }
        else
        {
            driveCircle();
            circleElapsedMs += delta;
        }

        if (circleElapsedMs >= CIRCLE_360_MS)
        {
            finishCircle();
        }
    }

    void finishCircle()
    {
        motors.stop();
        state = WAITING;
    }

    void driveCircle()
    {
        // Тот же знак, что и у обычного "вперёд" (см. driveHistoryCommand
        // для 'F'), но с разными скоростями колёс - вместо прямой робот
        // едет по дуге и за CIRCLE_360_MS успевает описать полный круг.
        motors.setLeft(DEFAULT_DRIVE_SPEED);
        motors.setRight(-CIRCLE_INNER_SPEED);
    }

    // Снимает с истории хода самый свежий отрезок (уже инвертированный -
    // F<->B, L<->R), начинает его отыгрывать. Возвращает false, если
    // история уже пуста - тогда возврат по истории завершён/невозможен.
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
            // История опустела ровно к моменту потери связи -
            // отыгрывать нечего, остаёмся в ожидании.
            return;
        }

        state = HISTORY;
    }

    void runHistory()
    {
        // Едущий вперёд инвертированный отрезок ('F') уважает
        // УЗ-защиту/таран как обычное движение. Остальные инверсии
        // ('B'/'L'/'R') - как и раньше, без переднего дальномера.
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
    // больше нет (кнопка запускает круг немедленно), но само поле по-
    // прежнему "true", пока круговой осмотр реально выполняется - это и
    // держит кнопку в интерфейсе нажатой ровно на время манёвра.
    Serial.print(",ARMED,");
    Serial.print(scoutReturn.isCircleActive());

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
