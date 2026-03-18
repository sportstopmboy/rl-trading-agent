#include "cpp_src/market/Market.h"
#include "cpp_src/market/CallOption.h"
#include "cpp_src/market/InterestRate.h"
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

// Constructor
// Initialises the SPX price history window with the last 30 days of 2009
// This allows for the first month of 2010 to still be used in training the AI
Market::Market() : spxPriceHistory{
                       1109.80, 1094.90, 1091.38, 1106.24, 1105.65, 1110.63, 1091.49, 1095.63, 1108.86, 1109.24,
                       1099.92, 1105.98, 1103.25, 1091.94, 1095.95, 1102.35, 1106.41, 1114.11, 1107.93, 1109.18,
                       1096.08, 1102.47, 1114.05, 1118.02, 1120.59, 1126.48, 1127.78, 1126.20, 1126.42, 1115.10},
                   callOptionIndex(0), currentDayIndex(0)
{
    // Checks if the interest rate data has already been loaded
    // If not, it is loaded into RAM
    if (!isTreasuryDataLoaded)
    {
        isTreasuryDataLoaded = true;
        loadTreasuryData();
    }

    // Checks if the call option data has already been loaded
    // If not, it is loaded into RAM
    if (!isCallOptionDataLoaded)
    {
        isCallOptionDataLoaded = true;
        loadCallOptionData();
    }
}

// Returns an array of pointers pointing to the addresses of the call options for the current day
// By returning an array of pointers instead of copies of the call options, we make the program more effictient
vector<const CallOption*> Market::getTodaysCallOptions()
{
    // The lightweight array of memory addresses we will hand to the AI
    vector<const CallOption*> todaysCallOptions;

    // Since the data is sorted, the program starts searching from the index of today's first call option
    // This makes the program more efficient by not having to find the day each time
    // This loop runs until the quote date of a call option changes or the end of the array is reached
    while (callOptionIndex < (int)openCallOptions.size() && openCallOptions[callOptionIndex].getQuoteDate() == tradingDays[currentDayIndex])
    {
        // Add the call option to the array
        todaysCallOptions.push_back(&openCallOptions[callOptionIndex]);

        // Increment the call option index
        callOptionIndex++;
    }

    // Return the array
    return todaysCallOptions;
}

// Returns a pointer to the address of the interest rate for the current day
// By returning a pointer instead of a copy of the interest rate data, we make the program more efficient
const InterestRate* Market::getTodaysInterestRate() const
{
    // This function uses a binary search to make the program more efficient
    // Cuts search time from O(n) to O(log(n))
    int low = 0, high = interestRates.size() - 1, mid;

    while (low <= high)
    {
        mid = low + (high - low) / 2;

        if (interestRates[mid].getDate() == tradingDays[currentDayIndex])
            return &interestRates[mid];

        else if (interestRates[mid].getDate() < tradingDays[currentDayIndex])
        {
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }
    }
    // Return nullptr if interest rate not found
    return nullptr;
}

// Returns a string containing today's date
const string& Market::getTodaysDate() const
{
    return tradingDays[currentDayIndex];
}

// Returns the current closing price of the S&P 500
double Market::getCurrentSpxPrice()
{
    return spxPriceHistory.back();
}

// Function which signals that the AI has processed today's data and put in its trades
// The market moves to the next day and the data is updated accordingly
void Market::endTradingDay()
{
    // Update the 30 day rolling window of S&P 500 closing prices
    updateDailyPrice();

    // Update the current day index
    currentDayIndex++;

    // Have we reached the end of the call option data?
    if (currentDayIndex >= (int)tradingDays.size())
    {
        // If so, reset the portfolio
        reset();
    }
}

// Resets the market back to the orginal state
// Essentially rewinds time back to the start of 2010
void Market::reset()
{
    // Reset the tracking indeces
    currentDayIndex = 0;
    callOptionIndex = 0;

    // Reset rolling window to historical values
    spxPriceHistory = {
        1109.80, 1094.90, 1091.38, 1106.24, 1105.65, 1110.63, 1091.49, 1095.63, 1108.86, 1109.24,
        1099.92, 1105.98, 1103.25, 1091.94, 1095.95, 1102.35, 1106.41, 1114.11, 1107.93, 1109.18,
        1096.08, 1102.47, 1114.05, 1118.02, 1120.59, 1126.48, 1127.78, 1126.20, 1126.42, 1115.10};
}

// Calculates the historical volatility of the S&P 500 given the closing prices in the last 30 days
// This is plugged in as the volatility value into the Black-Scholes equation 
// Instead of the implied volatility value from the files
// This value is represents the annualized standard deviation of the natural log of daily returns of the S&P 500
double Market::calculateHistoricalVolatility() const
{
    return sqrt(calculateSampleVariance() * 252);
}

// Function which calculates the daily logarithmic return.
// Works by taking the natural log of the relative difference between today's price and yesterday's price.
double Market::calculateDailyLogReturn(int i) const
{
    return log(spxPriceHistory[i] / spxPriceHistory[i - 1]);
}

// Returns the average daily logarithmic return of the S&P 500 for the last 30 days.
double Market::calculateMeanReturn() const
{
    double totalReturn = 0.0;
    for (size_t i = spxPriceHistory.size() - 1; i > 0; i--)
    {
        totalReturn += calculateDailyLogReturn(i);
    }
    return totalReturn / spxPriceHistory.size();
}

// Return the sample variance for the last 30 days.
/*
    The sample variance is calculated by summing the squares of the difference between the daily return for each
    day and the average return, then dividing the sum by 29 (the number of daily return values).
*/
double Market::calculateSampleVariance() const
{
    double meanReturn = calculateMeanReturn();

    double sampleVariance = 0.0;
    for (size_t i = spxPriceHistory.size() - 1; i > 0; i--)
    {
        double dailyReturn = calculateDailyLogReturn(i);
        sampleVariance += (dailyReturn - meanReturn) * (dailyReturn - meanReturn);
    }
    return sampleVariance / (spxPriceHistory.size() - 1);
}

// Updates the 30 day rolling window by adding the latest closing price of the S&P 500
// And removing the now 31st closing price
void Market::updateDailyPrice()
{
    // Add today's price to the deque.
    spxPriceHistory.push_back(openCallOptions[callOptionIndex - 1].getUnderlyingLast());
    // Remove the price which falls outside the rolling window after today.
    spxPriceHistory.pop_front();
}

// Helper method to format the file path given the year and month we are looking for
string Market::getFilePath(int year, int month)
{
    ostringstream path;

    // Formats: spx_eod_data/spx_eod_yyyy/spx_eod_yyyyMM.csv
    path << "resources/spx_eod_data/spx_eod_" << year << "/spx_eod_"
         << year << setfill('0') << setw(2) << month << ".csv";

    return path.str();
}

// Loads the call option data into RAM
void Market::loadCallOptionData()
{
    // Counter to count how many total rows are in the files
    int rowCount = 0;

    // For loop to loop through each month of each year
    for (size_t year = BEGIN_YEAR; year <= END_YEAR; year++)
    {
        for (size_t month = BEGIN_MONTH; month <= END_MONTH; month++)
        {
            cout << "Loading call option data for " << year << "-"
                 << setfill('0') << setw(2) << month << "...\n";

            // Open the file for reading
            ifstream file(getFilePath(year, month));
            
            // Print an error if the file does not open and exit the loading  
            if (!file.is_open())
            {
                cerr << "ERROR: Could not open " << getFilePath(year, month) << '\n';
                return;
            }

            // Safely parses non-crucial floats (Greeks)
            // This lambda function helps to minimize the amount of data loss when reading files
            // Since some rows are missing some data values, they would be rendered useless without this function
            // This function defaults values to zero for non-crucial floats
            // The non-crucial floats are listed below
            auto parseGreek = [](const string& str) -> double
            {
                if (str.empty() || str.find_first_not_of(" \t\r\n") == string::npos) return 0.0;
                return stod(str);
            };

            // Safely parses integers (Volume), defaulting to 0 for illiquid days
            // This lambda function helps to minimize the amount of data loss when reading files
            // Since some rows are missing volume values, they would be rendered useless without this function
            // This function defaults volumes to zero
            // This works on the assumption that a missing value represents an illiquid day for this option
            auto parseInt = [](const string& str) -> int
            {
                if (str.empty() || str.find_first_not_of(" \t\r\n") == string::npos) return 0;
                return stoi(str);
            };

            // String to store one line of data at a time
            string line;

            // Skip header row
            getline(file, line);

            // Read each line from the file
            while (getline(file, line))
            {
                // Skip empty lines (usually the last line of the file)
                if (line.empty()) continue;

                // Increment Row Counter
                rowCount++;

                // Temporary array to hold a line of data split into individual tokens
                vector<string> tokens;
                stringstream ss(line);
                string token;

                // Split the line into individual pieces of data, and add each piece to temporary array
                while (getline(ss, token, ','))
                {
                    // Protect the program and strip hidden carriage returns
                    if (!token.empty() && token.back() == '\r') {
                        token.pop_back(); 
                    }

                    tokens.push_back(token);
                }

                // Checks if the line contained the correct amount of columns
                if (tokens.size() == 14)
                {
                    // Checks if the quote date of this call option has appeared before
                    // If it has not, it means it's part of a new trading day
                    // And therefore it is added to the trading days array
                    if (tradingDays.empty() || tokens[0] != tradingDays.back())
                    {
                        tradingDays.push_back(tokens[0]);
                    }

                    try
                    {
                        // Attempts to add the line of data to the call option array
                        // Checks for crucial values and defualts non-crucial 
                        // This strategy increased the percentage of usable data significantly
                        // It took the the percentage of usable data to 99.92%
                        openCallOptions.push_back(CallOption(
                            tokens[0],        // Quote Date (Crucial)
                            tokens[2],        // Expiration Date (Crucial)
                            stod(tokens[1]),  // Underlying Last (Crucial)
                            stod(tokens[13]), // Strike Price (Crucial)
                            stoi(tokens[3]),  // Days To Expiration (Crucial)
                            stod(tokens[11]), // Bid (Crucial)
                            stod(tokens[12]), // Ask (Crucial)

                            // --- NON-CRUCIAL INTEGER ---
                            parseInt(tokens[10]), // Volume (Defaults to 0)

                            // --- NON-CRUCIAL GREEK ---
                            parseGreek(tokens[9]), // Implied Volatility (Defaults to 0.0)

                            // --- THE NEW CRUCIAL GREEKS ---
                            stod(tokens[4]), // Delta (Crucial: Throws error if empty)
                            stod(tokens[5]), // Gamma (Crucial: Throws error if empty)

                            // --- THE NON-CRUCIAL GREEKS ---
                            parseGreek(tokens[6]), // Vega (Defaults to 0.0)
                            parseGreek(tokens[7]), // Theta (Defaults to 0.0)
                            parseGreek(tokens[8])  // Rho (Defaults to 0.0)
                            ));
                    }
                    catch (const invalid_argument& e)
                    {
                        // cerr << "WARNING: Skipping corrupted row due to parse error: " << line << '\n';
                    }
                }
                else
                {
                    cerr << "WARNING: Skipping row with incorrect column count (" << tokens.size() << "): " << line << '\n';
                }
            }
            cout << "Successfully loaded call option data. " << openCallOptions.size() << "/"
                 << rowCount << " contracts loaded.\n\n";
        }
    }
}

// Loads the interest rate data into RAM
void Market::loadTreasuryData()
{
    cout << "Loading 14-year Treasury history into static memory...\n";

    // Open the file for reading
    ifstream file("resources/par_yield_curve_rates/treasury_rates.csv");

    // Print an error if the file does not open and exit the loading
    if (!file.is_open())
    {
        cerr << "ERROR: Could not open treasury_rates.csv\n";
        return;
    }

    // String to store one line of data at a time
    string line;

    // Skip the header row
    getline(file, line);

    // Read each line from the file
    while (getline(file, line))
    {
        // Skip empty lines (usually the last line of the file)
        if (line.empty()) continue;

        // Temporary array to hold a line of data split into individual tokens
        vector<string> tokens;
        stringstream ss(line);
        string token;

        // Split the line into individual pieces of data, and add each piece to temporary array
        while (getline(ss, token, ','))
        {
            tokens.push_back(token);
        }

        // Checks if the line contained the correct amount of columns
        if (tokens.size() == 14)
        {
            try
            {
                // Attempts to add the line of data to the interest rate array
                interestRates.push_back(InterestRate(
                    tokens[0],        // Date
                    stod(tokens[1]),  // 1 Mo
                    stod(tokens[2]),  // 2 Mo
                    stod(tokens[3]),  // 3 Mo
                    stod(tokens[4]),  // 4 Mo
                    stod(tokens[5]),  // 6 Mo
                    stod(tokens[6]),  // 1 Yr
                    stod(tokens[7]),  // 2 Yr
                    stod(tokens[8]),  // 3 Yr
                    stod(tokens[9]),  // 5 Yr
                    stod(tokens[10]), // 7 Yr
                    stod(tokens[11]), // 10 Yr
                    stod(tokens[12]), // 20 Yr
                    stod(tokens[13])  // 30 Yr
                    ));
            }
            catch (const invalid_argument& e)
            {
                cerr << "WARNING: Skipping corrupted row due to parse error: " << line << '\n';
            }
        }
        else
        {
            cerr << "WARNING: Skipping row with incorrect column count (" << tokens.size() << "): " << line << '\n';
        }
    }
    cout << "Successfully loaded interest rates.\n\n";
}