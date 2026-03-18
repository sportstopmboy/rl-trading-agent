#include "cpp_src/market/CallOption.h"
#include <cmath>
#include <string>
#include <utility>

using namespace std;

// Constructor
CallOption::CallOption(string quoteDate, string expirationDate, double underlyingLast, double strike,
                       int daysToExpiration, double bid, double ask, int volume, double impliedVolatility,
                       double delta, double gamma, double vega, double theta, double rho)
    : quoteDate(move(quoteDate)),
      expirationDate(move(expirationDate)),
      S(underlyingLast),
      K(strike),
      daysToExpiration(daysToExpiration),
      bid(bid),
      ask(ask),
      volume(volume),
      impliedVolatility(impliedVolatility),
      delta(delta),
      gamma(gamma),
      vega(vega),
      theta(theta),
      rho(rho)
{
}

// This function is the Black-Scholes Equation
// It returns C - the theoretical price of the option assuming Brownian geometric motion
double CallOption::calculateTheoreticalPrice(double interestRate, double historicalVolatility) const
{
    // Clause to handle Call Option Expiration Day
    if (daysToExpiration == 0) return (S > K) ? (S - K) : 0.0;

    // Converts the days to expiration to the Black-Scholes equation input T
    // T represents the time until the option expires in years and not days
    double T = (double)daysToExpiration / 365.0;

    // Calculate d1
    // But first, protect against division by 0
    if (historicalVolatility <= 0.0) historicalVolatility = 0.0001;

    // Calculates the Black-Scholes equation parameter d1
    // d1 represents the sensitivity of the option price to changes in the underlying stock price
    // N(d1) = delta
    double d1 = (log(S / K) + (interestRate + historicalVolatility * historicalVolatility / 2.0) * T) 
                / (historicalVolatility * sqrt(T));
    
    // Calculates the Black-Scholes equation parameter d2
    // d2 represents the risk-adjusted probability that a call option will finish in-the-money (exercise probability)
    // N(d2) is the likelihood the option is worth anything by the end
    double d2 = d1 - historicalVolatility * sqrt(T);

    // Return C - the theoretical price of the option
    return S * calculateNx(d1) - K * exp(-interestRate * T) * calculateNx(d2);
}

// Computes the cumulative standard normal distribution N(x)
double CallOption::calculateNx(double x)
{
    // sqrt(2.0) is a constant, so the compiler optimizes this calculation instantly.
    return 0.5 * (1.0 + erf(x / sqrt(2.0)));
}

// Calculates the fair price of an option
// Is the average of the ask and bid price
double CallOption::calculateMidPrice() const
{
    return (bid + ask) / 2.0;
}

// Getters
const string& CallOption::getQuoteDate() const { return quoteDate; }

const string& CallOption::getExpirationDate() const { return expirationDate; }

double CallOption::getUnderlyingLast() const { return S; }

double CallOption::getStrike() const { return K; }

int CallOption::getDaysToExpiration() const { return daysToExpiration; }

double CallOption::getBid() const { return bid; }

double CallOption::getAsk() const { return ask; }

int CallOption::getVolume() const { return volume; }

double CallOption::getImpliedVolatility() const { return impliedVolatility; }

double CallOption::getDelta() const { return delta; }

double CallOption::getGamma() const { return gamma; }

double CallOption::getVega() const { return vega; }

double CallOption::getTheta() const { return theta; }

double CallOption::getRho() const { return rho; }