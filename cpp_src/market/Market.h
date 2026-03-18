#ifndef MARKET_H
#define MARKET_H

#include "cpp_src/market/CallOption.h"
#include "cpp_src/market/InterestRate.h"
#include <deque>
#include <string>
#include <vector>

class Market
{
private:
    // Holds the close price of the S&P 500 in last 30 days of 2009. 
    inline static std::vector<double> spxBuffer2009 = {
        1109.80, 1094.90, 1091.38, 1106.24, 1105.65, 1110.63, 1091.49, 1095.63, 1108.86, 1109.24,
        1099.92, 1105.98, 1103.25, 1091.94, 1095.95, 1102.35, 1106.41, 1114.11, 1107.93, 1109.18,
        1096.08, 1102.47, 1114.05, 1118.02, 1120.59, 1126.48, 1127.78, 1126.20, 1126.42, 1115.10
    };

    // Holds all the closing prices from 2010 to 2023
    inline static std::vector<double> masterSpxPrices;

    // Deque which contains the closing price of the S&P 500 for all days in the simulation.
    std::deque<double> spxPriceHistory;

    // An array of all the call options available from 2010 to 2023
    // Originally, the program used paging to access only the data it needs for one month
    // However, this idea caused massive slowdowns in AI training
    // The data takes ~100 seconds to load, which would result in hours lost over lots of iterations
    // However, the RAM usage went from ~35MB to ~2.4GB
    inline static std::vector<CallOption> openCallOptions;

    // An array containing the indecies of the first call option for a new trading day
    // Where arr[i] = (i+1)th day of call options
    inline static std::vector<int> dailyOptionStartIndices;
    
    // A static boolean to track whether the call option data was loaded
    // Allows for the data to only have to be loaded once
    // Makes the program more efficient over large number of iterations
    inline static bool isCallOptionDataLoaded = false;
    
    // Tracks which call options have been given to the agent already
    // Synchronises the call option array with the trading days array
    // This allows for O(1) look up time for today's call options
    int callOptionIndex;

    // A static array containing interest rate data from 2010 to 2023
    inline static std::vector<InterestRate> interestRates;

    // A static boolean to track whether the interest rate data was loaded
    // Allows for the data to only have to be loaded once
    // Makes the program more efficient over large number of iterations
    inline static bool isTreasuryDataLoaded = false;

    // A static array containing the dates of each trading day from 2010 to 2023 
    inline static std::vector<std::string> tradingDays;

    // An index pointing to the date corresponding to the current trading day
    int currentDayIndex;

    // Date constants
    static constexpr int BEGIN_YEAR = 2010;
    static constexpr int BEGIN_MONTH = 1;
    static constexpr int END_YEAR = 2023;
    static constexpr int END_MONTH = 12;

public:
    // The last trading day in the data
    static constexpr std::string_view END_DATE = "2023-12-29";

    // Constructor
    // Initialises the SPX price history window with the last 30 days of 2009
    // This allows for the first month of 2010 to still be used in training the AI
    Market();

    // Returns an array of pointers pointing to the addresses of the call options for the current day
    // By returning an array of pointers instead of copies of the call options, we make the program more effictient
    std::vector<const CallOption*> getTodaysCallOptions();

    // Returns a pointer to the address of the interest rate for the current day
    // By returning a pointer instead of a copy of the interest rate data, we make the program more efficient
    const InterestRate* getTodaysInterestRate() const;

    // Returns a string containing today's date
    const std::string& getTodaysDate() const;

    // Returns the total number of trading days in the simulation
    const int getTotalTradingDays() const;

    // Returns the current closing price of the S&P 500
    double getCurrentSpxPrice();

    // Function which signals that the AI has processed today's data and put in its trades
    // The market moves to the next day and the data is updated accordingly
    void endTradingDay();

    // Resets the market back to the orginal state
    // Essentially rewinds time back to the start of 2010
    void reset(int startIndex);

    // Calculates the historical volatility of the S&P 500 given the closing prices in the last 30 days
    // This is plugged in as the volatility value into the Black-Scholes equation 
    // Instead of the implied volatility value from the files
    // This value is represents the annualized standard deviation of the natural log of daily returns of the S&P 500
    double calculateHistoricalVolatility() const;

private:
    // Function which calculates the daily logarithmic return.
    // Works by taking the natural log of the relative difference between today's price and yesterday's price.
    double calculateDailyLogReturn(int i) const;

    // Returns the average daily logarithmic return of the S&P 500 for the last 30 days.
    double calculateMeanReturn() const;

    // Return the sample variance for the last 30 days.
    /*
        The sample variance is calculated by summing the squares of the difference between the daily return for each
        day and the average return, then dividing the sum by 29 (the number of daily return values).
    */
    double calculateSampleVariance() const;

    // Updates the 30 day rolling window by adding the latest closing price of the S&P 500
    // And removing the now 31st closing price
    void updateDailyPrice();

    // Helper method to format the file path given the year and month we are looking for
    static std::string getFilePath(int year, int month);

    // Loads the call option data into RAM
    static void loadCallOptionData();
    // Loads the interest rate data into RAM
    static void loadTreasuryData();
};

#endif