#include <ros.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Float32.h>
#include <auv_cal_state_la_2017/HControl.h>
#include <auv_cal_state_la_2017/RControl.h>
#include <Servo.h>
#include <Wire.h>
#include <SPI.h>
#include <Wire.h>
#include "MS5837.h"
#include "SFE_LSM9DS0.h"

#define GyroMeasError PI * (40.0f / 180.0f)       // gyroscope measurement error in rads/s (shown as 3 deg/s)
#define GyroMeasDrift PI * (0.0f / 180.0f)      // gyroscope measurement drift in rad/s/s (shown as 0.0 deg/s/s)
#define beta sqrt(3.0f / 4.0f) * GyroMeasError   // compute beta
#define zeta sqrt(3.0f / 4.0f) * GyroMeasDrift   // compute zeta, the other free parameter in the Madgwick scheme usually set to a small or zero value
#define Kp 2.0f * 5.0f // these are the free parameters in the Mahony filter and fusion scheme, Kp for proportional feedback, Ki for integral
#define Ki 0.0f

#define LSM9DS0_XM  0x1D 
#define LSM9DS0_G   0x6B 
LSM9DS0 dof(MODE_I2C, LSM9DS0_G, LSM9DS0_XM);

const byte INT1XM = 53; // INT1XM tells us when accel data is ready
const byte INT2XM = 51; // INT2XM tells us when mag data is ready
const byte DRDYG  = 49; // DRDYG  tells us when gyro data is ready

//Initialization of the Servo's for Blue Robotics Motors 
Servo T1;     //right front
Servo T2;     //right back
Servo T3;     //left front 
Servo T4;     //left back
Servo T5;     //right front
Servo T6;     //right back
Servo T7;     //left front 
Servo T8;     //left back

MS5837 sensor;

//CHANGED PWM_Motors TO PWM_Motors_depth SINCE THERE ARE 2 DIFFERENT PWM CALCULATIONS
//ONE IS FOR DEPTH AND THE OTHER IS USED FOR MOTORS TO ROTATE TO PROPER NEW LOCATION
int PWM_Motors_Depth;
float dutyCycl_depth;
float assignedDepth;
float feetDepth_read;

//initializations for IMU
float pitch, yaw, roll, heading;
float deltat = 0.0f;        // integration interval for both filter schemes
uint32_t count = 0;         // used to control display output rate
uint32_t delt_t = 0;        // used to control display output rate
uint32_t lastUpdate = 0;    // used to calculate integration interval
uint32_t Now = 0;           // used to calculate integration interval

int i;
int PWM_Motors_orient;
float abias[3] = {0, 0, 0}, gbias[3] = {0, 0, 0};
float ax, ay, az, gx, gy, gz, mx, my, mz; // variables to hold latest sensor data values 
float q[4] = {1.0f, 0.0f, 0.0f, 0.0f};    // vector to hold quaternion
float eInt[3] = {0.0f, 0.0f, 0.0f};       // vector to hold integral error for Mahony method
float temperature;
float dutyCycl_orient;
float assignedYaw;


//Initialize ROS node
const float rotationUpperBound = 166.2;
const float rotationLowerBound = -193.8;
float bottomDepth = 12;
bool isGoingUp;
bool isGoingDown;
bool isTurningRight;
bool isTurningLeft;
bool keepTurningRight;
bool keepTurningLeft;

ros::NodeHandle nh;
std_msgs::Float32 currentDepth;
std_msgs::Float32 currentRotation;
auv_cal_state_la_2017::HControl hControlStatus;
auv_cal_state_la_2017::RControl rControlStatus;

ros::Publisher hControlPublisher("height_control_status", &hControlStatus);     //int: state, float: depth
ros::Publisher rControlPublisher("rotation_control_status", &rControlStatus);   //int: state, float: rotation
ros::Publisher currentDepthPublisher("current_depth", &currentDepth);           //float: depth
ros::Publisher currentRotationPublisher("current_rotation", &currentRotation);  //float: rotation

void hControlCallback(const auv_cal_state_la_2017::HControl& hControl);
void rControlCallback(const auv_cal_state_la_2017::RControl& rControl);
ros::Subscriber<auv_cal_state_la_2017::HControl> hControlSubscriber("height_control", &hControlCallback);   //int: state, float: depth
ros::Subscriber<auv_cal_state_la_2017::RControl> rControlSubscriber("rotation_control", &rControlCallback); //int: state, float: rotation

void setup() {
  
  //For servo motors on Pelican, pins 2-5 are for motors 1-4. PWM on these motors are 1100-1499 (counter
  //clockwise direction) and 1501-1900 (clockwise direction). Note that in code I use pins 6-8, this was used
  //for testing with leds. 
  pinMode(INT1XM, INPUT);
  pinMode(INT2XM, INPUT);
  pinMode(DRDYG,  INPUT);
  pinMode(2, OUTPUT); //1 on motor  
  pinMode(3, OUTPUT); //2 on motor
  pinMode(4, OUTPUT); //3 on motor
  pinMode(5, OUTPUT); //4 on motor
  pinMode(6, OUTPUT); //5 on motor
  pinMode(7, OUTPUT); //6 on motor
  pinMode(8, OUTPUT); //7 on motor
  pinMode(9, OUTPUT); //8 on motor
  
 //Pelican Motors activation of motors (initialization of pins to servo motors)
  T1.attach(2); //right front servo
  T1.writeMicroseconds(1500);  
  T2.attach(3); //right back servo
  T2.writeMicroseconds(1500);  
  T3.attach(4); //back left servo
  T3.writeMicroseconds(1500);
  T4.attach(5); //front left servo
  T4.writeMicroseconds(1500);
  T5.attach(6); //front left servo
  T5.writeMicroseconds(1500);
  T6.attach(7); //front left servo
  T6.writeMicroseconds(1500);
  T7.attach(8); //front left servo
  T7.writeMicroseconds(1500);
  T8.attach(9); //front left servo
  T8.writeMicroseconds(1500);
  
  delay(1000);
  
  
  Wire.begin();
  Serial.begin(38400);

  //Initialize ROS variable
  isGoingUp = false;
  isGoingDown = false;
  isTurningRight = false;
  isTurningLeft = false;
  keepTurningRight = false;
  keepTurningLeft = false;
  
  //Testing------------------
  feetDepth_read = 0;
  yaw = 0;
  
  assignedDepth = feetDepth_read;
  currentDepth.data = feetDepth_read;
  
  assignedYaw = yaw;
  currentRotation.data = yaw;
  
  hControlStatus.state = 1;
  hControlStatus.depth = 0;
  rControlStatus.state = 1;
  rControlStatus.rotation = 0;
  
  nh.initNode();
  nh.subscribe(hControlSubscriber);
  nh.subscribe(rControlSubscriber);
  nh.advertise(hControlPublisher);
  nh.advertise(rControlPublisher);
  nh.advertise(currentDepthPublisher);
  nh.advertise(currentRotationPublisher);

  initializeIMU();                    

  sensor.init();  
  sensor.setFluidDensity(997); // kg/m^3 (997 freshwater, 1029 for seawater)
  
  nh.loginfo("Data is ready.");
  nh.loginfo("Sub is staying. Waiting to receive data from master...");

}

void loop() {
  
  gettingRawData();

  //Timer
  Now = micros();
  deltat = ((Now - lastUpdate)/1000000.0f); // set integration time by time elapsed since last filter update
  lastUpdate = Now;
  // Sensors x- and y-axes are aligned but magnetometer z-axis (+ down) is opposite to z-axis (+ up) of accelerometer and gyro!
  // This is ok by aircraft orientation standards!  
  // Pass gyro rate as rad/s
  MahonyQuaternionUpdate(ax, ay, az, gx*PI/180.0f, gy*PI/180.0f, gz*PI/180.0f, mx, my, mz);

  // Serial print and/or display at 0.5 s rate independent of data rates
  delt_t = millis() - count;
  if (delt_t > 10) {

    // Define output variables from updated quaternion---these are Tait-Bryan angles, commonly used in aircraft orientation.
    // In this coordinate system, the positive z-axis is down toward Earth. 
    // Yaw is the angle between Sensor x-axis and Earth magnetic North (or true North if corrected for local declination), 
    // looking down on the sensor positive yaw is counterclockwise.
    // Pitch is angle between sensor x-axis and Earth ground plane, toward the Earth is positive, up toward the sky is negative.
    // Roll is angle between sensor y-axis and Earth ground plane, y-axis up is positive roll.
    // These arise from the definition of the homogeneous rotation matrix constructed from quaternions.
    // Tait-Bryan angles as well as Euler angles are non-commutative; that is, to get the correct orientation the rotations must be
    // applied in the correct order which for this configuration is yaw, pitch, and then roll.
    // For more see http://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles which has additional links.
    
    //yaw   = atan2(2.0f * (q[1] * q[2] + q[0] * q[3]), q[0] * q[0] + q[1] * q[1] - q[2] * q[2] - q[3] * q[3]);   
    pitch = -asin(2.0f * (q[1] * q[3] - q[0] * q[2]));
    roll  = atan2(2.0f * (q[0] * q[1] + q[2] * q[3]), q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]);
    pitch *= 180.0f / PI;
    //yaw   *= 180.0f / PI; 
    //yaw   -= 13.8; // Declination at Danville, California is 13 degrees 48 minutes and 47 seconds on 2014-04-04
    roll  *= 180.0f / PI;

   //****************************NEED TO CHECK WITH ERICK!!
  }//****************************The bracket was not here before!

  //Depth
  //Testing----------------------
  //feetDepth_read =  sensor.depth() * 3.28;                                   //1 meter = 3.28 feet  
  dutyCycl_depth = (abs(assignedDepth - feetDepth_read)/ 12.0);              //function to get a percentage of assigned height to the feet read
  PWM_Motors_Depth = dutyCycl_depth * 350;                                   //PWM for motors are between 1500 - 1900; difference is 400 

  //Rotation
  //duty cycle and PWM calculation for orientation
  dutyCycl_orient = degreeToTurn() / 360.0; //Warning: the return value from degreeToTurn is from 0 to 180
  //  PWM_Motors_orient = dutyCycl * 400;
  //****************************NEED TO CHECK WITH ERICK!!
  PWM_Motors_orient = dutyCycl_orient * 400;

  //Apply on Motors
  heightControl();
  rotationControl();

  //Update and publish current data to master
  currentDepth.data = feetDepth_read;
  currentDepthPublisher.publish(&currentDepth);
  currentRotation.data = yaw;
  currentRotationPublisher.publish(&currentRotation);
  
  nh.spinOnce();    
  count = millis(); 

  delay(100);
}


void hControlCallback(const auv_cal_state_la_2017::HControl& hControl) {

  char depthChar[6];
  float depth = hControl.depth;  
  dtostrf(depth, 4, 2, depthChar);

  if(hControl.state == 0){  
    if(!isGoingUp && !isGoingDown){
      if(depth == -1 || depth + assignedDepth >= bottomDepth)
        assignedDepth = bottomDepth;
      else
        assignedDepth = assignedDepth + depth;
      isGoingDown = true;
      nh.loginfo("Going down...");
      nh.loginfo(depthChar);
      nh.loginfo("ft...(-1 means infinite)");
      hControlStatus.state = 0;
      hControlStatus.depth = depth;
    }else
      nh.loginfo("Sub is still running. Command abort.");
  }
  else if(hControl.state == 1){  
    if(isGoingUp || isGoingDown){
      isGoingUp = false;
      isGoingDown = false;
      nh.loginfo("Height control is now cancelled");
    }
    assignedDepth = feetDepth_read;
    hControlStatus.state = 1;
    hControlStatus.depth = depth;
  }
  else if(hControl.state == 2){
    if(!isGoingUp && !isGoingDown){
      if(depth == -1 || depth >= assignedDepth)
        assignedDepth = 0;
      else 
        assignedDepth = assignedDepth - depth;
      isGoingUp = true;
      nh.loginfo("Going up...");
      nh.loginfo(depthChar);
      nh.loginfo("ft...(-1 means infinite)");
      hControlStatus.state = 2;
      hControlStatus.depth = depth;
    }else
      nh.loginfo("Sub is still running.Command abort.");
  }
  hControlPublisher.publish(&hControlStatus);
  
}


/*if (left){
  yaw_calc = yaw + des_degree; 

  if (yaw_calc >= 166){                   //maximum degree reached is 166, this statement compinsates the conversion from negative to positive degree
    yaw_calc1 = yaw_calc - 166;          //difference between the calculated value to the max value
    new_location_yaw = yaw_calc1 - 193;
  }
  else{
    new_location_yaw = yaw_calc;         //this value ranges between [-193,166] 
  }      
}

else if (right){
  yaw_calc = yaw - des_degree; 

  if(yaw_calc < -193){                     //minimum degree reached is -193
    yaw_calc1 = yaw_calc + 193;            //difference between the calculated value to the min value
    new_location_yaw = 166.01 + yaw_calc1; //new location in positive coordinate system
  }
  else {
    new_location_yaw = yaw_calc; //this value ranges between [-193,166] 
  }
  
}

else{
  yaw_calc = yaw; 
}*/

void rControlCallback(const auv_cal_state_la_2017::RControl& rControl){
  
  char rotationChar[6];
  float rotation = rControl.rotation;   
  dtostrf(rotation, 4, 2, rotationChar);

  if(rControl.state == 0){  
    if(!isTurningRight && !isTurningLeft){
      if(rotation == -1) 
        keepTurningLeft = true;
      else{     
        if (yaw + rotation >= 166) 
          assignedYaw = yaw + rotation - 359;
        else 
          assignedYaw = yaw + rotation;        
      }
      isTurningLeft = true;
      nh.loginfo("Turning left...");
      nh.loginfo(rotationChar);
      nh.loginfo("degree...(-1 means infinite)");
      rControlStatus.state = 0;
      rControlStatus.rotation = rotation;
    }else 
      nh.loginfo("Sub is still rotating. Command abort.");
  }
  else if(rControl.state == 1){  
    if(isTurningRight || isTurningLeft || keepTurningRight || keepTurningLeft){
      isTurningRight = false;
      isTurningLeft = false;
      keepTurningRight = false;
      keepTurningLeft = false;
      nh.loginfo("Rotation control is now cancelled");
    }
    assignedYaw = yaw;    
    rControlStatus.state = 1;
    rControlStatus.rotation = rotation;
  }
  else if(rControl.state == 2){
    if(!isTurningRight && !isTurningLeft){
      if(rotation == -1) 
        keepTurningRight = true;
      else{
        if (yaw - rotation < -193) 
          assignedYaw = yaw - rotation + 359;
        else 
          assignedYaw = yaw - rotation;        
      } 
      isTurningRight = true;
      nh.loginfo("Turning right...");
      nh.loginfo(rotationChar);
      nh.loginfo("degree...(-1 means infinite)");
      rControlStatus.state = 2;
      rControlStatus.rotation = rotation;
    }else 
      nh.loginfo("Sub is still rotating.Command abort.");
  }
  rControlPublisher.publish(&rControlStatus);
  
}


//Leveling while staying
void stayLeveling(){
  
  for (i = 0; (2 * i) < 90; i++){ //loop will start from 0 degrees -> 90 degrees 
    //right
    if((roll > 2*i) && (roll < (2*i + 2))){
      //Boost the right motors
      T2.writeMicroseconds(1500 + i*8);
      T3.writeMicroseconds(1500 - i*8);
      //Downgrade the left motors
      T1.writeMicroseconds(1500 + i*4);
      T4.writeMicroseconds(1500 - i*4);
    }
    //left
    if((roll < -1 *(2*i)) && (roll > -1 *(2*i + 2))){
      //Boost the left motors
      T1.writeMicroseconds(1500 - i*8);
      T4.writeMicroseconds(1500 + i*8);
      //Downgrade the right motors
      T2.writeMicroseconds(1500 - i*4);
      T3.writeMicroseconds(1500 + i*4);
    }
    //backward
    if((pitch > 2*i) && (pitch < (2*i + 2))){
      //Boost the back motors
      T3.writeMicroseconds(1500 - i*8);
      T4.writeMicroseconds(1500 + i*8);
      //Downgrade the front motors
      T1.writeMicroseconds(1500 + i*4);
      T2.writeMicroseconds(1500 - i*4);  
    }
    //forward
    if((pitch < -1*( 2*i)) && (pitch > -1 *(2*i + 2))){
      //Boost the front motors
      T1.writeMicroseconds(1500 - i*8);
      T2.writeMicroseconds(1500 + i*8);
      //Downgrade the back motors
      T3.writeMicroseconds(1500 + i*4);
      T4.writeMicroseconds(1500 - i*4);
    }
  }
  
}


//Going upward
void goingUpward(){
  
  int levelPower = (400 - PWM_Motors_Depth) / 45;
  int reversedLevelPower = (PWM_Motors_Depth / 45) * (-1);

  for(i = 0; (2 * i) < 90; i++){
    //right
    if((roll > 2 * i) && (roll < (2 * i + 2))){
      //Boost the right motors
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      //Downgrade the left motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);
    }
    //left
    if((roll < -1 *( 2 * i)) && (roll > -1 * (2 * i + 2))){
      //Boost the left motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      //Downgrade the right motors
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
    }
    //backward
    if((pitch > 2 * i) && (pitch < (2 * i + 2))){ 
      //Boost the back motors
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      //Downgrade the front motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);  
    }
    //forward
    if((pitch < -1 * (2 * i)) && (pitch > -1 * (2 * i + 2))){
      //Boost the front motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      //Downgrade the back motors
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);
    }
  }
    
}

//Going downward
void goingDownward(){
  
  PWM_Motors_Depth = -PWM_Motors_Depth;
  int levelPower = ((400 + PWM_Motors_Depth) / 45) * (-1);
  int reversedLevelPower = ((-1) * PWM_Motors_Depth) / 45;

  for (i = 0; (2 * i) < 90; i++){ //loop will start from 0 degrees -> 90 degrees 
    //right
    if((roll > 2*i) && (roll < (2*i + 2))){
      //Boost the left motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      //Downgrade the right motors
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);       
    }
    //left
    if((roll < -1 *(2*i)) && (roll > -1 *(2*i + 2))){
      //Boost the right motors
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      //Downgrade the left motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);     
    }
    //backward
    if((pitch > 2*i) && (pitch < (2*i + 2))){
      //Boost the front motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower); 
      //Downgrade the back motors
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);       
    }
    //forward
    if((pitch < -1*( 2*i)) && (pitch > -1 *(2*i + 2))){
      //Boost the back motors
      T3.writeMicroseconds(1500 - PWM_Motors_Depth - i * levelPower);
      T4.writeMicroseconds(1500 + PWM_Motors_Depth + i * levelPower);
      //Downgrade the front motors
      T1.writeMicroseconds(1500 - PWM_Motors_Depth - i * reversedLevelPower);
      T2.writeMicroseconds(1500 + PWM_Motors_Depth + i * reversedLevelPower);      
    }
  }
    
}

void heightControl(){
  
  //Going down
  if (feetDepth_read < assignedDepth - 0.5){   
    goingDownward();
    
    //Testing--------------------------
    feetDepth_read += 0.05;
    
  }  
  //Going up
  else if (feetDepth_read > assignedDepth + 0.5){ 
    goingUpward(); 
    
    //Testing---------------------------
    feetDepth_read -= 0.05;
      
  } 
  //Staying
  else {   
    if(isGoingUp || isGoingDown){
      isGoingUp = false;
      isGoingDown = false;
      nh.loginfo("Assigned depth reached.");
    }
    hControlStatus.state = 1;
    hControlStatus.depth = 0;
    hControlPublisher.publish(&hControlStatus);
    stayLeveling();
  }
  
}

//read rotation is from -193.8 to 166.2
void rotationControl(){

  float delta = degreeToTurn();
  
  if(keepTurningLeft){
    //Turn on left rotation motor with fixed power

    //Testing----------------------------
    yaw += 0.5;
    if(yaw > rotationUpperBound) 
      yaw -= 360;
  }
  else if(keepTurningRight){
    //Turn on right rotation motor with fixed power

    //Testing----------------------------
    yaw -= 0.5;
    if(yaw < rotationLowerBound) 
      yaw +=360;
  }
  // AutoRotation to the assignedYaw with +- 1.5 degree error tolerance
  else if(delta > 1.5){ 
    if(yaw + delta > rotationUpperBound){
      if(yaw - delta == assignedYaw) 
        rotateRightDynamically();
      else 
        rotateLeftDynamically();
    }
    else if(yaw - delta < rotationLowerBound){
      if(yaw + delta == assignedYaw) 
        rotateLeftDynamically();
      else 
        rotateRightDynamically();
    }
    else if(yaw < assignedYaw) 
      rotateLeftDynamically();
    else 
      rotateRightDynamically();
  }
  //No rotation
  else{
    if(isTurningRight || isTurningLeft || keepTurningRight || keepTurningLeft){
      isTurningRight = false;
      isTurningLeft = false;
      keepTurningRight = false;
      keepTurningLeft = false;
      nh.loginfo("Assigned rotation reached.");
    }
    rControlStatus.state = 1;
    rControlStatus.rotation = 0;     
    rControlPublisher.publish(&rControlStatus);
  }
  
}


//&&&&&&&&&&&&&&&&&&&&&&&&&& 
//NOTE: I FORGOT WHAT IS THE ROTATION ON THE MOTORS IN THE BOT TO SEE WHAT DIRECTION OF MOVEMENT
//WHEN I RETURN TO THE ROOM I CAN FIND OUT AND EDIT THIS CODE.  
//  if (yaw < assignedYaw){  
//    //turn on motos to go down
//    T5.writeMicroseconds(1500 + PWM_Motors);
//    T7.writeMicroseconds(1500 - PWM_Motors);
//  }
//  else if (yaw > assignedYaw){ 
//    //turn on motors to go up
//    T5.writeMicroseconds(1500 - PWM_Motors);
//    T7.writeMicroseconds(1500 + PWM_Motors);
//  } 
void rotateLeftDynamically(){
  //Rotate left with PWM_Motors_orient
  
  //Testing----------------------------
  yaw += 0.5;
  if(yaw > rotationUpperBound) yaw -= 360;
  
}

void rotateRightDynamically(){
  //Rotate right with PWM_Motors_orient
  
  //Testing----------------------------
  yaw -= 0.5;
  if(yaw < rotationLowerBound) yaw +=360;
  
}

//Return 0 to 180
float degreeToTurn(){
  float difference = max(yaw, assignedYaw) - min(yaw, assignedYaw);
  if (difference > 180) return 360 - difference;
  else return difference;
}


void gettingRawData(){
  
  if(digitalRead(DRDYG)) {  // When new gyro data is ready
  dof.readGyro();           // Read raw gyro data
    gx = dof.calcGyro(dof.gx) - gbias[0];   // Convert to degrees per seconds, remove gyro biases
    gy = dof.calcGyro(dof.gy) - gbias[1];
    gz = dof.calcGyro(dof.gz) - gbias[2];
  }
  
  if(digitalRead(INT1XM)) {  // When new accelerometer data is ready
    dof.readAccel();         // Read raw accelerometer data
    ax = dof.calcAccel(dof.ax) - abias[0];   // Convert to g's, remove accelerometer biases
    ay = dof.calcAccel(dof.ay) - abias[1];
    az = dof.calcAccel(dof.az) - abias[2];
  }
  
  if(digitalRead(INT2XM)) {  // When new magnetometer data is ready
    dof.readMag();           // Read raw magnetometer data
    mx = dof.calcMag(dof.mx);     // Convert to Gauss and correct for calibration
    my = dof.calcMag(dof.my);
    mz = dof.calcMag(dof.mz);
    
    dof.readTemp();
    temperature = 21.0 + (float) dof.temperature/8.; // slope is 8 LSB per degree C, just guessing at the intercept
  }
  
}

void initializeIMU(){
  
  uint32_t status = dof.begin();
  delay(2000); 

  // Set data output ranges; choose lowest ranges for maximum resolution
  // Accelerometer scale can be: A_SCALE_2G, A_SCALE_4G, A_SCALE_6G, A_SCALE_8G, or A_SCALE_16G   
  dof.setAccelScale(dof.A_SCALE_2G);
  // Gyro scale can be:  G_SCALE__245, G_SCALE__500, or G_SCALE__2000DPS
  dof.setGyroScale(dof.G_SCALE_245DPS);
  // Magnetometer scale can be: M_SCALE_2GS, M_SCALE_4GS, M_SCALE_8GS, M_SCALE_12GS   
  dof.setMagScale(dof.M_SCALE_2GS);
  
  // Set output data rates  
  // Accelerometer output data rate (ODR) can be: A_ODR_3125 (3.225 Hz), A_ODR_625 (6.25 Hz), A_ODR_125 (12.5 Hz), A_ODR_25, A_ODR_50, 
  //                                              A_ODR_100,  A_ODR_200, A_ODR_400, A_ODR_800, A_ODR_1600 (1600 Hz)
  dof.setAccelODR(dof.A_ODR_200); // Set accelerometer update rate at 100 Hz
  // Accelerometer anti-aliasing filter rate can be 50, 194, 362, or 763 Hz
  // Anti-aliasing acts like a low-pass filter allowing oversampling of accelerometer and rejection of high-frequency spurious noise.
  // Strategy here is to effectively oversample accelerometer at 100 Hz and use a 50 Hz anti-aliasing (low-pass) filter frequency
  // to get a smooth ~150 Hz filter update rate
  dof.setAccelABW(dof.A_ABW_50); // Choose lowest filter setting for low noise
 
  // Gyro output data rates can be: 95 Hz (bandwidth 12.5 or 25 Hz), 190 Hz (bandwidth 12.5, 25, 50, or 70 Hz)
  //                                 380 Hz (bandwidth 20, 25, 50, 100 Hz), or 760 Hz (bandwidth 30, 35, 50, 100 Hz)
  dof.setGyroODR(dof.G_ODR_190_BW_125);  // Set gyro update rate to 190 Hz with the smallest bandwidth for low noise

  // Magnetometer output data rate can be: 3.125 (ODR_3125), 6.25 (ODR_625), 12.5 (ODR_125), 25, 50, or 100 Hz
  dof.setMagODR(dof.M_ODR_125); // Set magnetometer to update every 80 ms
    
  // Use the FIFO mode to average accelerometer and gyro readings to calculate the biases, which can then be removed from
  // all subsequent measurements.
  dof.calLSM9DS0(gbias, abias);
  
}

void MahonyQuaternionUpdate(float ax, float ay, float az, float gx, float gy, float gz, float mx, float my, float mz){
  
  float q1 = q[0], q2 = q[1], q3 = q[2], q4 = q[3];   // short name local variable for readability
  float norm;
  float hx, hy, bx, bz;
  float vx, vy, vz, wx, wy, wz;
  float ex, ey, ez;
  float pa, pb, pc;

  // Auxiliary variables to avoid repeated arithmetic
  float q1q1 = q1 * q1;
  float q1q2 = q1 * q2;
  float q1q3 = q1 * q3;
  float q1q4 = q1 * q4;
  float q2q2 = q2 * q2;
  float q2q3 = q2 * q3;
  float q2q4 = q2 * q4;
  float q3q3 = q3 * q3;
  float q3q4 = q3 * q4;
  float q4q4 = q4 * q4;   

  // Normalise accelerometer measurement
  norm = sqrt(ax * ax + ay * ay + az * az);
  if (norm == 0.0f) return; // handle NaN
  norm = 1.0f / norm;        // use reciprocal for division
  ax *= norm;
  ay *= norm;
  az *= norm;

  // Normalise magnetometer measurement
  norm = sqrt(mx * mx + my * my + mz * mz);
  if (norm == 0.0f) return; // handle NaN
  norm = 1.0f / norm;        // use reciprocal for division
  mx *= norm;
  my *= norm;
  mz *= norm;

  // Reference direction of Earth's magnetic field
  hx = 2.0f * mx * (0.5f - q3q3 - q4q4) + 2.0f * my * (q2q3 - q1q4) + 2.0f * mz * (q2q4 + q1q3);
  hy = 2.0f * mx * (q2q3 + q1q4) + 2.0f * my * (0.5f - q2q2 - q4q4) + 2.0f * mz * (q3q4 - q1q2);
  bx = sqrt((hx * hx) + (hy * hy));
  bz = 2.0f * mx * (q2q4 - q1q3) + 2.0f * my * (q3q4 + q1q2) + 2.0f * mz * (0.5f - q2q2 - q3q3);

  // Estimated direction of gravity and magnetic field
  vx = 2.0f * (q2q4 - q1q3);
  vy = 2.0f * (q1q2 + q3q4);
  vz = q1q1 - q2q2 - q3q3 + q4q4;
  wx = 2.0f * bx * (0.5f - q3q3 - q4q4) + 2.0f * bz * (q2q4 - q1q3);
  wy = 2.0f * bx * (q2q3 - q1q4) + 2.0f * bz * (q1q2 + q3q4);
  wz = 2.0f * bx * (q1q3 + q2q4) + 2.0f * bz * (0.5f - q2q2 - q3q3);  

  // Error is cross product between estimated direction and measured direction of gravity
  ex = (ay * vz - az * vy) + (my * wz - mz * wy);
  ey = (az * vx - ax * vz) + (mz * wx - mx * wz);
  ez = (ax * vy - ay * vx) + (mx * wy - my * wx);
  if (Ki > 0.0f)
  {
    eInt[0] += ex;      // accumulate integral error
    eInt[1] += ey;
    eInt[2] += ez;
  }
  else
  {
    eInt[0] = 0.0f;     // prevent integral wind up
    eInt[1] = 0.0f;
    eInt[2] = 0.0f;
  }

  // Apply feedback terms
  gx = gx + Kp * ex + Ki * eInt[0];
  gy = gy + Kp * ey + Ki * eInt[1];
  gz = gz + Kp * ez + Ki * eInt[2];
 
  // Integrate rate of change of quaternion
  pa = q2;
  pb = q3;
  pc = q4;
  q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * deltat);
  q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * deltat);
  q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * deltat);
  q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * deltat);

  // Normalise quaternion
  norm = sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4);
  norm = 1.0f / norm;
  q[0] = q1 * norm;
  q[1] = q2 * norm;
  q[2] = q3 * norm;
  q[3] = q4 * norm;
 
}


