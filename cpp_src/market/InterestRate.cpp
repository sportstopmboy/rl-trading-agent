#include "cpp_src/market/InterestRate.h"
#include <string>

using namespace std;

// Constructor
InterestRate::InterestRate(string date, double oneMonthRate, double twoMonthRate, double threeMonthRate,
                           double fourMonthRate, double sixMonthRate, double oneYearRate, double twoYearRate,
                           double threeYearRate, double fiveYearRate, double sevenYearRate, double tenYearRate,
                           double twentyYearRate, double thirtyYearRate)
    : date(move(date)),
      oneMonthRate(oneMonthRate / 100.0),
      twoMonthRate(twoMonthRate / 100.0),
      threeMonthRate(threeMonthRate / 100.0),
      fourMonthRate(fourMonthRate / 100.0),
      sixMonthRate(sixMonthRate / 100.0),
      oneYearRate(oneYearRate / 100.0),
      twoYearRate(twoYearRate / 100.0),
      threeYearRate(threeYearRate / 100.0),
      fiveYearRate(fiveYearRate / 100.0),
      sevenYearRate(sevenYearRate / 100.0),
      tenYearRate(tenYearRate / 100.0),
      twentyYearRate(twentyYearRate / 100.0),
      thirtyYearRate(thirtyYearRate / 100.0)
{
}

// Calculates the risk-free interest rate used in the Black-Scholes formula.
/*
    Returns the nearest neighbouring interest rate value. The nearest neighbouring
    interest rate is determined by calculating the shortest distance from the days
    to expiration of the call option to the different interest rates.The harcoded
    values represent midpoints between two interest rates posted by the treasury.
    These midpoints form the ranges each interest applies to.

    e.g. The two month rate applies when 45 <= days to expiration < 75.
*/
double InterestRate::calculateAnnualizedRate(int daysToExpiration) const
{
    if (daysToExpiration < 45)          return oneMonthRate;
    else if (daysToExpiration < 75)     return twoMonthRate;
    else if (daysToExpiration < 105)    return threeMonthRate;
    else if (daysToExpiration < 150)    return fourMonthRate;
    else if (daysToExpiration < 273)    return sixMonthRate;
    else if (daysToExpiration < 548)    return oneYearRate;
    else if (daysToExpiration < 913)    return twoYearRate;
    else if (daysToExpiration < 1460)   return threeYearRate;
    else if (daysToExpiration < 2190)   return fiveYearRate;
    else if (daysToExpiration < 3103)   return sevenYearRate;
    else if (daysToExpiration < 5475)   return tenYearRate;
    else if (daysToExpiration < 9125)   return twentyYearRate;
    else                                return thirtyYearRate;
}

// Getters
const string& InterestRate::getDate() const { return date; }