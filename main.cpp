#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"


#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <iostream>

struct Point { double x, y; };

Point solve2x2(const double M[2][2], const double v[2], bool &ok) {
    double det = M[0][0]*M[1][1] - M[0][1]*M[1][0];
    ok = std::fabs(det) > 1e-14;
    if (!ok) return {0.0, 0.0};
    double inv00 =  M[1][1]/det;
    double inv01 = -M[0][1]/det;
    double inv10 = -M[1][0]/det;
    double inv11 =  M[0][0]/det;
    return { inv00*v[0] + inv01*v[1], inv10*v[0] + inv11*v[1] };
}

// returns an initial guess for (x,y)
Point initialGuessFromRanges(const std::vector<Point> &anchors,
                             const std::vector<double> &ranges)
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

// Compute residuals and Jacobian for given (x,y)
void computeResidualsAndJacobian(
    const Point &p,
    const std::vector<Point> &anchors,
    const std::vector<double> &ranges,
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

// Levenberg–Marquardt optimization
Point levenbergMarquardt(
    const std::vector<Point> &anchors,
    const std::vector<double> &ranges,
    Point initialGuess,
    int maxIters = 100,
    double tau = 1e-3,
    double eps1 = 1e-8,
    double eps2 = 1e-8)
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


//! Serial port name of the receiving microcontroller
const char* port_name = "/dev/ttyACM0"; 
//! Total number of receivers in the system
const uint8_t numberOfReceivers = 3;
//! Array of two calibration distances for each receiver
const uint16_t calDistances[numberOfReceivers][2] = {0};

int main() {
    
    //! Instantiate serial device
    Serial rxDev(port_name,B19200); // Create the receiving device (Arduino)
    //! TODO Set a flag when read module is initiated
    if( rxDev.begin() < 0 ){

        //! Test code
        std::vector<Point> anchors = {{0,0},{10,0},{10,8}};
        Point truePos {4.2, 3.1};
        std::vector<double> ranges;

        // simulate noiseless ranges
        for (auto &a : anchors) {
            double dx = truePos.x - a.x;
            double dy = truePos.y - a.y;
            ranges.push_back(std::sqrt(dx*dx + dy*dy));
        }

        Point guess = initialGuessFromRanges(anchors, ranges);
        std::cout << "Initial guess: (" << guess.x << ", " << guess.y << ")\n";

        Point est = levenbergMarquardt(anchors, ranges, guess);

        std::cout << "Estimated position: (" << est.x << ", " << est.y << ")\n";

        return -1;
    }

    //! Instantiate decoder class
    CordDecoder decoder(rxDev, numberOfReceivers, Unit::cm);
    //decoder.calcReceiverOffset(0,50,12.0,30.0);
    
    decoder.setOffset(0,0.034375,-63.875);
    decoder.setOffset(1,0.034375,-63.875);
    decoder.setOffset(2,0.034375,-63.875);

    std::cout << "Starting program..." << std::endl;
    while(true){
        //std::cout << "Decoder class distance: " << decoder.getDistance(0) << " cm" << std::endl;
        decoder.calcDistances();
        decoder.printDistances();
    }
    
}
