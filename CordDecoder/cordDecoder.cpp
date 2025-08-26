#include "cordDecoder.h"

CordDecoder::CordDecoder(SerialInterface &serial,uint8_t numberOfReceivers, Unit unit):
                        serial(serial){
    //! Initialize all offsets and counts to 0 for all receivers
    for(int n=0;n<numberOfReceivers;n++){
        scaleFactors.push_back({0,0});
        recvCounts.push_back(0);
        recvDistances.push_back(0);
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
    std::string token;
    int bytesRecv = 0;

    //! Get the tick values from the all the receivers
    //! The order that the count value is at corresponds to the receiver number
    //! Ex: XXXX(R0),XXXX(R1),... 
    bytesRecv = serial.readLineSer(recvLine);
    if( bytesRecv < 0) {
        //!  If the we fail to read a line, print an error message
        std::cerr << "Read line function failed in Cord Detect" << std::endl;
        return -1;
    }else if( bytesRecv == 0 ){
        return 0;
    }
   
    //! Get the various counts from all the receivers
    std::stringstream stream(recvLine);
    //! Only grab the receivers that we want
    for(int x=0;x<recvCounts.size();x++){
        std::getline(stream,token,delimiter);
        if(!token.empty()){
            try{
                recvCounts[x]  = std::stoi(token);
            }catch(std::invalid_argument const &ex){
                std::cerr << token << std::endl;
                std::cerr << "std::invalid_argument::what(): " << ex.what();
                recvCounts[x] = 0;
            }catch(std::exception const &ex){
                //! Catch all for any other error
                std::cerr << "Errro in decoder receive module " << ex.what() << std::endl;
                recvCounts[x] = 0;
            }
        }else{
            recvCounts[x] = 0;
        }
    }

    return bytesRecv;
}

int CordDecoder::setOffset(uint8_t receiver, double a, double b){
    if( !isReceiverOk(receiver) ){
        //! Receiver index is out of bounds or unusable
        return -1;
    }
    scaleFactors[receiver].a = a;
    scaleFactors[receiver].b = b;
    recvDistances[receiver] = 0;
    return 0;
}

uint8_t CordDecoder::getReceiverCount(){
    return scaleFactors.size();
}

void CordDecoder::printDistances(){
    for(auto dist : recvDistances){
        std::cout << dist << " ";
    }
    std::cout << std::endl;
}

int CordDecoder::calcReceiverOffset(uint8_t receiver, uint16_t numReads, double knownDistance1, double knownDistance2){
    std::cout << "Receiver: " << receiver << " number of reads: " << numReads << std::endl;
    std::cout << "Known distance 1: " << knownDistance1 << " Known distance 2: " << knownDistance2 << std::endl;

    int bytesRecv = 0;
    //! Number of valid reads at a certain known distance
    int validReads = 0; 
    //! Average counts for distance 1
    int avgCounts1 = 0;
    //! Average counts for distance 2
    int avgCounts2 = 0;
    //! Dummy string for "storing" user input
    std::string dummy;
    bool skipFirstLine = false;

    if( !isReceiverOk(receiver) ){
     //! Receiver index is out of bounds or unusable
     return -1;
    }
    
    if(numReads == 0){
        std::cerr << "There needs to be at least 1 reading for calibration" << std::endl;
        return -1;
    }
    
    //! Prompt to start the calibration process
    std::cout << "Move the tower to known distance 1 and press enter" << std::endl;
    std::getline(std::cin, dummy);  // Read the input into dummy

    //! Clear the read buffer
    serial.clearReadBuffer();
    //! Skip the first line after buffer was cleared
    serial.readLineSer(dummy);
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

    //! Calculate the average of the reads
    avgCounts1 = avgCounts1/numReads;
    //! Reset the number of valid reads
    validReads = 0;

    //! Now we need to move the tower to another known distance
    std::cout << "Move tower to distance 2, press enter when done" << std::endl;
    std::getline(std::cin, dummy);  // Read the input into dummy

    serial.clearReadBuffer();
    serial.readLineSer(dummy);
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

    //! Calculate the average of the reads
    avgCounts2 = avgCounts2/numReads;

    if(avgCounts1 == avgCounts2){
        std::cerr << "Invalid read run, is the sensor functional?" << std::endl;
        return -1;
    }

    //! Calculate the scaling offset (A) then calculate B
    double a = (knownDistance1 - knownDistance2)/(avgCounts1-avgCounts2);
    double b = (knownDistance1-(a*avgCounts1));

    scaleFactors[receiver].a = a;
    scaleFactors[receiver].b = b;

    std::cout << "Scale factor A = " << a << std::endl;
    std::cout << "Scale factor B = " << b << std::endl;
    std::getline(std::cin, dummy);  // Read the input into dummy

    return 0;
}

int CordDecoder::calcDistances(){
    int bytesRecv = 0;

    // Get the tick value from the receiver
    bytesRecv = serialRecvCounts();
    if( bytesRecv < 0) {
        std::cout << "Error in serial input from distance calculation " << std::endl;
        return -1;
    }else if( bytesRecv == 0 ){
        //! If the line received is empty, skip it
        return 0;
    }

    //! For every count that was received, calculate the distance using
    //! its corresponding scale factors
    for(int x=0;x<recvCounts.size();x++){
        recvDistances[x] = ( (scaleFactors[x].a * recvCounts[x]) + scaleFactors[x].b); 
    }

    return 0;
}
