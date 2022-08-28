#include <Arduino.h>
#include <WiFi.h>
#include <Stepper.h>
#include <IRremote.hpp>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>

char server[] = "hauntedhallow.xyz";
WiFiClient client;
HttpClient httpClient = HttpClient(client, server, 80);

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

struct PrintBed{
    int size = 260;
    int width = size;
    int depth = size;
    int height = size;
};
PrintBed bed;

struct Motor{
    bool ready = false;
    float position;
    float lastPosition;
    int button1;
    int button2;
    int pins[4] = {0};
    Stepper stepper = Stepper(STEPS, 0, 0, 0, 0);
    void init() {
        pinMode(button1, INPUT);
        pinMode(button2, INPUT);

        pinMode(pins[0], OUTPUT);
        pinMode(pins[1], OUTPUT);
        pinMode(pins[2], OUTPUT);
        pinMode(pins[3], OUTPUT);

        stepper = Stepper(STEPS, pins[0], pins[1], pins[2], pins[3]);
        stepper.setSpeed(30); // set the speed of the motor to 30 RPMs
    }
    bool scrolling = false;
    float source;
    float destination;
    void scrollTo(float coord) {
        Serial.println("Scrolling to " + String(coord));

        source = position;
        destination = coord;
        scrolling = true;
    }
    //This must be <= 1
    void changePosition(float amount) {
        //The stepper only takes ints so accomodate for that
        if ((int)lastPosition != (int)position)
            stepper.step(amount < 0 ? 1 : -1);

        lastPosition = position;
        position += amount;
    }
    void endScroll() {
        Serial.println("Ending scroll at " + String(position));

        scrolling = false;
        destination = position;
        lastPosition = position;
    }
};
Motor motors[3];
bool isInitialized() {
    bool initialized = true;
    for (auto & motor : motors) {
        if (!motor.ready) {
            initialized = false;
            break;
        }
    }

    return initialized;
}

void sendPost(String uri, String data) {
    httpClient.beginRequest();
    int responseCode = httpClient.post(uri);
    Serial.println("Sending " + uri + "@" + String(responseCode) + " | " + data);
    httpClient.sendHeader("Content-Type", "application/json");
    httpClient.sendHeader("Content-Length", data.length());
    httpClient.beginBody();
    httpClient.print(data);
    httpClient.endRequest();
}
void sendStatus() {
    sendPost("/api/printer/status", "{\"status\":" + String(isInitialized()) + "}");
}

void drawScreen(String message = "", bool updateStatus = true);
void drawScreen(String message, bool updateStatus) {
    lcd.clear();

    // set cursor to first column, second row
    lcd.setCursor(0,0);

    if (message != "") {
        lcd.print(message);
    }
    else {
        if (WiFi.status() == WL_CONNECTED)
            lcd.print("Connected!");

        lcd.setCursor(0, 1);
        lcd.print(isInitialized() ? "READY" : "PLEASE INIT");
    }

    if (updateStatus)
        sendStatus();
}
void alert(String message, int timer = 1000);
void alert(String message, int timer) {
    drawScreen(message, false);

    delay(timer);

    drawScreen("", false);
}

void scrollToStart() {
    alert("Scrolling to Start");

    for (auto & motor : motors)
        motor.scrollTo(0);
}
void scrollToEnd() {
    alert("Scroll to End!");

    for (auto & motor : motors)
        motor.scrollTo(bed.size);
}
void scrollToCenter() {
    alert("Scroll to Center!");

    for (auto & motor : motors)
        motor.scrollTo(bed.size / 2);
}

void initialize(bool skipRewind = false);
void initialize(bool skipRewind) {
    alert("Initializing...");
    delay(500);

    if (!skipRewind) {
        //Move all our motors to their starting positions
        int buffer = 300;
        while (buffer > 0) {
            for (auto &motor: motors)
                motor.stepper.step(+1);

            buffer--;
        }

        delay(1000);

        //Set a small margin
        int margin = 5;
        while (margin > 0) {
            for (auto &motor: motors)
                motor.stepper.step(-1);

            margin--;
        }
    }

    for (auto & motor : motors) {
        motor.ready = true;
        motor.position = 0;
    }

    //Draw the screen again after we re-init
    drawScreen();
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

        drawScreen();
    }
}
void setup() {
    Serial.begin(115200);

    lcd.init();
    lcd.backlight();
    drawScreen("Booting Up...");

    //connect to WiFi
    connectToWifi();

    IrReceiver.begin(5, ENABLE_LED_FEEDBACK);

    motors[0].button1 = 39;
    motors[0].button2 = 36;
    motors[0].pins[0] = 32;
    motors[0].pins[1] = 33;
    motors[0].pins[2] = 25;
    motors[0].pins[3] = 26;
    motors[0].init();

    motors[1].button1 = 35;
    motors[1].button2 = 34;
    motors[1].pins[0] = 27;
    motors[1].pins[1] = 14;
    motors[1].pins[2] = 12;
    motors[1].pins[3] = 13;
    motors[1].init();

    motors[2].button1 = 17;
    motors[2].button2 = 16;
    motors[2].pins[0] = 4;
    motors[2].pins[1] = 0;
    motors[2].pins[2] = 2;
    motors[2].pins[3] = 15;
    motors[2].init();
}

void performIRFunction(char event[9]) {
    if (strcmp(event, buttons.power) == 0)
        initialize();

    if (strcmp(event, buttons.back) == 0)
        scrollToStart();

    if (strcmp(event, buttons.frwrd) == 0)
        scrollToEnd();

    if (strcmp(event, buttons.zero) == 0)
        initialize(true);

    if (strcmp(event, buttons.play) == 0)
        scrollToCenter();

    Serial.println("Finished function " + String(event));
    alert("Func Complete");
}

bool btnIsPressed = false;
String lastDisplayStr;
void loop() {

    //Tne global increment value for our steppers
    int increment = 1;
    int motorIndex = 0;
    for (auto & motor : motors) {
        if (motor.scrolling) {
            //If the motor is moving forward and is at or past its destination,
            //Or if the motor is moving backwards and is at or in front of its destination,
            if (motor.lastPosition < motor.position && motor.position >= motor.destination ||
                motor.lastPosition > motor.position && motor.position <= motor.destination) {

                Serial.println("Last: " + String(motor.lastPosition) + " | Current: " + String(motor.position));

                motor.endScroll();
            } else {
                float xIncrement = 1;
                float yIncrement = 1;

                Serial.println("updating motor destination from " + String(motor.position) + " to " + String(motor.destination));

                //If this isn't a vertical axis
                if (motorIndex != 2) {

                    //pos1 = 25 | dest1 = 32
                    //pos2 = 20 | dest2 = 35
                    //7 > 15
                    float xDiff = abs(motors[0].destination - motors[0].position); // 7
                    float yDiff = abs(motors[1].destination - motors[1].position); // 15
                    //x is bigger get y in pieces
                    if (xDiff > yDiff) {
                        yIncrement = yDiff / xDiff; //Only move  y by this amount
                    } else {
                        xIncrement = xDiff / yDiff;
                    }
                }

                if (motor.destination > motor.position) {
                    switch (motorIndex) {
                        case 0:
                            motor.changePosition(xIncrement);
                            break;
                        case 1:
                            motor.changePosition(yIncrement);
                            break;
                        case 2:
                        default:
                            motor.changePosition(increment);
                            break;
                    }

                } else {
                    switch (motorIndex) {
                        case 0:
                            motor.changePosition(-xIncrement);
                            break;
                        case 1:
                            motor.changePosition(-yIncrement);
                            break;
                        case 2:
                        default:
                            motor.changePosition(-increment);
                            break;
                    }
                }
            }
        }

        motorIndex++;
    }

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
    int motorCount = 1;
    bool btnWasPressed = btnIsPressed;
    btnIsPressed = false;
    String btnDisplayStr = "Mv: "; //Directions are based on a perspective facing the front of the printer
    for (auto & motor : motors) {
        if (motor.pins[0] == 0)
            continue;

        //Btn is actually pressed when false (I think because the pullup resistor reverses this)
        bool btn1Pressed = !digitalRead(motor.button1);
        bool btn2Pressed = !digitalRead(motor.button2);
        if (btn1Pressed) {
            btnIsPressed = true;

            motor.changePosition(-increment);

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
            motor.changePosition(increment);

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