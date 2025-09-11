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
        std::cerr << "receiver:" << receiver << "is out of bounds" << std::endl;
        return false;
    }else{
        //! Future implementations may include other checks
        return true;
    }
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

int CordDecoder::getHistogramCounts(uint8_t numReads, std::vector<uint32_t>& validCounts){
    //! Dummy string for "storing" user input (used to flush serial buffer)
    std::string dummy;
    //! Bytes received from serial bus
    int bytesRecv = 0;
    //! Stores the highest number of valid reads inside each histogram
    //! Index corresponds to receiver (0 -> receiver 1, etc)
    std::vector<uint32_t> maxValidReads;
    //! Stores the histogram for all receivers
    //! Key: Value read, pair: frequency
    std::vector<std::unordered_map<uint32_t, uint32_t>> receiverHistograms;

    if(numReads<=0){
        std::cerr << "Number of reads needs to be greater than 0" << std::endl;
    }

    for(int x=0; x < getReceiverCount(); x++){
        if(!isReceiverOk(x)){
            //! Receiver index is out of bounds or unusable
            std::cout << "receiver error " << x << std::endl;
            return -1;
        }else{
            //! Add empty inner vectors
            maxValidReads.push_back(0);
            receiverHistograms.emplace_back();
        }
    }

    //! Clear the read buffer
    serial.clearReadBuffer(); 
    //! Skip the first line after buffer was cleared
    serial.readLineSer(dummy);
    
    //! Receive counts and store them in the histogram
    //! TODO: throw away counts that are too far the median
    bool doneFlag = false;
    do{
        //! Receive a serial line
        bytesRecv = serialRecvCounts();
        if(bytesRecv<0){
            //! Fail on serial error
            std::cerr << "Failed to calibrate, serial error" << std::endl;
        } else if(bytesRecv == 0){
            //! Skip empty lines
            std::cout << "empty " << std::endl;
            continue;
        }
        std::cout << "received" << std::endl;
        for(auto toa : recvCounts){
            std::cout << " " << toa;
        }
        std::cout << std::endl;

        //! Make the histogram for each receiver
        for(int x=0;x < getReceiverCount();x++){    
            //! TODO: Decide if, when implemented, getReceiverCount filtering is enought
            //! For now use every count that is received
            if(receiverHistograms[x].count(recvCounts[x])){
                // If the key (count) exists increment it
                receiverHistograms[x][recvCounts[x]] += 1;
                uint32_t readCount = receiverHistograms[x][recvCounts[x]];
                if(readCount > maxValidReads[x]){
                    //! Store the maximum read count of any tick value
                    maxValidReads[x] = readCount;
                }
            }else{
                //! Create an entry for that particular tick count
                receiverHistograms[x][recvCounts[x]] = 1;
            }

            //! Check if we have received the minimum valid reads for all
            //! receivers, if so exit the loop
            if( maxValidReads[x] >= numReads ){
                doneFlag = true;
            }else{
                doneFlag = false;
                std::cout << "not ready" << std::endl;
            }
        }


    }while(!doneFlag);

    //! For debugging purposes
    uint32_t index = 0;
    for(auto histogram : receiverHistograms){
        std::cout << "Histogram contents: " << index << std::endl;
        uint32_t maxFreq = 0;
        uint32_t tickCnt = 0;
        for(auto key: histogram){
            std::cout << key.first << " " << key.second << std::endl;
            if(key.second > maxFreq){
                maxFreq = key.second;
                tickCnt = key.first;
            }
        }
        std::cout << "Max tickCnt: " << tickCnt << " max frequency: " << maxFreq << std::endl;
        validCounts.push_back(tickCnt);
        index++;
    }

    return 0;
}

int CordDecoder::calcSingleReceiverOffset(uint8_t receiver, uint16_t numReads, double knownDistance1, double knownDistance2){
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
    //bool skipFirstLine = false;

    if( !isReceiverOk(receiver) ){
     //! Receiver index is out of bounds or unusable
     return -1;
    }
    
    if(numReads <= 0){
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


CordDecoder::Point CordDecoder::solve2x2(const double M[2][2], const double v[2], bool &ok) {
    double det = M[0][0]*M[1][1] - M[0][1]*M[1][0];
    ok = std::fabs(det) > 1e-14;
    if (!ok) return {0.0, 0.0};
    double inv00 =  M[1][1]/det;
    double inv01 = -M[0][1]/det;
    double inv10 = -M[1][0]/det;
    double inv11 =  M[0][0]/det;
    return { inv00*v[0] + inv01*v[1], inv10*v[0] + inv11*v[1] };
}

CordDecoder::Point CordDecoder::initialGuessFromRanges(std::vector<Point> &anchors,
                             std::vector<double> &ranges)
{
    size_t N = anchors.size();
    if (N == 0) return {0.0,0.0};
    if (N == 1) return anchors[0];

    // If only 2 anchors: return midpoint of circle intersections approx -> use midpoint of anchors
    if (N == 2) return { (anchors[0].x + anchors[1].x)/2.0,
                         (anchors[0].y + anchors[1].y)/2.0 };

    // Build A (N-1 x 2) and b (N-1) using anchor 0 as reference
    // Normal equations: ATA * p = ATb
    double ATA[2][2] = {{0,0},{0,0}};
    double ATb[2] = {0,0};

    const Point &a1 = anchors[0];
    double d1 = ranges[0];

    for (size_t i = 1; i < N; ++i) {
        double xi = anchors[i].x;
        double yi = anchors[i].y;
        double di = ranges[i];

        double Ai0 = -2.0 * (xi - a1.x);
        double Ai1 = -2.0 * (yi - a1.y);

        double bi = di*di - d1*d1 - xi*xi + a1.x*a1.x - yi*yi + a1.y*a1.y;

        // accumulate ATA = A^T A
        ATA[0][0] += Ai0 * Ai0;
        ATA[0][1] += Ai0 * Ai1;
        ATA[1][0] += Ai1 * Ai0;
        ATA[1][1] += Ai1 * Ai1;

        // accumulate ATb
        ATb[0] += Ai0 * bi;
        ATb[1] += Ai1 * bi;
    }

    // Check conditioning of ATA
    double condDet = ATA[0][0]*ATA[1][1] - ATA[0][1]*ATA[1][0];
    double trace = std::fabs(ATA[0][0]) + std::fabs(ATA[1][1]);
    bool ok;
    Point p = solve2x2(ATA, ATb, ok);

    if (!ok || std::fabs(condDet) < 1e-12 * trace) {
        // Ill-conditioned: try Tikhonov regularization (tiny)
        double lambda = 1e-6 * (trace + 1e-12);
        double ATAreg[2][2] = {{ ATA[0][0] + lambda, ATA[0][1] },
                               { ATA[1][0], ATA[1][1] + lambda }};
        p = solve2x2(ATAreg, ATb, ok);
    }

    if (ok) {
        return p;
    }

    // Fallback: weighted centroid with weight = 1/(d + eps)
    double eps = 1e-6;
    double wx = 0.0, wy = 0.0, wsum = 0.0;
    for (size_t i=0;i<N;++i) {
        double w = 1.0 / (ranges[i] + eps);
        wx += w * anchors[i].x;
        wy += w * anchors[i].y;
        wsum += w;
    }
    if (wsum == 0.0) return {0.0,0.0};
    return { wx/wsum, wy/wsum };
}

void CordDecoder::computeResidualsAndJacobian(
    Point &p,
    std::vector<Point> &anchors,
    std::vector<double> &ranges,
    std::vector<double> &residuals,
    std::vector<std::array<double,2>> &J)
{
    size_t N = anchors.size();
    residuals.resize(N);
    J.resize(N);

    for (size_t i=0; i<N; i++) {
        double dx = p.x - anchors[i].x;
        double dy = p.y - anchors[i].y;
        double dist = std::sqrt(dx*dx + dy*dy);
        if (dist < 1e-12) dist = 1e-12; // avoid divide by zero

        residuals[i] = dist - ranges[i];
        J[i][0] = dx / dist;  // ∂r/∂x
        J[i][1] = dy / dist;  // ∂r/∂y
    }
}

CordDecoder::Point CordDecoder::levenbergMarquardt(
     std::vector<Point> &anchors,
     std::vector<double> &ranges,
    CordDecoder::Point initialGuess,
    int maxIters,
    double tau,
    double eps1,
    double eps2)
{
    Point p = initialGuess;
    size_t N = anchors.size();

    std::vector<double> residuals;
    std::vector<std::array<double,2>> J;

    computeResidualsAndJacobian(p, anchors, ranges, residuals, J);

    // Compute cost
    double cost = 0.0;
    for (double r : residuals) cost += 0.5 * r * r;

    // Compute J^T J and J^T r
    double A[2][2] = {{0,0},{0,0}};
    double g[2] = {0,0};
    for (size_t i=0; i<N; i++) {
        A[0][0] += J[i][0]*J[i][0];
        A[0][1] += J[i][0]*J[i][1];
        A[1][0] += J[i][1]*J[i][0];
        A[1][1] += J[i][1]*J[i][1];
        g[0] += J[i][0]*residuals[i];
        g[1] += J[i][1]*residuals[i];
    }

    // Init damping
    double mu = tau * std::max(A[0][0], A[1][1]);
    double v = 2.0;

    for (int iter=0; iter<maxIters; iter++) {
        // Solve (A + mu I) h = -g  (2x2 system)
        double H[2][2] = {
            {A[0][0] + mu, A[0][1]},
            {A[1][0], A[1][1] + mu}
        };
        double det = H[0][0]*H[1][1] - H[0][1]*H[1][0];
        if (std::fabs(det) < 1e-15) break; // singular

        double invH[2][2];
        invH[0][0] = H[1][1]/det;
        invH[0][1] = -H[0][1]/det;
        invH[1][0] = -H[1][0]/det;
        invH[1][1] = H[0][0]/det;

        double h[2];
        h[0] = -(invH[0][0]*g[0] + invH[0][1]*g[1]);
        h[1] = -(invH[1][0]*g[0] + invH[1][1]*g[1]);

        if (std::sqrt(h[0]*h[0] + h[1]*h[1]) <= eps2*(std::sqrt(p.x*p.x+p.y*p.y)+eps2))
            break;

        // Candidate update
        Point p_new {p.x + h[0], p.y + h[1]};
        std::vector<double> r_new;
        std::vector<std::array<double,2>> J_new;
        computeResidualsAndJacobian(p_new, anchors, ranges, r_new, J_new);

        double cost_new = 0.0;
        for (double r : r_new) cost_new += 0.5 * r * r;

        double rho_denom = 0.5 * (h[0]*(mu*h[0]-g[0]) + h[1]*(mu*h[1]-g[1]));
        double rho = (cost - cost_new) / (rho_denom == 0 ? 1e-12 : rho_denom);

        if (rho > 0) {
            // Accept update
            p = p_new;
            residuals = r_new;
            J = J_new;
            cost = cost_new;

            // Recompute A and g
            A[0][0]=A[0][1]=A[1][0]=A[1][1]=0.0;
            g[0]=g[1]=0.0;
            for (size_t i=0; i<N; i++) {
                A[0][0] += J[i][0]*J[i][0];
                A[0][1] += J[i][0]*J[i][1];
                A[1][0] += J[i][1]*J[i][0];
                A[1][1] += J[i][1]*J[i][1];
                g[0] += J[i][0]*residuals[i];
                g[1] += J[i][1]*residuals[i];
            }

            mu *= std::max(1.0/3.0, 1.0 - std::pow(2*rho-1,3));
            v = 2.0;
        } else {
            mu *= v;
            v *= 2.0;
        }

        if (std::sqrt(g[0]*g[0]+g[1]*g[1]) < eps1) break;
    }

    return p;
}


void CordDecoder::getPosition(){
    //! Test code

    // simulate noiseless ranges
    //for (auto &a : anchors) {
    //    double dx = truePos.x - a.x;
    //    double dy = truePos.y - a.y;
    //    ranges.push_back(std::sqrt(dx*dx + dy*dy));
    //}

    CordDecoder::Point guess = initialGuessFromRanges(anchors, recvDistances);
    std::cout << "Initial guess: (" << guess.x << ", " << guess.y << ")\n";

    CordDecoder::Point est = levenbergMarquardt(anchors, recvDistances, guess);

    std::cout << "Estimated position: (" << est.x << ", " << est.y << ")\n";
}

