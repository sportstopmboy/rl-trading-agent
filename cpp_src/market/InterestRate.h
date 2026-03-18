#ifndef INTEREST_RATE_H
#define INTEREST_RATE_H

#include <string>

class InterestRate
{
private:
    // The date to which the interest rates apply to.
    std::string date;

    // The rates stored as decimals for the relevant time periods.
    double oneMonthRate;
    double twoMonthRate;
    double threeMonthRate;
    double fourMonthRate;
    double sixMonthRate;
    double oneYearRate;
    double twoYearRate;
    double threeYearRate;
    double fiveYearRate;
    double sevenYearRate;
    double tenYearRate;
    double twentyYearRate;
    double thirtyYearRate;

public:
    // Constructor
    InterestRate(std::string date, double oneMonthRate, double twoMonthRate, double threeMonthRate,
                 double fourMonthRate, double sixMonthRate, double oneYearRate, double twoYearRate,
                 double threeYearRate, double fiveYearRate, double sevenYearRate, double tenYearRate,
                 double twentyYearRate, double thirtyYearRate);

    // Calculates the risk-free interest rate used in the Black-Scholes formula.
    double calculateAnnualizedRate(int daysToExpiration) const;

    // Getters
    const std::string& getDate() const;
};

#endif