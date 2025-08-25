#include "cordDecoder.h"

CordDecoder::CordDecoder(SerialInterface &serial,uint8_t numberOfReceivers, Unit unit):
                        serial(serial){
    //! Initialize all offsets and counts to 0 for all receivers
    for(int n=0;n<numberOfReceivers;n++){
        scaleFactors.push_back({0,0});
        recvCounts.push_back(0);
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

int CordDecoder::serialRecvCounts(){
    std::string recvLine;
    int bytesRecv = 0;
    uint32_t count = 0;

    // Get the tick values from the receivers
    bytesRecv = serial.readLineSer(recvLine);
    if( bytesRecv < 0) {
        //!  If the we fail to read a line, print an error message
        std::cerr << "Read line function failed in Cord Detect" << std::endl;
        return -1;
    }else if( bytesRecv == 0 ){
        return 0;
    }

    //! Get the count that was just read for the receiver
    //! Checking here for a valid count value may be advisable
    count = atoi(recvLine.c_str());
    recvCounts[0] = count;

    return bytesRecv;
}

int CordDecoder::setOffset(uint8_t receiver, double a, double b){
    if( !isReceiverOk(receiver) ){
        //! Receiver index is out of bounds or unusable
        return -1;
    }
    scaleFactors[receiver].a = a;
    scaleFactors[receiver].b = b;
    prevDistance = 0;
    return 0;
}

double CordDecoder::getDistance(uint8_t receiver){
    int bytesRecv = 0;
    uint32_t recvCount = 0;

    if( !isReceiverOk(receiver) ){
     //! Receiver index is out of bounds or unusable
     return -1;
    }

    // Get the tick value from the receiver
    bytesRecv = serialRecvCounts();
    if( bytesRecv < 0) {
        std::cout << "Failed to calculate distance to: " << receiver << std::endl;
        return -1;
    }else if( bytesRecv == 0 ){
        //! If the line received is empty, skip it
        return prevDistance;
    }

    // Get the count that was just read for the receiver
    recvCount = recvCounts[0];

    //! Calculate the distance from the transmitter to the sensor using
    //! m  = Ax+B (m is the distance, A is the scaling factor, and B is the linear offset) 
    double distance =  ( (scaleFactors[receiver].a * recvCount) + scaleFactors[receiver].b);

   return distance;
}

uint8_t CordDecoder::getReceiverCount(){
    return scaleFactors.size();
}

int CordDecoder::calcReceiverOffset(uint8_t receiver, uint16_t numReads, double knownDistance1, double knownDistance2){
    std::cout << "Experimental!" << std::endl;
    std::cout << "Receiver: " << receiver << " number of reads: " << numReads << std::endl;
    std::cout << "Known distance 1: " << knownDistance1 << " Known distance 2: " << knownDistance2 << std::endl;


    int bytesRecv = 0;
    //! Number of valid reads at a certain known distance
    int validReads = 0; 
    //! Average counts for distance 1
    int avgCounts1 = 0;
    //! Average counts for distance 2
    int avgCounts2 = 0;


    if( !isReceiverOk(receiver) ){
     //! Receiver index is out of bounds or unusable
     return -1;
    }
    
    if(numReads == 0){
        std::cerr << "There needs to be at least 1 reading for calibration" << std::endl;
        return -1;
    }

    do{
        bytesRecv = serialRecvCounts();
        if(bytesRecv<0){
            //! Fail on serial error
            std::cerr << "Failed to calibrate, serial error" << std::endl;
        } else if(bytesRecv == 0){
            //! Skip empty lines
            std::cout << "empty" << std::endl;
            continue;
        }

        //! Get the current count
        int count = recvCounts[receiver];
        std::cout << count << std::endl;
        avgCounts1+=count;
        validReads++;

    }while( validReads < numReads );
    //! Reset the number of valid reads
    validReads = 0;

    //! Now we need to move the tower to another known distance
    std::cout << "Move tower to distance 2, press enter when done" << std::endl;
    std::string dummy;
    std::getline(std::cin, dummy);  // Read the input into dummy

    do{
        bytesRecv = serialRecvCounts();
        if(bytesRecv<0){
            //! Fail on serial error
            std::cerr << "Failed to calibrate, serial error" << std::endl;
        } else if(bytesRecv == 0){
            //! Skip empty lines
            std::cout << "empty" << std::endl;
            continue;
        }

        //! Get the current count
        int count = recvCounts[receiver];
        std::cout << count << std::endl;
        avgCounts2+=count;
        validReads++;

    }while( validReads < numReads );

    //! Calculate the scaling offset (A) then calculate B
    double a = (knownDistance1 - knownDistance2)/(avgCounts1-avgCounts2);
    double b = (knownDistance1-(a*avgCounts1));

    scaleFactors[receiver].a = a;
    scaleFactors[receiver].b = b;

    std::cout << "Scale factor A = " << a << std::endl;
    std::cout << "Scale factor B = " << b << std::endl;

    return 0;
}
