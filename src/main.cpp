#include <Arduino.h>
#include <WiFi.h>
#include <Stepper.h>
#include <IRremote.hpp>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include <AsyncUDP.h>

AsyncUDP UDP;
char server[] = "hauntedhallow.xyz";
WiFiClient client;
HttpClient httpClient = HttpClient(client, server, 80);

//Set our display properties
LiquidCrystal_I2C lcd(0x27, 16, 2);

// change this to the number of steps on your motor
#define STEPS 100

int runtime = 0;
int lastDisplayUpdateTime;

struct IRButtons{
    char power[9] = "ba45ff00";
    char volUp[9] = "b946ff00";
    char func[9] = "b847ff00";
    char back[9] = "bb44ff00";
    char play[9] = "bf40ff00";
    char frwrd[9] = "bc43ff00";
    char down[9] = "f807ff00";
    char volDwn[9] = "ea15ff00";
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

//Motor stuff
struct Motor{
    bool ready = false;
    float position;
    float lastPosition;
    int button1;
    int button2;
    int pins[4] = {0};
    Stepper stepper = Stepper(STEPS, 0, 0, 0, 0);
    void init(int steps = STEPS, int speed = 30) {
        pinMode(button1, INPUT);
        pinMode(button2, INPUT);

        pinMode(pins[0], OUTPUT);
        pinMode(pins[1], OUTPUT);
        pinMode(pins[2], OUTPUT);
        pinMode(pins[3], OUTPUT);

        stepper = Stepper(steps, pins[0], pins[1], pins[2], pins[3]);
        stepper.setSpeed(speed); // set the speed of the motor to 30 RPMs
    }
    bool scrolling = false;
    float source;
    float destination;
    void scrollTo(float  coord) {
        if (coord == position)
            return;

        Serial.println("Scrolling to " + String(coord));

        source = position;
        destination = coord;
        scrolling = true;
    }
    //This must be <= 1
    void changePosition(float amount) {
        Serial.println("Changing by: " + String(amount));

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

//Pen vars
#define PEN_FWD 19
#define PEN_BCK 12
struct PenObj {
    bool Hot = false;
    bool FeedMode = false;
    void heat() {
        digitalWrite(PEN_FWD, HIGH);
        delay(500);
        digitalWrite(PEN_FWD, LOW);
        Hot = true;
    }
    void toggleFeedMode() {
        if (FeedMode)
            FeedMode = false;
        else
            FeedMode = true;
    }
};
PenObj Pen;


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
        if (Pen.FeedMode)
            lcd.print("FEED MODE (Z Axis)");
        else
            lcd.print(isInitialized() ? "READY" : "PLEASE INIT");
    }

    if (updateStatus)
        sendStatus();

    lastDisplayUpdateTime = runtime;
}
void alert(String message, int timer = 1000);
void alert(String message, int timer) {
    Serial.println("ALERT: " + message);
    drawScreen(message, false);

    delay(timer);

    drawScreen("", false);
}

//Use -1 to not move axis at all
void scrollToCoords(float x, float y, float z) {
    for (int i = 0; i < 3; i++) {
        if (i == 0 && x > -1)
            motors[i].scrollTo(x);

        if (i == 1 && y > -1)
            motors[i].scrollTo(y);

        if (i == 2 && z > -1)
            motors[i].scrollTo(z);
    }
}

void initialize(bool skipRewind = false);
void initialize(bool skipRewind) {
    alert("Initializing...");
    delay(500);

    if (!skipRewind) {
        //Move all our motors to their starting positions
        int buffer = 300;
        while (buffer > 0) {
            for (int m = 0; m < 2; m++)
                motors[m].stepper.step(+1);

            buffer--;
        }

        delay(1000);

        //Set a small margin
        int margin = 5;
        while (margin > 0) {
            for (int i = 0; i < 2; i++)
                if (i < 2)
                    motors[i].stepper.step(-1);

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

void extrude(bool backwards = false);
void extrude(bool backwards) {
    Serial.println("Extruding | " + String(backwards));
    if (backwards) {
        digitalWrite(PEN_BCK, HIGH);
        delay(5000);
        digitalWrite(PEN_BCK, LOW);
        return;
    }

    digitalWrite(PEN_FWD, HIGH);
    delay(5000);
    digitalWrite(PEN_FWD, LOW);
}

IPAddress LocalIP(192, 168, 1, 222);
void processUdp(AsyncUDPPacket packet) {
    //Validate the incoming IP address
    String source = "UNKNOWN";
    if (packet.remoteIP().toString().indexOf("128.199.7.114") != 0) {
        alert("Received UDP request from unknown source");
        return;
    }

    char* tmpStr = (char*) malloc(packet.length() + 1);
    memcpy(tmpStr, packet.data(), packet.length());
    tmpStr[packet.length()] = '\0'; // ensure null termination
    String dataString = String(tmpStr);
    free(tmpStr);

    Serial.println("Received " + dataString + " from UDP@" + packet.remoteIP().toString());

    StaticJsonDocument<200> json;
    DeserializationError error = deserializeJson(json, dataString);
    if (error) {
        Serial.println("JSON ERROR!");
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        alert("DeserializeJson!");
        return;
    }

    if (json.containsKey("CMD")) {
        if (json["CMD"] == "PEN_ON") {
            if (!Pen.Hot)
                Pen.heat();
        } else if (json["CMD"] == "PEN_FWD") {
            extrude();
        } else if (json["CMD"] == "PEN_BCK") {
            extrude(true);
        } else
            alert("Requested action not recognized.");
    } else
        alert("Spirit requested no action.");
}
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

    //3D Pen
    pinMode(PEN_BCK, OUTPUT); //Bck
    pinMode(PEN_FWD, OUTPUT); //Fwd

    //connect to WiFi
    connectToWifi();

    if(UDP.listen(LocalIP, 4225)) {
        Serial.println("UDP listening locally on IP \"" + LocalIP.toString() + ":" + 4225 + "\"");
        UDP.onPacket([](AsyncUDPPacket packet) {
            Serial.println("Received UDP Packet. Processing...");
            processUdp(packet);
        });
    }

    IrReceiver.begin(18);

    motors[0].button1 = 34;
    motors[0].button2 = 35;
    motors[0].pins[0] = 26;
    motors[0].pins[1] = 27;
    motors[0].pins[2] = 14;
    motors[0].pins[3] = 13;
    motors[0].init();

    motors[1].button1 = 36;
    motors[1].button2 = 39;
    motors[1].pins[0] = 32;
    motors[1].pins[1] = 33;
    motors[1].pins[2] = 25;
    motors[1].pins[3] = 5;
    motors[1].init();

    motors[2].button1 = 16;
    motors[2].button2 = 17;
    motors[2].pins[0] = 4;
    motors[2].pins[1] = 0;
    motors[2].pins[2] = 2;
    motors[2].pins[3] = 15;
    motors[2].init(32, 500);
}

void performIRFunction(char event[9]) {
    Serial.println("Caught IR function " + String(event));

    //Power Button = Full Initialize
    if (strcmp(event, buttons.power) == 0)
        initialize();

    //Zero Button = Set initialized without moving motors
    if (strcmp(event, buttons.zero) == 0)
        initialize(true);

    //Skip Back = Zero all motors
    if (strcmp(event, buttons.back) == 0)
        scrollToCoords(0, 0, 0);

    //Skip Forward = Max all motors
    if (strcmp(event, buttons.frwrd) == 0)
        scrollToCoords(bed.size, bed.size, bed.size);

    //Play Button = Center all motors
    if (strcmp(event, buttons.play) == 0) {
        float half = (float)bed.size / 2;
        scrollToCoords(half, half, half);
    }

    //Volume Down = Send position to back right corner
    if (strcmp(event, buttons.volDwn) == 0)
        scrollToCoords(0, bed.size, bed.size);

    //Volume Up = Send position to front left corner
    if (strcmp(event, buttons.volUp) == 0)
        scrollToCoords(bed.size, 0, bed.size);

    //Func Button = Turn on pen
    if (strcmp(event, buttons.func) == 0)
        if (!Pen.Hot)
            Pen.heat();
        else
            Pen.toggleFeedMode();


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
    int motorCount = 0;
    bool btnWasPressed = btnIsPressed;
    btnIsPressed = false;
    String btnDisplayStr = "Mv: "; //Directions are based on a perspective facing the front of the printer
    for (auto & motor : motors) {
        if (motor.pins[0] == 0)
            continue;

        //Btn is actually pressed when false (I think because the pullup resistor reverses this)
        bool btn1Pressed = !digitalRead(motor.button1);
        bool btn2Pressed = !digitalRead(motor.button2);
        if (Pen.FeedMode) {
            if (btn1Pressed) {
                btnIsPressed = true;

                digitalWrite(PEN_FWD, HIGH);
            }
            if (btn2Pressed) {
                btnIsPressed = true;

                digitalWrite(PEN_BCK, HIGH);
            }
        } else {
            if (btn1Pressed) {
                btnIsPressed = true;

                motor.changePosition(-increment);

                switch (motorCount) {
                    case 0:
                        btnDisplayStr += " Bck ";
                        break;
                    case 1:
                        btnDisplayStr += " L ";
                        break;
                    case 2:
                        btnDisplayStr += " Up ";
                        break;
                    default:
                        break;
                }
            }
            if (btn2Pressed) {
                btnIsPressed = true;
                motor.changePosition(increment);

                switch (motorCount) {
                    case 0:
                        btnDisplayStr += " Fwd ";
                        break;
                    case 1:
                        btnDisplayStr += " R ";
                        break;
                    case 2:
                        btnDisplayStr += " Dwn ";
                        break;
                    default:
                        break;
                }
            }
        }

        motorCount++;
    }

    //If we're pressing a directional button display that on the screen
    if (btnIsPressed) {
        if (lastDisplayStr != btnDisplayStr)
            drawScreen(btnDisplayStr);

    //Redraw the screen when we release a button or the last display update time is greater than (roughly) 5 seconds
    } else if (btnWasPressed || runtime - lastDisplayUpdateTime > 3000000) {
        Serial.println("Runtime: " + String(runtime) + " | Last Update: " + String(lastDisplayUpdateTime));
        drawScreen();
    }

    if (!btnIsPressed && btnWasPressed && Pen.FeedMode) {
        digitalWrite(PEN_BCK, LOW);
        digitalWrite(PEN_FWD, LOW);
    }

    lastDisplayStr = btnDisplayStr;
    runtime++;
}