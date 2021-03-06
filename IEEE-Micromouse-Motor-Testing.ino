#include <I2C.h>

//Sensor pins
#define IRpinR 3
#define IRpinF 2
#define IRpinL 1

//Motor Pins
#define speedPinR 9
#define directionPinR 8
#define speedPinL 10
#define directionPinL 11

//Button pin
#define buttonPin 12

// Define constants
#define mazeSize 16
#define DEBUG   0  //debugging for control systems
#define DEBUG1  0  //debugging for algorithms

//Button variables
bool buttonState;
bool lastButtonState = HIGH;
long lastDebounceTime = 0;
long debounceDelay = 10;

//Maze variables
int maze [mazeSize] [mazeSize];
int visits [mazeSize] [mazeSize];

// location variables
int posx = 0;
int posy = 0;

//direction: 0: North 1: East 2: South 3: West
char direction = 0;

// Motor Speed variable
int speed = 90;

struct action {
  int act; //0 is forward, 1 is turnLeft, 2 is turnRight, 3 is turnAround
  int cells; //only matters for forward, how many cells should I move
};

bool looping = false;
bool interrupted = false;

void setup() {
  Serial.begin(9600);
  Serial.println("Hi");
  I2c.begin();
  I2c.timeOut(500);
  I2c.pullup(true);
}

#define MIN_DIST (0.3)
#define TICKS_PER_CM (95.8463)
#define ACCEL_X_VAL (25000.0/dt)
#define ACCEL_W_VAL (100.0/dt)
void moveForward(int unitcells){

  setMotors(0,0);
  double estdist = 18*unitcells; // Estimated distance from target in cm
  double oldspeederrorX = 0;
  double oldspeederrorW = 0;
  
  long timer = millis() - 1;
  long dt = 0;
  do {
    dt = millis() - timer;
    timer = millis();
    
    int leftenc, rightenc;
    readEncoders(&leftenc, &rightenc);

    //---------Reading---------

    double actualspeedX = 0;
    double actualspeedW = 0;

    actualspeedX += (1000.0/dt)*leftenc/TICKS_PER_CM / 2;
    actualspeedX += (1000.0/dt)*rightenc/TICKS_PER_CM / 2;
    estdist -= actualspeedX*dt/1000;
    
    actualspeedW += (1000.0/dt)*leftenc/TICKS_PER_CM / 2;
    actualspeedW -= (1000.0/dt)*rightenc/TICKS_PER_CM / 2;

    //-------Estimating/Predicting-------

    double idealspeedX = min(25.0, estdist*2.5+10);
    double targetspeedX = actualspeedX;
    if (targetspeedX < idealspeedX) {
      targetspeedX += ACCEL_X_VAL;
      if (targetspeedX > idealspeedX) targetspeedX = idealspeedX;
    }
    else if (targetspeedX > idealspeedX) {
      targetspeedX -= ACCEL_X_VAL;
      if (targetspeedX < idealspeedX) targetspeedX = idealspeedX;
    }

    double idealspeedW = 0;
    double targetspeedW = actualspeedW;
    if (targetspeedW < idealspeedW) {
      targetspeedW += ACCEL_W_VAL;
      if (targetspeedW > idealspeedW) targetspeedW = idealspeedW;
    }
    else if (targetspeedW > idealspeedW) {
      targetspeedW -= ACCEL_W_VAL;
      if (targetspeedW < idealspeedW) targetspeedW = idealspeedW;
    }

    //-----------PID-------------
    double kPX = 0.5, kDX = 0.1;
    double speederrorX = targetspeedX - actualspeedX;
    double errorX = kPX*speederrorX + kDX*(speederrorX-oldspeederrorX);
    oldspeederrorX = speederrorX;

    double kPW = 0.01, kDW = 0.01;
    double speederrorW = targetspeedW - actualspeedW;
    double errorW = kPW*speederrorW + kDW*(speederrorW-oldspeederrorW);
    oldspeederrorW = speederrorW;
    
    setMotors((int) min(255, (errorX + errorW)*10), (int) min(255, (errorX - errorW)*10));
    
  } while (abs(estdist) > MIN_DIST);
}

// Returns a velocity in cm/s given the number of cells the mouse needs to travel and the current
//    time the mouse has been on this profile
double getStraightProfile(unsigned long dt, int cells) {
  
  // These values are generated by calculations as follows:
  //    Distance covered = velocity*time = Area under profile
  //    Area under profile = area of ramp up (A1) + area of steady rate (A2) + area of slow down (A3)
  //    To find the time we have to travel for, we need to find the two points where
  //      the steady rate is defined (start and stop of steady region)
  //    These are the times where we ramp up/slow down
  //    We need to find the time it takes to get to the end in order to find the time to start slowing down
  //    To find the total time, we can use the distance we need to cover divided by the speed
  //    We know that during the ramp up we will cover a fixed distance because we know the target
  //      speed and the time we will be ramping up, so this is a constant
  //    The slowing down is also a constant distance, the same as the ramp up (since accel is the same)
  //    Therefore we can find the remaining distance that needs to be covered by the steady speed area
  //    Since we ramp up/slow down in a consistent amount of time (and therefore distance) we can
  //      calculate exactly when we should start these actions
  //    Speeding up time is trivial since it is always 500 ms after starting the profile
  //    Slowing down time is 500 ms before the end
  //    Since we have distance and velocity of the steady area, we can calculate the time
  //      at which we have to start slowing down
  //    This calculation is: t_slowdown = t_steady + t_rampend
  //    t_steady = d_steady/v_steady
  //    d_steady = d_total - d_ramp - d_slow
  //        d_ramp and d_slow are constants of value 1/2(500ms)*maxspeed
  //        this simplifies to:
  //    d_steady = d_total - 500ms(v_steady)
  //        So the final equation is:
  //    t_slowdown = (d_total - 500ms(v_steady))/v_steady + t_rampend
  //    t_slowdown = d_total/v_steady - 500ms + 500ms
  //    t_slowdown = d_total/v_steady
  //    
  //    This equation looks too easy to be true. Think of it this way:
  //    Imagine if the robot moved at exactly the steady max speed the whole time, without ramping
  //    Then, the time would be d_total/v_steady, as we have it
  //    Now, using ramping, we slice off a part of the distance we cover from the front and put
  //      it on the end. This means our ramp acceleration doesn't matter, as long as it is the same
  //    The time to start slowing down is still the original end time, we just added a bit extra
  //    So yeah
  //    d_total is in cm (double), v_steady is in cm/s (double)
  //    d_total is just number of cells * 18cm, v_steady is a constant we define at compile time in cm/ms
  //    
  //    As an fyi, the max ticks/50ms was 130, which is around 2600 ticks/s, which is around 27.1268 cm/s
  //    This is 0.02712674413441 cm/ms (take this number as you will)
  //    I chose a max of 25 cm/s, or 25cm/1000ms

  //             d_total  *  v_steady

  int percentage = 0; // Represents percentage of max speed in tenths of a percentage
  int t_slowdown = 18*cells*1000/25;
  
  if (dt < 200) {
    percentage = (dt*5); // Scales linearly from 0 to 1000 over 200ms
  } else if (dt >= t_slowdown) {
    percentage = 1000 - (dt-t_slowdown)*5; // Scales linearly from 1000 to 0 over 200ms
  } else {
    percentage = 1000;
  }

  //    Since the output has to be a velocity in terms of ticks of the encoder per second,
  //      we have to do more conversion math on that desired velocity
  //    out_ticks/s = v_steady * ticks/cm
  //    ticks/cm = ticks/rotation * rotations/cm
  //    rotations/cm = 1/(cm/rotation), cm/rotation = circum of wheel
  //    circum = pi*diam, diam = 4cm (according to pololu's website)
  //    So, the final number comes out to be:
  //    ticks/cm = 95.8463
  //    Multiply that number by the velocity of the profile, and Bob's your uncle

  //    Max_speed * (percentage/1000) * ticks/1000cm = percentage * (25cm*95846ticks/(1000*1000cm))
  //    out = percentage(0<->1000) * 2.39615 (cm/s)
  return 2.39615*percentage;
}

unsigned long calculateTotalProfileTime(int cells) {
  
}

void loop() {
  moveForward(4);
  setMotors(0,0);
  //delay(2000);
}

void readEncoders(int *left, int *right) {
  unsigned char lefth, leftl, righth, rightl;
  I2c.read(0x3D, 0, 1);
  lefth = I2c.receive();
  I2c.read(0x3D, 1, 1);
  leftl = I2c.receive();

  I2c.read(0x3E, 0, 1);
  righth = I2c.receive();
  I2c.read(0x3E, 1, 1);
  rightl = I2c.receive();
  *left = (lefth << 8) | leftl;
  *right = (righth << 8) | rightl;
}

int leftspeedold, rightspeedold;
void setMotors(int speedL, int speedR) {
  leftspeedold = speedL;
  rightspeedold = speedR;
  bool rRev = speedR > 0;
  bool lRev = speedL > 0;

  digitalWrite(directionPinR, rRev);
  digitalWrite(directionPinL, lRev);
  analogWrite(speedPinR, abs(speedR));
  analogWrite(speedPinL, abs(speedL));
}

void updateMotors(int deltaL, int deltaR) {
  leftspeedold += deltaL;
  if (leftspeedold > 255) leftspeedold = 255;
  if (leftspeedold < 255) leftspeedold = -255;
  rightspeedold += deltaR;
  if (rightspeedold > 255) rightspeedold = 255;
  if (rightspeedold < 255) rightspeedold = -255;
  bool rRev = rightspeedold >= 0;
  bool lRev = leftspeedold >= 0;

  digitalWrite(directionPinR, rRev);
  digitalWrite(directionPinL, lRev);
  analogWrite(speedPinR, abs(rightspeedold));
  analogWrite(speedPinL, abs(leftspeedold));
}

