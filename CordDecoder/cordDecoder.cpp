#include "cordDecoder.h"

CordDecoder::CordDecoder(SerialInterface &serial,uint8_t numberOfReceivers, Unit unit):
                        serial(serial){
    //! Initialize all offsets to 0 for all receivers
    for(int n=0;n<numberOfReceivers;n++){
        scaleFactors.push_back({0,0});
    }
    //! Store the unit type we are using
    this->unit = unit;
}

bool CordDecoder::isReceiverOk(uint8_t receiver){
    //! Check if the receiver index is out of bounds
    if(receiver > (scaleFactors.size()-1) ){
        return false;
    }
    //! Future implementations may include other checks

    return true;
}

int CordDecoder::setOffset(uint8_t receiver, double a, double b){
    if( !isReceiverOk(receiver) ){
        //! Receiver index is out of bounds or unusable
        return -1;
    }
    scaleFactors[receiver].a = a;
    scaleFactors[receiver].b = b;
    return 0;
}


double CordDecoder::getDistance(uint8_t receiver){
    if( !isReceiverOk(receiver) ){
        //! Receiver index is out of bounds or unusable
        return -1;
    }
   //! Calculate the distance from the transmitter to the sensor using
   //! m  = Ax+B (m is the distance, A is the scaling factor, and B is the linear offset) 
   //double distance = 

   return 0;
}

uint8_t CordDecoder::getReceiverCount(){
    return scaleFactors.size();
}

int CordDecoder::calcReceiverOffset(uint8_t receiver){
    std::cout << "Function not implemented yet..." << std::endl;

    return 0;
}
