#include <Wire.h>  // Comes with Arduino IDE

#include "Roomba_Driver.h"
#include <Servo.h> // Servo lib
#include "scheduler.h"  // TTS

//Roomba
#define ROOMBA_SERIAL_PIN     2
#define ROOMBA_DIGITA_PIN     30

Roomba r(ROOMBA_SERIAL_PIN, ROOMBA_DIGITA_PIN);

//PWM 
#define TILT_SERVO_PIN        8
#define PAN_SERVO_PIN         9
#define LASER_PIN            10

#define TEST_PIN             40
#define TEST_PIN_2           41
#define TEST_PIN_3           43

unsigned long next_time = 1;
unsigned long runtime = 0;
bool initialized = true;

char btInput[32] = "";
char inputBuffer[32] = "";


char *token = "";
const char s[2] = ",";
//Read commands from Bluetooth.


Servo panServo;     //PAN SERVO MOTOR (X)
int panAngle = 90;  // Init angle to 90

Servo tiltServo;    //TILT SERVO Motor (Y)

int tiltAngle = 90; // Init angle to 90

const int maxStepAngle = 5;   // init max step angle


void setup() {
  Serial.begin(9600);
  Serial1.begin(19200);
  Serial.print("Starting init");
//  r.init();
  pinMode(LASER_PIN, OUTPUT);
  pinMode(TEST_PIN, OUTPUT);
  pinMode(TEST_PIN_2, OUTPUT);
  pinMode(TEST_PIN_3, OUTPUT);

  panServo.attach(PAN_SERVO_PIN);     // Pan servo
  tiltServo.attach(TILT_SERVO_PIN);   // Tilt Servo
  centerServoPosition();
  
  delay(1000); 
  Scheduler_Init();
  Scheduler_StartTask(0, 75, ReceiveInputTask);
  Scheduler_StartTask(7, 125, RoobaTasks);
  Scheduler_StartTask(17, 50, ServoTasks);
}

// INPUT EXAMPLE: "f,100,s*"

char* device = ' ';

char* joystickButton;    // Global value for controlling the laser

//Roomba
char* roombaDirection = "s"; 
char* roombaSpeed     = "0";
char* roombaRadious   = "0";
char roombaState = 'i';   // Roomba State of the roomba

//Servo
char* servoName       = " ";
int  servoPanSpeed    = 0;
int  servoTiltSpeed   = 0;




void parseInputStringAndUpdate(){
  int count = 0;
  token = strtok(btInput, s);
  while( token != NULL ) 
  {
    if(count == 0 ){   // First token
      device = token;
    }
    if(count == 1){   // Second Token
      if(*device == 'l'){  
        joystickButton = token;
        Serial.println(joystickButton);
      }else if(*device == 'r'){
        roombaDirection = token;  // Update 
      }else if(*device == 's'){
        servoPanSpeed = atoi(token);
      }
    }
    if(count == 2){   // check the third token if there is one
      if(*device == 'r' && *roombaDirection == 'd' ){     // Roomba command && drive mode
        roombaSpeed = token;   //Speed should be between -500 to 500
      }else{
        if(*device == 's'){  //  Servo Command
          servoTiltSpeed = atoi(token);  
        }else{
          roombaSpeed = token;
        }
      }
    }
    if(count == 3){   // check the forth token if there is one
      if(*device == 'r'){     // Roomba command
        roombaRadious = token;    // Radious should be betweej 0 to 2000
      }else{
        // unknown case
        Serial1.print("4th token = ");
        Serial1.print(token);
      }
    }
    
    token = strtok(NULL, s);
    count++;
  } 
  if(count < 2){
     roombaSpeed = "0";
  }else if(count > 2){
     joystickButton = "1";
  }
}

// turns laser on if inputValue is 0 and off otherwise.
void laserState(char* inputValue)
{
//  Serial.print("Laser State :");
//  Serial.println(inputValue);
  if (inputValue[0] == '0')
  {
    digitalWrite(LASER_PIN, HIGH);
  }
  else
  {
    digitalWrite(LASER_PIN, LOW);
  }
}
char *convert(uint8_t *a)
{
  char* buffer2;
  int i;

  buffer2 = malloc(9);
  if (!buffer2)
    return NULL;

  buffer2[8] = 0;
  for (i = 0; i <= 7; i++)
    buffer2[7 - i] = (((*a) >> i) & (0x01)) + '0';

  puts(buffer2);

  return buffer2;
}


//ReceiveInputTask 
void ReceiveInputTask() {
  digitalWrite(TEST_PIN, HIGH);

  while(Serial1.available()) {
    //Make sure the Roomba is ready to go.
    uint8_t command = Serial1.read();
//    char *final_string;
//    final_string = convert(&command);
    Serial.println(command);

//    free(final_string);
//    q
//    if (command != '*')
//    {
//      *inputBuffer = command;
//      strcat(btInput, inputBuffer);
//    } 
//    else
//    { 
//      //execute bt input
//      Serial.println(btInput);
////      digitalWrite(TEST_PIN, HIGH);
//      parseInputStringAndUpdate();
//      laserState(joystickButton);
////      digitalWrite(TEST_PIN, LOW);
//      btInput[0] = '\0';
//    }
  }
  digitalWrite(TEST_PIN, LOW);
}


//Roomba Tasks
void RoobaTasks() {
  digitalWrite(TEST_PIN_2, HIGH);

  if(!initialized) {
    r.init();
    initialized = true;
  }

  char command     = ' ';
  int localRadious = 0;
  int localSpeed   = 0;
  sscanf(roombaRadious, "%d", &localRadious);
  sscanf(roombaSpeed, "%d", &localSpeed);

  if(roombaDirection[0]){  
    command = roombaDirection[0];  // Get the command digit
  }
  
  switch(command)
    {
      case 'd':   //Drive State
        if(localSpeed <= 500 && localSpeed >= -500 && localRadious <= 32768){
          if(localSpeed == 0){  // Stop roomba if speed is 0
            r.drive(0, 0); 
          }else{
            Serial.print("speed: ");
            Serial.println(localSpeed);
            Serial.print(" radious: ");
            Serial.println(localRadious);
            r.drive(localSpeed, localRadious);
            roombaState = 'd';
          }
        }else{
          r.drive(0, 0);   // Stop the roomba if input value is wrong 
        }
        break;
      case 's':
        r.drive(0,0);
        roombaState = 's';
        break;
      case 'k':
        r.dock();
        roombaState = 'k';
        break;
      case 'p':
        r.power_off();
        initialized = false;
        roombaState = 'p';
        break;
      default:
        roombaState = 'i';
        break;
    }
  digitalWrite(TEST_PIN_2, LOW);
}


// Servo Functions
// get joystickpercentage
int getJoyStickPercentage(int x){
  int mapped  = map(x, 0 , 1023, -maxStepAngle, maxStepAngle );
//  Serial.print(mapped);
  if(  -2 <= mapped && mapped <= 2 ){
    mapped = 0;
  }
  return mapped;
}

void servoMovement(Servo inputServo, int angle, int* servoAngle, int minAngle, int maxAngle){    
   *servoAngle += angle;
//   Serial.print("Servo Angle : ");
//   Serial.println(*servoAngle);
//   
   if( *servoAngle >= maxAngle ){
      *servoAngle = maxAngle;
   }else if( *servoAngle <= minAngle){
      *servoAngle = minAngle;
   }
  // in steps of 1 degree
  inputServo.write(*servoAngle);        // tell servo to go to position in variable 'pos'
  
}
// End Servo Functions

// Center the servo motors
void centerServoPosition(){
  panServo.write(90);
  tiltServo.write(90);
}

// Servo Tasks
void ServoTasks(){
  // ------ Servo Motor data -------
  //  Serial.print("Pan Angle : " );
  //  Serial.println(servoPanSpeed);
  //
  //  Serial.print("Tilt Angle : " );
  //  Serial.println(servoTiltSpeed);
  // ------ Servo Motor data -------
  digitalWrite(TEST_PIN_3, HIGH);

  servoMovement( panServo,servoPanSpeed, &panAngle   ,45, 135);  
  servoMovement( tiltServo, servoTiltSpeed, &tiltAngle ,60, 135 );  
  digitalWrite(TEST_PIN_3, LOW);

}



void loop()
{
  uint32_t idle_period = Scheduler_Dispatch();
  if (idle_period)
  {
    delay(idle_period);
  }
}
