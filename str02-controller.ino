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
// Команды управления
// ============================================================

class CommandProtocol
{
private:
    static const int DEFAULT_SPEED = 80;

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
        // Движение вперёд
        case 'F':
            motors.setLeft(DEFAULT_SPEED);
            motors.setRight(-DEFAULT_SPEED);
            break;

        // Движение назад
        case 'B':
            motors.setLeft(-DEFAULT_SPEED);
            motors.setRight(DEFAULT_SPEED);
            break;

        // Поворот налево
        case 'L':
            motors.setLeft(-DEFAULT_SPEED);
            motors.setRight(-DEFAULT_SPEED);
            break;

        // Поворот направо
        case 'R':
            motors.setLeft(DEFAULT_SPEED);
            motors.setRight(DEFAULT_SPEED);
            break;

        // Остановка
        case 'S':
            motors.stop();
            break;

        default:
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
CommandProtocol commandProtocol(motors);

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

    // Опрос TrackingCam временно отключён для диагностики.

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

    camera.begin();
}


// ============================================================
// Основной цикл
// ============================================================

void loop()
{
    commandProtocol.poll();

    speedSensors.poll();

    unsigned long now = millis();

    if (now - lastTelemetryMs >= TELEMETRY_PERIOD_MS)
    {
        lastTelemetryMs = now;

        sendTelemetry();
    }
}
