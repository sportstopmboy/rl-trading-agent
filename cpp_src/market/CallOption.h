#ifndef CALL_OPTION_H
#define CALL_OPTION_H

#include <string>

class CallOption
{
private:
    // Date Variables
    std::string quoteDate;
    std::string expirationDate;

    // Core Pricing Inputs
    double S;             // The SPX price (S)
    double K;             // The Strike price (K)
    int daysToExpiration; // Days to Expiration of Call Option

    // Market Pricing
    double bid; // The highest price a buyer is willing to pay
    double ask; // The lowest price a seller is willing to sell at
    int volume; // The number of contracts trading

    // The Greeks and Volatility
    double impliedVolatility; // Annualized percentage of expected future price fluctuations
    double delta;             // Measures the option's sensitivity to the underlying assets price
    double gamma;             // The second derivative of price - it measures the rate of change of delta
    double vega;              // Measures the option's sensitivity to IV
    double theta;             // Measures the optoin's sensitivity to time decay
    double rho;               // Measures teh option's sensitivity to changes in the risk-free interest rate

public:
    // Constructor
    CallOption(std::string quoteDate, std::string expirationDate, double underlyingLast,
               double strike, int daysToExpiration, double bid, double ask, int volume,
               double impliedVolatility, double delta, double gamma, double vega, double theta, double rho);

    // This function is the Black-Scholes Equation
    // It returns C - the theoretical price of the option assuming Brownian geometric motion
    double calculateTheoreticalPrice(double interestRate, double historicalVolatility) const;

private:

    // Computes the cumulative standard normal distribution N(x)
    static double calculateNx(double x);

public:
    // Calculates the fair price of an option
    // Is the average of the ask and bid price
    double calculateMidPrice() const;

    // Getters
    const std::string& getQuoteDate() const;
    const std::string& getExpirationDate() const;
    double getUnderlyingLast() const;
    double getStrike() const;
    int getDaysToExpiration() const;
    double getBid() const;
    double getAsk() const;
    int getVolume() const;
    double getImpliedVolatility() const;
    double getDelta() const;
    double getGamma() const;
    double getVega() const;
    double getTheta() const;
    double getRho() const;
};

#endif