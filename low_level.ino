// ============================================================
// OmegaBot — управление моторами и приём команд
// ============================================================

class MotorController
{
private:
    const uint8_t leftMotorPwmPin;
    const uint8_t leftMotorDirectionPin;

    const uint8_t rightMotorPwmPin;
    const uint8_t rightMotorDirectionPin;

public:
    MotorController(
        uint8_t leftPwmPin,
        uint8_t leftDirectionPin,
        uint8_t rightPwmPin,
        uint8_t rightDirectionPin
    )
        : leftMotorPwmPin(leftPwmPin),
          leftMotorDirectionPin(leftDirectionPin),
          rightMotorPwmPin(rightPwmPin),
          rightMotorDirectionPin(rightDirectionPin)
    {
    }

    void initialize()
    {
        pinMode(leftMotorPwmPin, OUTPUT);
        pinMode(leftMotorDirectionPin, OUTPUT);

        pinMode(rightMotorPwmPin, OUTPUT);
        pinMode(rightMotorDirectionPin, OUTPUT);

        stop();
    }

    void drive(int leftMotorSpeed, int rightMotorSpeed)
    {
        setMotorSpeed(
            leftMotorDirectionPin,
            leftMotorPwmPin,
            leftMotorSpeed
        );

        setMotorSpeed(
            rightMotorDirectionPin,
            rightMotorPwmPin,
            rightMotorSpeed
        );
    }

    void stop()
    {
        drive(0, 0);
    }

private:
    void setMotorSpeed(
        uint8_t directionPin,
        uint8_t pwmPin,
        int motorSpeed
    )
    {
        digitalWrite(
            directionPin,
            motorSpeed >= 0 ? HIGH : LOW
        );

        analogWrite(
            pwmPin,
            abs(motorSpeed)
        );
    }
};


class CommandController
{
private:
    const int movementSpeed;
    MotorController& motorController;

public:
    CommandController(
        MotorController& motors,
        int defaultMovementSpeed
    )
        : movementSpeed(defaultMovementSpeed),
          motorController(motors)
    {
    }

    bool processCommand(char command)
    {
        switch (command)
        {
            case 'F':
                moveForward();
                return true;

            case 'B':
                moveBackward();
                return true;

            case 'L':
                turnLeft();
                return true;

            case 'R':
                turnRight();
                return true;

            case 'S':
                stopRobot();
                return true;

            default:
                return false;
        }
    }

private:
    void moveForward()
    {
        motorController.drive(
            movementSpeed,
            movementSpeed
        );
    }

    void moveBackward()
    {
        motorController.drive(
            -movementSpeed,
            -movementSpeed
        );
    }

    void turnLeft()
    {
        motorController.drive(
            -movementSpeed,
            movementSpeed
        );
    }

    void turnRight()
    {
        motorController.drive(
            movementSpeed,
            -movementSpeed
        );
    }

    void stopRobot()
    {
        motorController.stop();
    }
};


class SafetyController
{
private:
    const unsigned long commandTimeoutMilliseconds;

    unsigned long lastCommandTimeMilliseconds;

    MotorController& motorController;

public:
    SafetyController(
        MotorController& motors,
        unsigned long timeoutMilliseconds
    )
        : commandTimeoutMilliseconds(timeoutMilliseconds),
          lastCommandTimeMilliseconds(0),
          motorController(motors)
    {
    }

    void initialize()
    {
        lastCommandTimeMilliseconds = millis();
    }

    void registerCommand()
    {
        lastCommandTimeMilliseconds = millis();
    }

    void update()
    {
        if (
            millis() - lastCommandTimeMilliseconds >
            commandTimeoutMilliseconds
        )
        {
            motorController.stop();
        }
    }
};


class RobotController
{
private:
    // Аппаратная конфигурация OmegaBot.
    static const uint8_t LEFT_MOTOR_PWM_PIN = 6;
    static const uint8_t LEFT_MOTOR_DIRECTION_PIN = 7;

    static const uint8_t RIGHT_MOTOR_PWM_PIN = 5;
    static const uint8_t RIGHT_MOTOR_DIRECTION_PIN = 4;

    static const int DEFAULT_MOVEMENT_SPEED = 120;

    // При потере команд робот должен остановиться автоматически.
    static const unsigned long COMMAND_TIMEOUT_MILLISECONDS = 500;

    MotorController motorController;

    CommandController commandController;

    SafetyController safetyController;

public:
    RobotController()
        : motorController(
            LEFT_MOTOR_PWM_PIN,
            LEFT_MOTOR_DIRECTION_PIN,
            RIGHT_MOTOR_PWM_PIN,
            RIGHT_MOTOR_DIRECTION_PIN
        ),
          commandController(
            motorController,
            DEFAULT_MOVEMENT_SPEED
        ),
          safetyController(
            motorController,
            COMMAND_TIMEOUT_MILLISECONDS
        )
    {
    }

    void initialize()
    {
        motorController.initialize();

        Serial.begin(115200);

        safetyController.initialize();
    }

    void update()
    {
        processCommands();

        safetyController.update();
    }

private:
    void processCommands()
    {
        while (Serial.available() > 0)
        {
            char receivedCommand = Serial.read();

            if (
                commandController.processCommand(
                    receivedCommand
                )
            )
            {
                safetyController.registerCommand();
            }
        }
    }
};


RobotController robotController;


void setup()
{
    robotController.initialize();
}


void loop()
{
    robotController.update();
}
