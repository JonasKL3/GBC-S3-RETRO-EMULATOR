#pragma once
class GamePadClass {public: bool isLeftPressed(){return false;} bool isRightPressed(){return false;} bool isUpPressed(){return false;} bool isDownPressed(){return false;} int getXaxisData(){return 0;} int getYaxisData(){return 0;} bool isCrossPressed(){return false;} bool isCirclePressed(){return false;} bool isSquarePressed(){return false;} bool isTrianglePressed(){return false;} bool isSelectPressed(){return false;} bool isStartPressed(){return false;}};
extern GamePadClass GamePad;
class DabbleClass {public: void begin(const char*){} void processInput(){}};
extern DabbleClass Dabble;
