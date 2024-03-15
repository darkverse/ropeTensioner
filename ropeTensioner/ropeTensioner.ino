/*      Rope Tensioner
 *       
 *      Release history:
 *      rev0 - 03apr2017 initial release
 *             1. fully functional
 *             2. left button moves cursor for editing load set point
 *             3. last cell in first row of lcd displays asterisk when in pause state
 *      rev1 - 09apr2017
 *             1. added more verbose comments
 *      rev2 - 14apr2017
 *             1. added an autopause fail safe so that if a sudden loss of load is detected
 *                the system will be forced to the pause state to turn the relay (and thust the winch) off.
 *                Autopause is only armed after a certain minimum load (AUTOPAUSE_ARM_THRESHOLD) is measured.  Once
 *                the autopause is armed, if the meausured load falls below a certain value (AUTOPAUSE_THRESHOLD),
 *                the system will automatically move to the pause state.  Once in the pause state, an operator
 *                is required to push the pause button to unpause the system.  That is, the autopause feature
 *                will automatically pause the system, but will not automatically unpause it.
 *
 *      This program was written by Chris Hassan for Andrew Bibby.  It uses
 *      the following libraries (original or modified versions) and I
 *      readily give all credit that is due to the creators and owners
 *      of these libraries:
 *      1. LiquidCrystal (default Arduino)
 *      2. HX711
 *      This program is covered by the GNU General Public License. 
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 */ 

#include "LcdWBtns.h"
#include "HX711.h"

#define HX711_SCALE_FACTOR        684  //expected to be adcCounts/kg  THIS NEEDS TO BE SET TO THE VALUE FOR YOUR SYSTEM (once it's known)
#define NUM_TQ_POINTS_TO_AVG      3     //sets the number of torque readings to average
#define AUTOPAUSE_ARM_THRESHOLD   51    //defines minimum load, in kg, that muse be seen to arm the autopause feature
#define AUTOPAUSE_THRESHOLD       50    //defines the load, in kg, at which the autopause forces the system to the pause state, if autopause is armed

//The ON and OFF definitions allow relays to be turned ON and OFF instead of HIGH and LOW.  This
//is useful because regardless of the logic level required to turn the relay ON and OFF, we just 
//define it correctly here as HIGH or LOW, and then still turn the relay ON and OFF, which is easier
//than trying to remember if we need to set the relay HIGH to turn it ON or LOW to turn it ON.  
#define RELAY_PIN       13    //the Arduino output pin that the realy is connected to           
#define RELAY_ON        HIGH  //so we can turn relays "ON" instead of turning them "HIGH"
#define RELAY_OFF       LOW   //so we can turn relays "OFF" instead of turning them "LOW"

#define HX711_CLK_PIN   2     //clock pin for serial data out of the hx711
#define HX711_DOUT_PIN  3     //data out pin for serial data out of the hx711

//redefining the names of the LCD buttons to names that are more meaningful for this application
#define BTN_TARE            BTN_RIGHT   //the "RIGHT" button on the LCD is to tare the system
#define BTN_LOAD_UP         BTN_UP      //the "UP" button on the LCD increases the currently selected digit of the target load
#define BTN_LOAD_DN         BTN_DOWN    //the "DOWN" button on the LCD decreases the currently selected digit of the target load 
#define BTN_EDIT_COL_SEL    BTN_LEFT    //the "LEFT" button shifts the currently selected digit to edit to the left (and wraps around)
#define BTN_PAUSE           BTN_SELECT  //forces the relay to be de-energized and doesn't allow the program to re-energize it

//create the LCD object and the hx711 software objects
CLcdWBtns *pLcdWBtns = new CLcdWBtns(8, 9, 4, 5, 6, 7); //creates an instance of the "LCD with Buttons" class (CLcdWBtns)
HX711 *pHx711 = new HX711(HX711_DOUT_PIN, HX711_CLK_PIN); //creates an instance of the HX711 load cell amp

//create some variables to be used throughout the program
int tTskReadLoadCell = 0;
uint8_t ucLcdEditPos = 11;
unsigned int unLoadSetPoint = 0;
unsigned int ucarLoadValue[4] = {0};
float fMeasuredLoad = 0.0f;
bool bAutoPauseArmed = false;

enum eState {INIT, TARE, RUNNING, PAUSE}; //these are the states that the system can be in
eState state;

//setup is used to get the system setup so that it functions predictably when it starts
void setup()
{
    //Serial.begin(9600);
    pHx711->set_scale(HX711_SCALE_FACTOR); //sets the HX711 scale factor to whatever is in the #define HX711_SCALE_FACTOR (this has to be set for your system)
    digitalWrite(RELAY_PIN, RELAY_OFF); //make sure the relay pin on the Arduino is OFF
    pinMode(RELAY_PIN, OUTPUT); //set the relay pin on the Arduino to an output pin
    state = INIT; //start in the INIT state (INITialize)
}

unsigned long int displaySetPtTimer = millis(); //timer to decide when to display the load target
unsigned long int displayMeasuredTimer = millis(); //timer to decide when to display the measured load
void loop() //this is the "main" loop; it's where the program starts running, and it runs as long as the board is powered up
{
    switch(state)
    {
        case INIT:
            state = TARE; //INIT is currently only used to put us in the TARE state so the load offset will be captured
            break;
        case TARE:
            Tare(); //capture the load offset of the system (i.e. force a zero reference to be set at the current load value)
            pLcdWBtns->cursor(); //display the cursor (the underscore) on the LCD
            break;
        case RUNNING: //system is measuring and displaying the load and turning relay on an off as needed
            fMeasuredLoad = pHx711->get_units(NUM_TQ_POINTS_TO_AVG);  //reads the load cell in kg
            if(fMeasuredLoad >= AUTOPAUSE_ARM_THRESHOLD)
              bAutoPauseArmed = true;
            if(fMeasuredLoad < AUTOPAUSE_THRESHOLD && bAutoPauseArmed)
            {
              bAutoPauseArmed = false;
              state = PAUSE;
            }
            if(fMeasuredLoad < unLoadSetPoint * 0.95) //if at less than 95% of the target load
                digitalWrite(RELAY_PIN, RELAY_ON);  //turn the relay ON
            else
                digitalWrite(RELAY_PIN, RELAY_OFF); //if measured load is at least 95% of the target, turn the relay off
            if(millis() - displaySetPtTimer > 1000) //display the target load information every 1000ms (i.e. 1sec)
            {            
                DisplayLoadTarget();  //display the target load information
                displaySetPtTimer = millis(); //reset the target load display timer
            }
            if(millis() - displayMeasuredTimer > 300) //display the measured load information every 300ms (i.e. 0.3sec)
            {
                DisplayMeasuredLoad();  //dispaly the measured load information
                displayMeasuredTimer = millis(); //reset the measured load display timer
            }
            break;
        case PAUSE: //relay is forced to stay off, and system is measuring and displaying the load 
            digitalWrite(RELAY_PIN, RELAY_OFF); //turn the relay off
            fMeasuredLoad = pHx711->get_units(NUM_TQ_POINTS_TO_AVG); //read the load cell
            if(millis() - displaySetPtTimer > 1000) //display target load information every 1000ms (i.e. 1sec)
            {            
                DisplayLoadTarget();  //display the target load information
                displaySetPtTimer = millis(); //reset the target load display timer
            }
            if(millis() - displayMeasuredTimer > 300) //display the measured load information every 300ms (i.e. 0.3sec) 
            {
                DisplayMeasuredLoad();  //display the measured load information
                displayMeasuredTimer = millis();  //reset the measured load display timer
            }
            break;
    }
    pLcdWBtns->GetPressedBtn(ButtonDownAndUp, ButtonDown, ButtonUp);  //check for button presses
}

void ButtonDown(uint8_t nPressedBtn)  //not currently being used
{}

void ButtonUp(uint8_t nReleasedBtn) //not currently being used
{}

//called if a button is pressed and released, and performs actions based on which button was pressed and released
void ButtonDownAndUp(uint8_t nPressedReleasedBtn)
{
    switch(nPressedReleasedBtn)
    {
        case BTN_EDIT_COL_SEL:  //moves the cursor from one editable column to the next and wraps around when it reaches the end
            ucLcdEditPos = (8 == ucLcdEditPos ? 11 : ucLcdEditPos - 1); //lowest index value = most sig digit for lcd and value array
            pLcdWBtns->setCursor(ucLcdEditPos,0); //moves the "edit curor" (i.e. the underscore) to the selected digit
            break;
        case BTN_LOAD_UP: //increases the number in the currently selected digit for the target load
            if(8 == ucLcdEditPos) //this is the "thousands" column of the target load
            {
                if(5 == ucarLoadValue[ucLcdEditPos-8]) //target load is editable to 5000, so if "thousands" column is 5 and the up button was pressed
                    ucarLoadValue[ucLcdEditPos-8] = 0; //set the "thousands" value back to zero 
                else
                    ucarLoadValue[ucLcdEditPos-8]++; //increase the thousands column digit by 1
            }
            else
            {
                if(9 == ucarLoadValue[ucLcdEditPos-8]) //if it's any digit besides "thousands", 9 is the highest value and...
                    ucarLoadValue[ucLcdEditPos-8] = 0; //...if it's 9 and up button is pressed, set it back to zero
                else
                    ucarLoadValue[ucLcdEditPos-8]++; //increase the currently select digit by 1
            }
            pLcdWBtns->setCursor(ucLcdEditPos,0); //LCD automatically moves cursor to next position, so force it back to the column we're editing

            //combine the individual target load digits into one number
            unLoadSetPoint = ucarLoadValue[0] * 1000; //thousands
            unLoadSetPoint += ucarLoadValue[1] * 100; //plus hundreds
            unLoadSetPoint += ucarLoadValue[2] * 10;  //plus tens
            unLoadSetPoint += ucarLoadValue[3];       //plus ones
            DisplayLoadTarget();  //display the target load value on the LCD
            break;
        case BTN_LOAD_DN: //decreases the number in the currently selected digit for the target load
            if(8 == ucLcdEditPos) //this is the "thousands" column of the target load
            {
                if(0 == ucarLoadValue[ucLcdEditPos-8]) //target load max is 5000, so if thousands is 0 and down button pressed...
                    ucarLoadValue[ucLcdEditPos-8] = 5; //...set thousands to 5
                else
                    ucarLoadValue[ucLcdEditPos-8]--; //decrease thousands column by 1
            }
            else
            {
                if(0 == ucarLoadValue[ucLcdEditPos-8]) //any column besides thousands has max value of 9 so if it's 0 and down button is pressed
                    ucarLoadValue[ucLcdEditPos-8] = 9; //set it back to 9
                else
                    ucarLoadValue[ucLcdEditPos-8]--; //decrease currently selected digit by 1
            }
            pLcdWBtns->setCursor(ucLcdEditPos,0); //LCD automatically moves cursor to next position, so force it back to the column we're editing

            //combine the individual target load digits into one number
            unLoadSetPoint = ucarLoadValue[0] * 1000;
            unLoadSetPoint += ucarLoadValue[1] * 100;
            unLoadSetPoint += ucarLoadValue[2] * 10;
            unLoadSetPoint += ucarLoadValue[3];
            DisplayLoadTarget();  //display the target load value on the LCD      
            break;
        case BTN_PAUSE: 
            if(PAUSE == state)  //if the system was already in the PAUSE state and the pause button was pressed...
            {
                pLcdWBtns->setCursor(15,0); 
                pLcdWBtns->print(" ");  //...clear the asterisk (i.e. pause indicator) from the LCD
                state = RUNNING;  //move the system to the RUNNING state
            }
            else //otherwise, if the system was not in the PAUSE state and the PAUSE button was pressed...
            {
                pLcdWBtns->setCursor(15,0);
                pLcdWBtns->print("*");  //...display the asterisk (i.e. "pause indicator") on the LCD
                state = PAUSE;  //move the system to the PAUSE state
            }
            break;
        case BTN_TARE:  //if the tare button was pressed
            pLcdWBtns->clear(); //clear the LCD
            pLcdWBtns->cursor(); //display the cursor on the LCD
            state = TARE; //move the system to the TARE state
            break;
    }
}

//forces the current measured load value to be zero regardless of the actual load present relative to a true 0kg load
void Tare()
{
    pLcdWBtns->setCursor(0,0);
    pLcdWBtns->print("TARING..."); //display on the LCD to let the user know what's happening
    pLcdWBtns->setCursor(0,1);
    pLcdWBtns->print("2 sec delay");
    delay(2000); //delay for 2 seconds (to allow the system to "stabilize")
    pHx711->tare(); //tell the HX711 to caputre it's offset
    state = RUNNING; //move the system back to the RUNNING state
}

//displays the measured load on the LCD
void DisplayMeasuredLoad()
{
    char cBuf[17];
    pLcdWBtns->setCursor(0,1);
    sprintf(cBuf, "Meas:   %04dkg  ",(unsigned int)fMeasuredLoad);
    pLcdWBtns->print(cBuf);
    pLcdWBtns->setCursor(ucLcdEditPos,0);
}

//displays the target load on the LCD
void DisplayLoadTarget()
{
    char cBuf[17];
    pLcdWBtns->setCursor(0,0);
    sprintf(cBuf, "Set Pt: %04dkg", unLoadSetPoint);
    pLcdWBtns->print(cBuf);
    pLcdWBtns->setCursor(ucLcdEditPos,0);    
}
