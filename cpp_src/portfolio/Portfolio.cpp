#include "cpp_src/portfolio/Portfolio.h"
#include "cpp_src/portfolio/SPXPosition.h"
#include "cpp_src/market/CallOption.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <algorithm>

using namespace std;

// Constructor
// Creates the portfolio with the specified starting capital
Portfolio::Portfolio(double startingCash) 
    : cash(startingCash), 
      spxPosition(), // Implicitly build it with dummy zeros
      initialCash(startingCash),
      previousNAV(startingCash) 
      
{
    // activeOptions starts naturally empty.
}

// Method which allows the AI to buy/sell a contract
// (Returns true if the trade was successful, false if insufficient funds)
bool Portfolio::tradeOption(const CallOption& contract, double price, int quantity)
{
    // Prevent the AI from making empty trades
    if (quantity == 0 ) return false;

    // Check purchasing power
    // If quantity is negative (selling), price * quantity * 100 is negative.
    // The 100 represents the assumption that we are trading 100 shares per conract.
    // cash >= negative is always true, which correctly allows the sale.
    if (quantity > 0 && cash < (price * quantity * 100) + (quantity * OptionPosition::FEE)) return false;

    // Check if we already are trading this call option.
    // If we are modify that option position instead of creating a new one.
    for (auto it = activeOptions.begin(); it != activeOptions.end(); ++it)
    {
        if (it->isSameContract(contract)) 
        {
            // Execute trade and update cash
            cash += it->processTrade(price, quantity);

            // Clean up: If the trade closed out the position, delete it from the array
            if (it->getNumContracts() == 0)
            {
                activeOptions.erase(it);
            }
            
            return true;
        }
    }

    // If we haven't traded this contract before, open a new position
    activeOptions.push_back(OptionPosition(contract));

    // Update the bank account
    cash += activeOptions.back().processTrade(price, quantity);

    return true;
}

// Method which forces the AI to hedge its position
// This functionality is forced to ensure that the AI trades volatility and not direction
// (Returns true if the trade was successful, false if insufficient funds)
bool Portfolio::hedgeNetDelta(double spxPrice, const vector<const CallOption*>& todaysCallOptions)
{
    // Temporary vairable to calculate the total directional exposure of the current portfolio.
    double targetHedge = 0.0;

    // Loop through each option position we own, find the corresponding contract with today's parameters
    for (const OptionPosition& optionPosition : activeOptions)
    {
        const CallOption& ownedContract = optionPosition.getContract();
        bool foundLiveDelta = false;

        // Loop through today's call options to find the contract which matches
        for (const CallOption* liveOption : todaysCallOptions)
        {
            // Check our contract against the open call option using the composite key
            // Key = ExpirationDate, Strike
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate()
                && liveOption->getStrike() == ownedContract.getStrike())    
            {
                // We pass the live delta into the getPositionDelta method
                targetHedge -= optionPosition.getPositionDelta(liveOption->getDelta());
                foundLiveDelta = true;
                break;
            }
        }

        // Fallback if the option didn't trade today
        // Use old delta as last resort
        if (!foundLiveDelta)
        {
            targetHedge -= optionPosition.getPositionDelta(ownedContract.getDelta());
        }
    }

    // Trade the difference of shares between current SPX position and the target position.
    return tradeSPX(spxPrice, (int)round(targetHedge - spxPosition.getNumShares()));
}

// Helper method which executes the trade order of the hedge net delta function
// It trades the difference calculated in that function
bool Portfolio::tradeSPX(double price, int quantity)
{
    // Prevent the AI from making empty trades
    if (quantity == 0) return false;

    // Check purchasing power
    // If quantity is negative (selling), price * quantity is negative.
    // cash >= negative is always true, which correctly allows the sale.
    if (quantity > 0 && cash < (price * quantity) + (quantity * SPXPosition::FEE)) return false;

    // Execute the trade and capture the exact cash flow 
    // (This automatically includes the 0.005 fee you wrote in SPXPosition)
    // Update the Portfolio's bank account
    cash += spxPosition.processTrade(price, quantity);;

    return true;
}

// Calculates the Sharpe Ratio
// This ratio represents how consistently the AI creates positive returns
// By using the Sharpe Ratio we encourage the AI to consistently make positive trades
// This eliminates high volatility of the portfolio and instead focuses on consistent results
double Portfolio::getRollingSharpeReward(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions)
{
    // Get the current net asset value
    double currentNAV = getNetAssetValue(currentSpxPrice, todaysCallOptions);
    
    // Calculate percentage return (with safety against absolute ruin)
    // This safety is in returning a 100% loss ratio when we went bankrupt the previous day
    double dailyReturn = 0.0;
    if (previousNAV > 0) {
        dailyReturn = (currentNAV - previousNAV) / previousNAV;
    } else {
        dailyReturn = -1.0; // Total loss
    }

    // Save NAV for tomorrow
    previousNAV = currentNAV;

    // Manage the Rolling Window
    returnHistory.push_back(dailyReturn); // Add the newest day
    if ((int)returnHistory.size() > sharpeWindowSize) {
        returnHistory.pop_front(); // Remove the oldest day
    }

    // If we don't have enough data yet, just reward the raw scaled return
    if (returnHistory.size() < 2) {
        return dailyReturn * 100.0; 
    }

    // Calculate Mean Return
    double sumReturns = 0.0;
    for (double r : returnHistory) {
        sumReturns += r;
    }
    double meanReturn = sumReturns / returnHistory.size();

    // Calculate Variance and Standard Deviation
    double varianceSum = 0.0;
    for (double r : returnHistory) {
        varianceSum += (r - meanReturn) * (r - meanReturn);
    }
    double variance = varianceSum / (returnHistory.size() - 1); // Sample variance
    double stdDev = std::sqrt(variance);

    // Calculate Annualized Sharpe Ratio
    // We add epsilon to the denominator so the C++ engine doesn't throw a division-by-zero 
    // error if the AI decides to sit in 100% cash (which has 0.0 variance).
    double sharpeRatio = (meanReturn / (stdDev + EPSILON)) * std::sqrt(252.0);

    // The guardrail: Cap the reward between -3.0 and +3.0
    // This prevents the Denominator Trap from skewing TensorBoard
    return std::clamp(sharpeRatio, -3.0, 3.0);
}

// Gets the net asset value (what the AI is worth)
// The net asset value consists of:
// - The AI's cash
// - The value of the contracts the AI owns
// - The value of the SPX shares the AI owns
// Tracking this gives us the AI's rewards
double Portfolio::getNetAssetValue(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    // Declare a variable to hold the net asset value.
    // From the beginning, initialize it with the amount of cash in the portfolio.
    double netAssetValue = cash;

    // Calculate the value of our SPX equity and add to the net asset value
    netAssetValue += spxPosition.getNumShares() * currentSpxPrice;

    // Find the value of each contract held in the portfolio
    for (const OptionPosition& optionPosition : activeOptions)
    {
        bool foundLivePrice = false;
        const CallOption& ownedContract = optionPosition.getContract();

        // Search today's call option contracts for the exact same contract
        for (const CallOption* liveOption : todaysCallOptions)
        {
            // Checks current held contract against each live option to check if they have the same composite key
            // The composite key is formed using the expiration date and the strike price
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate()
                && liveOption->getStrike() == ownedContract.getStrike())
            {
                // Get the mid price and use that to calculate contract value
                // Add to NAV (Negative quantities natively subtract from the total)
                netAssetValue += optionPosition.getNumContracts() * liveOption->calculateMidPrice() * 100.0;

                foundLivePrice = true;
                break; // Stop searching once we find the exact match
            }
        }

        // What if the option didn't trade today and is missing?
        if (!foundLivePrice)
        {
            // We use the Average Cost as a "stale price" fallback
            // This prevents the AI's net asset value from suddenly crashing to zero just 
            // because a deep out-of-the-money contract had zero volume today
            netAssetValue += optionPosition.getNumContracts() * optionPosition.getAvgCost() * 100.0;
        }
    }

    return netAssetValue;
}

// Given a call option, returns the number of contracts the AI owns/owes
int Portfolio::getPositionQuantity(const CallOption* contract) const
{
    // Safety check: Ensure the pointer isn't null
    if (contract == nullptr) return 0;

    // Loop through our current positions to determine if we own this particular option
    // And if so, how many of those options
    for (const OptionPosition& optionPosition : activeOptions)
    {
        const CallOption& ownedContract = optionPosition.getContract();

        // Compare the Composite Key
        if (ownedContract.getExpirationDate() == contract->getExpirationDate()
            && ownedContract.getStrike() == contract->getStrike())
        {
            // Return the number of contracts
            return optionPosition.getNumContracts();
        }
    }

    // If the loop was unsuccessful at finding the contract, return that we do not own it
    return 0;
}

// Market Maintenance
// This function automatically processes any contracts which expire today
// It settles them based on if they finished above the strike price
// It adds that cash to the AI's portfolio
void Portfolio::processExpirations(const string& todaysDate, double spxSettlementPrice)
{
    // We use an iterator starting at the beginning
    auto it = activeOptions.begin();

    // We use an iterator starting at the beginning
    while (it != activeOptions.end())
    {
        // Check if the contract expires today
        if (it->getContract().getExpirationDate() == todaysDate) 
        {
            // Calculate the Intrinsic Value of the Call Option
            double strikePrice = it->getContract().getStrike();
            // If the SPX finishes above the strike price, the value of the contract is the difference
            // Otherwise, it is worth nothing
            double intrinsicValue = (spxSettlementPrice > strikePrice) ? (spxSettlementPrice - strikePrice) : 0.0;

            // Calculate the cash settlement 
            // (Negative quantities naturally result in cash being deducted)
            // And update the portfolio's bank account accordingly
            cash += it->getNumContracts() * intrinsicValue * 100.0;
            
            // Erase the dead contract
            // .erase() automatically returns a valid iterator pointing to the next element
            it = activeOptions.erase(it);

        }
        else
        {
            // The contract did not expire.
            // We manually step forward to check the next one
            ++it;
        }
    }
}

// RL Episode Management
// Reset method for future episodes
// Brings back the portfolio to its intial state
void Portfolio::reset()
{
    cash = initialCash;
    previousNAV = initialCash;
    spxPosition = SPXPosition();

    // Instantly wipes the active options array for the next episode
    activeOptions.clear(); 

    // Clear the Sharpe memory for the new episode
    returnHistory.clear();
}

// Returns the total profit/loss until this point
// Calculated by getting the difference between the current NAV and the initial cash
// Purely mathematical, does not alter the portfolio state
double Portfolio::getTotalPnL(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    return getNetAssetValue(currentSpxPrice, todaysCallOptions) - initialCash;
}

// Shows whether the AI outperforms the S&P 500
// Returns how much more or less the AI makes compared to just holding the SPX
// Calculated by getting the difference between the AI's current NAV
// and the value of the SPX if the AI bought as much it could at the beginning of the simulation
double Portfolio::getRelativePnL(double currentSpxPrice, const vector<const CallOption*>& todaysCallOptions) const
{
    // The double literal is the price of the S&P 500 at the beginning of the simulation
    return getTotalPnL(currentSpxPrice, todaysCallOptions) - (initialCash / 1132.990000 * currentSpxPrice - initialCash);
}

// Getters
double Portfolio::getCash() const { return cash; }

const SPXPosition& Portfolio::getSpxPosition() const { return spxPosition; }

const vector<OptionPosition>& Portfolio::getActiveOptions() const { return activeOptions; }