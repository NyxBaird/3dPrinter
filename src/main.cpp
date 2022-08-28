#include <Arduino.h>
#include <WiFi.h>
#include <Stepper.h>
#include <IRremote.hpp>
#include <LiquidCrystal_I2C.h>

//Set our display properties
LiquidCrystal_I2C lcd(0x27, 16, 2);

// change this to the number of steps on your motor
#define STEPS 100

// the previous reading from the analog input
int previous = 0;

struct IRButtons{
    char power[9] = "ba45ff00";
    char volUp[9] = "b946ff00";
    char func[9] = "b847ff00";
    char back[9] = "bb44ff00";
    char play[9] = "bf40ff00";
    char frwrd[9] = "bc43ff00";
    char down[9] = "f807ff00";
    char VolDwn[9] = "ea15ff00";
    char up[9] = "f609ff00";
    char zero[9] = "e916ff00";
    char eq[9] = "e619ff00";
    char repeat[9] = "f20dff00";
    char one[9] = "f30cff00";
    char two[9] = "e718ff00";
    char three[9] = "a15eff00";
    char four[9] = "f708ff00";
    char five[9] = "e31cff00";
    char six[9] = "a55aff00";
    char seven[9] = "bd42ff00";
    char eight[9] = "ad52ff00";
    char nine[9] = "b54aff00";
};
IRButtons buttons;

struct Motor{
    int Button1;
    int Button2;
    int Pins[4] = {0};
    Stepper stepper = Stepper(STEPS, 0, 0, 0, 0);
    void init() {
        pinMode(Button1, INPUT);
        pinMode(Button2, INPUT);

        pinMode(Pins[0], OUTPUT);
        pinMode(Pins[1], OUTPUT);
        pinMode(Pins[2], OUTPUT);
        pinMode(Pins[3], OUTPUT);

        stepper = Stepper(STEPS, Pins[0], Pins[1], Pins[2], Pins[3]);
        stepper.setSpeed(30); // set the speed of the motor to 30 RPMs
    }
};
Motor motors[3];

void drawScreen(String message = "");
void drawScreen(String message) {
    lcd.clear();

    // set cursor to first column, second row
    lcd.setCursor(0,0);

    if (message != "") {
        lcd.print(message);
    } else
        lcd.print("Initializing...");
}

IPAddress LocalIP(192, 168, 1, 222);

void connectToWifi() {
    WiFi.mode(WIFI_STA);

    if (!WiFi.config(
            LocalIP,
            IPAddress(192, 168, 1, 1),
            IPAddress(255, 255, 0, 0),
            IPAddress(8, 8, 8, 8),
            IPAddress(8, 8, 4, 4)
    ))
        Serial.println("STA Failed to configure");

    const char* ssid       = "Garden Amenities";
    const char* password   = "Am3nit1es4b1tch35";
    Serial.println("Connecting to " + String(ssid));

    WiFi.begin(ssid, password);
    for (int x = 1; x < 100; x++) {
        delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            x = 200;
            Serial.println(" CONNECTED");
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected, IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("MAC Address: ");
        Serial.println(WiFi.macAddress());
        Serial.print("Gateway IP: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS Server: ");
        Serial.println(WiFi.dnsIP());

        drawScreen("Connected!");
    }
}

void initialize() {
    int buffer = 0;

    //Move all our motors to their starting positions
    buffer = 300;
    while (buffer > 0) {
        for (auto & motor : motors)
            motor.stepper.step(+1);

        buffer--;
    }

    delay(1000);

    //Set a small margin
    buffer = 5;
    while (buffer > 0) {
        for (auto & motor : motors)
            motor.stepper.step(-1);

        buffer--;
    }
}
void setup() {
    Serial.begin(115200);

    lcd.init();
    lcd.backlight();
    drawScreen();

    //connect to WiFi
    connectToWifi();

    IrReceiver.begin(5, ENABLE_LED_FEEDBACK);

    motors[0].Button1 = 39;
    motors[0].Button2 = 36;
    motors[0].Pins[0] = 32;
    motors[0].Pins[1] = 33;
    motors[0].Pins[2] = 25;
    motors[0].Pins[3] = 26;
    motors[0].init();

    motors[1].Button1 = 35;
    motors[1].Button2 = 34;
    motors[1].Pins[0] = 27;
    motors[1].Pins[1] = 14;
    motors[1].Pins[2] = 12;
    motors[1].Pins[3] = 13;
    motors[1].init();

    motors[2].Button1 = 17;
    motors[2].Button2 = 16;
    motors[2].Pins[0] = 4;
    motors[2].Pins[1] = 0;
    motors[2].Pins[2] = 2;
    motors[2].Pins[3] = 15;
    motors[2].init();
}

void performIRFunction(char event[9]) {
    if (strcmp(event, buttons.power) == 0) {
        Serial.println("Initializing! " + String(event));
        initialize();
    }

    Serial.println("Performing function on " + String(event));
}

bool btnIsPressed = false;
String lastDisplayStr;
void loop() {

    //handle ir commands
    if (IrReceiver.decode()) {
        if (IrReceiver.decodedIRData.decodedRawData > 0) {
            char code[9];
            itoa(IrReceiver.decodedIRData.decodedRawData, code, 16);
            performIRFunction(code);
        }

        IrReceiver.resume(); // Enable receiving of the next value
    }

    //Handle Button Presses
    int increment = 1;
    int motorCount = 1;
    bool btnWasPressed = btnIsPressed;
    btnIsPressed = false;
    String btnDisplayStr = "Mv: "; //Directions are based on a perspective facing the front of the printer
    for (auto & motor : motors) {
        if (motor.Pins[0] == 0)
            continue;

        //Btn is actually pressed when false (I think because the pullup resistor reverses this)
        bool btn1Pressed = !digitalRead(motor.Button1);
        bool btn2Pressed = !digitalRead(motor.Button2);

        if (btn1Pressed) {
            btnIsPressed = true;
            motor.stepper.step(increment);

            switch(motorCount) {
                case 1:
                    btnDisplayStr += " Bck ";
                    break;
                case 2:
                    btnDisplayStr += " L ";
                    break;
                case 3:
                    btnDisplayStr += " Up ";
                    break;
                default:
                    break;
            }
        }

        if (btn2Pressed) {
            btnIsPressed = true;
            motor.stepper.step(-increment);

            switch(motorCount) {
                case 1:
                    btnDisplayStr += " Fwd ";
                    break;
                case 2:
                    btnDisplayStr += " R ";
                    break;
                case 3:
                    btnDisplayStr += " Dwn ";
                    break;
                default:
                    break;
            }
        }

        motorCount++;
    }

    if (btnIsPressed) {
        if (lastDisplayStr != btnDisplayStr)
            drawScreen(btnDisplayStr);
    } else if (btnWasPressed) {
        drawScreen();
    }

    lastDisplayStr = btnDisplayStr;
}