#include <Arduino.h>
#include <Stepper.h>

#include <Stepper.h>

// change this to the number of steps on your motor
#define STEPS 100

// the previous reading from the analog input
int previous = 0;

struct Motor{
    int Button1;
    int Button2;
    int Pins[4] = {0};
    Stepper stepper = Stepper(STEPS, 0, 0, 0, 0);
    void init() {
        pinMode(Button1, INPUT);
        pinMode(Button2, INPUT);

        stepper = Stepper(STEPS, Pins[0], Pins[1], Pins[2], Pins[3]);
        stepper.setSpeed(30); // set the speed of the motor to 30 RPMs
    }
};
Motor motors[3];

void setup() {
    Serial.begin(115200);

    motors[0].Button1 = A1;
    motors[0].Button2 = A0;
    motors[0].Pins[0] = A2;
    motors[0].Pins[1] = A3;
    motors[0].Pins[2] = A4;
    motors[0].Pins[3] = A5;
    motors[0].init();

    motors[1].Button1 = 12;
    motors[1].Button2 = 13;
    motors[1].Pins[0] = 11;
    motors[1].Pins[1] = 10;
    motors[1].Pins[2] = 9;
    motors[1].Pins[3] = 8;
    motors[1].init();

    motors[2].Button1 = 6;
    motors[2].Button2 = 7;
    motors[2].Pins[0] = 5;
    motors[2].Pins[1] = 4;
    motors[2].Pins[2] = 3;
    motors[2].Pins[3] = 2;
    motors[2].init();
}

void loop() {
    int increment = 1;
    for (auto & motor : motors) {
        if (motor.Pins[0] == 0)
            continue;

        if (digitalRead(motor.Button1))
            motor.stepper.step(increment);

        if (digitalRead(motor.Button2))
            motor.stepper.step(-increment);

        if (digitalRead(motor.Button1) || digitalRead(motor.Button2))
            Serial.println("Btn: " + String(digitalRead(motor.Button1)) + " | " + String(digitalRead(motor.Button2)));
    }
}