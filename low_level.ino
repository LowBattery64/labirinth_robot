// ============================================================
// OmegaBot — минимальное управление моторами
// ============================================================

// Левый мотор
#define LEFT_PWM  6
#define LEFT_DIR  7

// Правый мотор
#define RIGHT_PWM 5
#define RIGHT_DIR 4

// Скорость движения
#define MOTOR_SPEED 120

// Если команда не приходит дольше этого времени,
// робот останавливается
#define COMMAND_TIMEOUT 500

unsigned long lastCommandTime = 0;


// ------------------------------------------------------------
// Управление моторами
// ------------------------------------------------------------

void drive(int left, int right)
{
    // Левый мотор
    if (left >= 0)
    {
        digitalWrite(LEFT_DIR, HIGH);
    }
    else
    {
        digitalWrite(LEFT_DIR, LOW);
    }

    analogWrite(LEFT_PWM, abs(left));


    // Правый мотор
    if (right >= 0)
    {
        digitalWrite(RIGHT_DIR, HIGH);
    }
    else
    {
        digitalWrite(RIGHT_DIR, LOW);
    }

    analogWrite(RIGHT_PWM, abs(right));
}


// ------------------------------------------------------------
// Остановка
// ------------------------------------------------------------

void stopMotors()
{
    drive(0, 0);
}


// ------------------------------------------------------------
// Настройка
// ------------------------------------------------------------

void setup()
{
    pinMode(LEFT_PWM, OUTPUT);
    pinMode(LEFT_DIR, OUTPUT);

    pinMode(RIGHT_PWM, OUTPUT);
    pinMode(RIGHT_DIR, OUTPUT);

    Serial.begin(115200);

    stopMotors();

    lastCommandTime = millis();
}


// ------------------------------------------------------------
// Основной цикл
// ------------------------------------------------------------

void loop()
{
    // Получили команду
    if (Serial.available() > 0)
    {
        char command = Serial.read();

        switch (command)
        {
            case 'F':
                // Вперёд
                drive(MOTOR_SPEED, MOTOR_SPEED);
                break;

            case 'B':
                // Назад
                drive(-MOTOR_SPEED, -MOTOR_SPEED);
                break;

            case 'L':
                // Поворот налево
                drive(-MOTOR_SPEED, MOTOR_SPEED);
                break;

            case 'R':
                // Поворот направо
                drive(MOTOR_SPEED, -MOTOR_SPEED);
                break;

            case 'S':
                // Стоп
                stopMotors();
                break;

            default:
                // Неизвестная команда
                break;
        }

        // Обновляем время последней команды
        lastCommandTime = millis();
    }


    // --------------------------------------------------------
    // Watchdog
    // --------------------------------------------------------

    if (millis() - lastCommandTime > COMMAND_TIMEOUT)
    {
        stopMotors();
    }
}