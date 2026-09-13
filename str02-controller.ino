// ============================================================
// OmegaBot — контроллер робота на плате ARP-DEK-STR-02
// ------------------------------------------------------------
// Заменяет старую пару "low_level.ino (Arduino) + labirinth_server.cpp
// (Raspberry Pi -> отдельный Arduino по serial)" на новой аппаратной
// платформе: здесь ВСЁ (моторы, датчики, камера) висит на одной
// Mega-совместимой плате. Raspberry Pi по-прежнему шлёт команды
// движения и читает телеметрию, но напрямую по USB (нативный Serial),
// без отдельного Arduino посередине.
//
// ============================================================

#include "TrackingCamDxlUart.h"


// ============================================================
// Распиновка робота
// ============================================================

class Pins
{
public:
    // Моторы (из методички для этой же платы)
    static const uint8_t M1_DIR = 45;
    static const uint8_t M1_SPEED = 44;
    static const uint8_t M2_DIR = 47;
    static const uint8_t M2_SPEED = 46;

    // ИК-датчики препятствий
    static const uint8_t IR_CENTER = 0; // см. предупреждение выше
    static const uint8_t IR_LEFT = 1;   // см. предупреждение выше
    static const uint8_t IR_RIGHT = 2;

    // УЗ-дальномеры (Trig, Echo)
    static const uint8_t US_CENTER_TRIG = 3;
    static const uint8_t US_CENTER_ECHO = 4;
    static const uint8_t US_LEFT_TRIG = 5;
    static const uint8_t US_LEFT_ECHO = 6;
    static const uint8_t US_RIGHT_TRIG = 7;
    static const uint8_t US_RIGHT_ECHO = 8;

    // см. класс SpeedSensorArray.
    static const uint8_t ENCODER_LEFT = 9;
    static const uint8_t ENCODER_RIGHT = 10;
};


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

    // speed: -255..255 (знак задаёт направление)
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
// ИК-датчики препятствий
// ============================================================

class IRSensorArray
{
public:
    void begin()
    {
        pinMode(Pins::IR_CENTER, INPUT);
        pinMode(Pins::IR_LEFT, INPUT);
        pinMode(Pins::IR_RIGHT, INPUT);
    }

    bool centerBlocked() const { return digitalRead(Pins::IR_CENTER) == HIGH; }
    bool leftBlocked() const { return digitalRead(Pins::IR_LEFT) == HIGH; }
    bool rightBlocked() const { return digitalRead(Pins::IR_RIGHT) == HIGH; }
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
        : trigPin(trig), echoPin(echo)
    {
    }

    void begin()
    {
        pinMode(trigPin, OUTPUT);
        pinMode(echoPin, INPUT);
    }

    // Возвращает расстояние в см, либо -1, если эхо не пришло
    // (объект вне диапазона).
    long readCm() const
    {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);

        // Таймаут ~20 мс -> максимум примерно 3-4 м дальности.
        unsigned long duration = pulseIn(echoPin, HIGH, 20000UL);

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

    // Читаем по очереди с небольшой паузой, чтобы эхо одного
    // датчика не поймал соседний (см. методичку, раздел про УЗ).
    void readAll(long &centerCm, long &leftCm, long &rightCm) const
    {
        centerCm = center.readCm();
        delay(10);
        leftCm = left.readCm();
        delay(10);
        rightCm = right.readCm();
    }
};


// ============================================================
// Датчики скорости колёс (энкодеры), опрос без прерываний
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
        : lastLeftState(false), lastRightState(false),
          leftPulses(0), rightPulses(0)
    {
    }

    void begin()
    {
        pinMode(Pins::ENCODER_LEFT, INPUT);
        pinMode(Pins::ENCODER_RIGHT, INPUT);
        lastLeftState = digitalRead(Pins::ENCODER_LEFT) == HIGH;
        lastRightState = digitalRead(Pins::ENCODER_RIGHT) == HIGH;
    }

    // Вызывать как можно чаще из loop() — ловим фронты вручную,
    // т.к. аппаратные прерывания на этих пинах на ATmega2560 недоступны.
    void poll()
    {
        bool leftState = digitalRead(Pins::ENCODER_LEFT) == HIGH;
        if (leftState && !lastLeftState)
        {
            leftPulses++;
        }
        lastLeftState = leftState;

        bool rightState = digitalRead(Pins::ENCODER_RIGHT) == HIGH;
        if (rightState && !lastRightState)
        {
            rightPulses++;
        }
        lastRightState = rightState;
    }

    unsigned long leftPulseCount() const { return leftPulses; }
    unsigned long rightPulseCount() const { return rightPulses; }

    void resetCounters()
    {
        leftPulses = 0;
        rightPulses = 0;
    }
};


// ============================================================
// Модуль технического зрения TrackingCam
// ============================================================

class CameraTracker
{
private:
    // Физически на плате разведён только один разъём UART под камеру.
    // Какой это номер аппаратного UART внутри — неизвестно заранее,
    // подбирается опытным путём (1 -> 2 -> 3), пока камера не откликнется.
    static const uint8_t CAM_ID = 51;
    static const uint8_t SERIAL_PORT = 1;
    static const long CAM_BAUDRATE = 115200;
    static const long PC_BAUDRATE = 115200;
    static const uint16_t TIMEOUT_MS = 30;

    TrackingCamDxlUart cam;

public:
    void begin()
    {
        cam.TrackingCamDxlUartInit(CAM_ID, SERIAL_PORT, CAM_BAUDRATE, PC_BAUDRATE, TIMEOUT_MS);
    }

    // Составные (многоцветные) объекты — то, что скорее всего нужно
    // для жёлтых меток-стикеров из требований проекта.
    uint8_t readObjects()
    {
        return cam.TrackingCamDxl_ReadObjects();
    }

    // Однотонные области — на случай, если метки окажутся простым
    // одноцветным пятном, а не композитным маркером.
    uint8_t readBlobs()
    {
        return cam.TrackingCamDxl_ReadObjects() == 0 ? cam.TrackingCamDxl_ReadBlobs() : 0;
    }

    bool hasObject() const { return cam.obj[0].obj_size > 0; }
    int objectX() const { return cam.obj[0].cx; }
    int objectY() const { return cam.obj[0].cy; }
};


// ============================================================
// Простой протокол управления по Serial (от Raspberry Pi)
// ------------------------------------------------------------
// 'F' — вперёд, 'B' — назад, 'L' — влево, 'R' — вправо, 'S' — стоп.
// Тот же однобайтовый протокол, что использовался в старой схеме
// (labirinth_server.cpp -> Arduino), для совместимости с уже
// написанным серверным кодом на Pi.
// ============================================================

class CommandProtocol
{
private:
    static const int DEFAULT_SPEED = 200;
    MotorController &motors;

public:
    explicit CommandProtocol(MotorController &motorController)
        : motors(motorController)
    {
    }

    void poll()
    {
        if (!Serial.available())
        {
            return;
        }

        char command = Serial.read();

        switch (command)
        {
        case 'F':
            motors.setLeft(DEFAULT_SPEED);
            motors.setRight(DEFAULT_SPEED);
            break;
        case 'B':
            motors.setLeft(-DEFAULT_SPEED);
            motors.setRight(-DEFAULT_SPEED);
            break;
        case 'L':
            motors.setLeft(-DEFAULT_SPEED);
            motors.setRight(DEFAULT_SPEED);
            break;
        case 'R':
            motors.setLeft(DEFAULT_SPEED);
            motors.setRight(-DEFAULT_SPEED);
            break;
        case 'S':
            motors.stop();
            break;
        default:
            // Неизвестная команда - игнорируем, не трогаем моторы.
            break;
        }
    }
};


// ============================================================
// Точка входа
// ============================================================

MotorController motors;
IRSensorArray irSensors;
UltrasonicArray ultrasonicSensors;
SpeedSensorArray speedSensors;
CameraTracker camera;
CommandProtocol commandProtocol(motors);

unsigned long lastTelemetryMs = 0;
const unsigned long TELEMETRY_PERIOD_MS = 200;

void sendTelemetry()
{
    long usCenter, usLeft, usRight;
    ultrasonicSensors.readAll(usCenter, usLeft, usRight);

    Serial.print("T,IR,");
    Serial.print(irSensors.centerBlocked());
    Serial.print(',');
    Serial.print(irSensors.leftBlocked());
    Serial.print(',');
    Serial.print(irSensors.rightBlocked());

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

    uint8_t objects = camera.readObjects();
    Serial.print(",OBJ,");
    Serial.print(objects);
    if (objects > 0)
    {
        Serial.print(',');
        Serial.print(camera.objectX());
        Serial.print(',');
        Serial.print(camera.objectY());
    }

    Serial.println();
}

void setup()
{
    // PC_BaudRate у камеры и скорость Serial к Raspberry Pi должны совпадать.
    Serial.begin(115200);

    motors.begin();
    irSensors.begin();
    ultrasonicSensors.begin();
    speedSensors.begin();
    camera.begin();
}

void loop()
{
    commandProtocol.poll();

    // Опрашиваем энкодеры максимально часто - помним, что pulseIn()
    // внутри sendTelemetry() блокирует выполнение на время УЗ-замера,
    // так что быстрые импульсы между вызовами loop() всё равно можно
    // пропустить. Для более точной одометрии в будущем стоит либо
    // разнести УЗ-опрос по времени (например, по одному датчику за цикл),
    // либо перенести правый энкодер (D10) на аппаратное прерывание PCINT.
    speedSensors.poll();

    unsigned long now = millis();
    if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
    {
        lastTelemetryMs = now;
        sendTelemetry();
    }
}
