#include "cpp_src/environment/Environment.h"
#include "cpp_src/market/Market.h"
#include <vector>
#include <limits>
#include <iostream>
#include <random>

using namespace std;

// The Environment constructor passes the cash directly to the Portfolio
Environment::Environment(double initialCash)
    : market(), portfolio(initialCash), isDone(false)
{
}

// The master array holding exactly 99 pointers for today's grid
// This is a 9x11 grid which allocates call options to specific qualities
// The two qualities are DTE (x-axis) and Moneyness (y-axis)
// Moneyness is defined as the strike price divided by the SPX price
// It searches for call options which are most similar to these two qualities
// In essence, if we imagine a cartesian plane, this grid represents the points
// which are closest to points we predefine
vector<double> Environment::getObservation(const vector<const CallOption*>& todaysCallOptions,
                                           double currentSpxPrice,
                                           double historicalVolatility,
                                           const InterestRate* todaysRates)
{
    // Pre-allocate exactly 796 slots too prevent dynamic memory reallocation penalties
    vector<double> state;
    state.reserve(796); // 9 * 11 * 8 + 4

    // Define the fixed grid coordinates
    const vector<int> targetDTEs{3, 7, 10, 14, 21, 30, 45, 60, 90};
    const vector<double> targetMoneyness{0.95, 0.96, 0.97, 0.98, 0.99, 1.00, 1.01, 1.02, 1.03, 1.04, 1.05};

    // Populate the 99 (9 x 11) grid slots
    for (const CallOption* contract : todaysGrid)
    {
        if (contract != nullptr)
        {
            // Get the 8 features
            double exactMoneyness = contract->getStrike() / currentSpxPrice;
            double exactDTEInYears = contract->getDaysToExpiration() / 365.0;
            double midPrice = contract->calculateMidPrice();
            
            double riskFreeRate = todaysRates ? todaysRates->calculateAnnualizedRate(contract->getDaysToExpiration()) : 0.0;
            double theoreticalPrice = contract->calculateTheoreticalPrice(riskFreeRate, historicalVolatility);
            
            double edgeRatio = (theoreticalPrice > 0.0001) ? (midPrice / theoreticalPrice) : 1.0;
            double spreadPct = (midPrice > 0.0) ? ((contract->getAsk() - contract->getBid()) / midPrice) : 0.0;

            // Push the 8 features
            state.push_back(exactMoneyness);
            state.push_back(exactDTEInYears);
            state.push_back(edgeRatio);
            state.push_back(contract->getDelta());
            state.push_back(contract->getGamma());
            state.push_back(contract->getTheta());
            state.push_back(contract->getRho());
            state.push_back(spreadPct);
        }
        else
        {
            // Failsafe for corrupted data days (although this should not happen)
            for (int i = 0; i < 8; i++) state.push_back(0.0);
        }
    }

    // Calculate and append the 4 global features
    double currentNAV = portfolio.getNetAssetValue(currentSpxPrice, todaysCallOptions);

    // Global 1: 30-day histoical volatility
    state.push_back(historicalVolatility);

    // Global 2: The baseline 3-month risk free rate
    // Assume 0.0 if the rate can't be found
    double baselineRate = todaysRates ? todaysRates->calculateAnnualizedRate(90) : 0.0;
    state.push_back(baselineRate);
    
    // Global 3: Portfolio Net Delta
    // (Normalized by NAV so the AI understands risk relative to its size)
    double netDelta = 0.0;

    for (const OptionPosition& optionPosition : portfolio.getActiveOptions())
    {
        const CallOption& ownedContract = optionPosition.getContract();

        // Try to find the live delta for accuracy
        // But automatically initialize to delta when we bought the contract
        // This is the fall back value if we cannot find live delta
        double liveDelta = ownedContract.getDelta();

        for (const CallOption* liveOption : todaysCallOptions)
        {
            if (liveOption->getExpirationDate() == ownedContract.getExpirationDate()
                && liveOption->getStrike() == ownedContract.getStrike())
            {
                liveDelta = liveOption->getDelta();
                break;
            }
        }

        netDelta += optionPosition.getPositionDelta(liveDelta);
    }

    // Note: Net Delta includes the SPX shares hedging it. 
    // We add the SPX share quantity to the options delta to get the true directional exposure.
    double trueNetDelta = netDelta + portfolio.getSpxPosition().getNumShares();
    // Protect against dividing by zero
    // This however should not happen as that would mean the AI is bankrupt
    state.push_back(currentNAV > 0 ? (trueNetDelta / currentNAV) : 0.0);

    // Global 4: Available Buying Power %
    // Protect against dividing by zero
    // This however should not happen as that would mean the AI is bankrupt
    state.push_back(currentNAV > 0 ? (portfolio.getCash() / currentNAV) : 0.0);

    return state;
}

// Gets the normalized state array to the AI
// The state array contains the 99 call options from today's grid
// As well as 8 state features for each call option
// These 8 state features are:
// 1) Exact moneyness
// 2) Exact DTE
// 3) Edge ratio (Black-Scholes Theoretical Price : Actual Mid Price)
// 4) Delta
// 5) Gamma
// 6) Theta
// 7) Rho
// 8) Spread Percentage
// The array also contains 4 global features:
// 1) 30-day histoical volatility
// 2) The baseline 3-month risk free rate
// 3) Portfolio Net Delta
// 4) Available Buying Power %
void Environment::executeAgentAction(const vector<double>& actionWeights,
                                     double currentSpxPrice)
{
    // Protect against inconsistent action weights array size errors
    if (actionWeights.size() != todaysGrid.size()) {
        cerr << "FATAL ERROR: AI passed " << actionWeights.size() 
             << " actions, but grid requires " << todaysGrid.size() << ".\n";
        return; 
    }

    // Determine the maximum buying power per slot
    double maxCapitalPerSlot = portfolio.getNetAssetValue(currentSpxPrice, market.getTodaysCallOptions());

    // Iterate through the AI's decisions
    for (size_t i = 0; i < todaysGrid.size(); i++)
    {
        const CallOption* contract = todaysGrid[i];

        // If the grid slot is empty, or the AI's signal is effectively zero, skip
        if (contract == nullptr || std::abs(actionWeights[i]) < 0.01) continue;

        // Calculate the physical dollar amount the AI wants to park in this contract
        // actionWeights[i] refers to the AI's signal (Squeezed between -1.0 for max short, and 1.0 for max long)
        // This value it multiplied by 5% to indicate that 5% of cash can be allocated to one call option at a time
        double targetCapital = actionWeights[i] * maxCapitalPerSlot * 0.05;

        // Calculate the physical cost of one contract
        double contractCost = contract->calculateMidPrice() * 100;

        // Protect against corrupted rows
        if (contractCost <= 0) continue;

        // Calculate the target quantity of contracts (truncated to a whole integer)
        int targetQuantity = (int)(targetCapital / contractCost);

        // Find out how many of this exact contract we currently own
        int currentQuantity = portfolio.getPositionQuantity(contract);

        // Calculate the difference between the number of contracts
        // Tells us what trade we actually need to make
        int tradeQuantity = targetQuantity - currentQuantity;

        // Execution and Action clamping safety net
        if (tradeQuantity != 0)
        {
            // The portfolio attemps the physical math
            // If the trade violates margin or cash limits, the portfolio rejects it and returns false
            portfolio.tradeOption(*contract, contract->calculateMidPrice(), tradeQuantity);

            // If the tradeOption() function returns false, the program does not crash.
            // All it means is that the AI attempted a trade it could not afford.
            // The trade is simply ignored (clamped).
        }
    }
}

// Helper function to find specific contracts that perfectly match chosen DTEs and Strikes
// This uses a nearest neighbor search to populate the grid each day
void Environment::updateDailyGrid(double currentSpxPrice, const std::vector<const CallOption*>& todaysCallOptions)
{
    // Wipe yesterday's grid, but keep the 99 slots of memory reserved for maximum efficiency
    todaysGrid.clear();
    todaysGrid.reserve(99); 

    // Define the x coordinates (DTE)
    const std::vector<int> targetDTEs = {3, 7, 10, 14, 21, 30, 45, 60, 90};
    // Define the y coordinates (moneyness)
    const std::vector<double> targetMoneyness = {0.95, 0.96, 0.97, 0.98, 0.99, 1.00, 1.01, 1.02, 1.03, 1.04, 1.05};

    // Loop through the 99 coordinates and perform a nearest neighbor search on each one
    for (int dte : targetDTEs)
    {
        for (double targetMoney : targetMoneyness)
        {
            // Temporary variables to hold the best match and closest distance
            const CallOption* bestMatch = nullptr;
            double minScore = std::numeric_limits<double>::max();

            // The nearest neighbor search
            for (const CallOption* option : todaysCallOptions)
            {
                // Get the DTE difference and moneyness difference
                double dteDiff = std::abs(option->getDaysToExpiration() - dte);
                double moneyDiff = std::abs(option->getStrike() / currentSpxPrice - targetMoney);

                // We multiply the absolute value of the DTE difference by a massive weight
                // This forces the algorithm to match the expiration date as closely as possible
                // before it cares about the strike price
                double score = (dteDiff * 1000.0) + moneyDiff;

                // Check if this is the nearest neighbor
                if (score < minScore)
                {
                    minScore = score;
                    bestMatch = option;
                }
            }
            // Save the exact memory address to the master class array
            todaysGrid.push_back(bestMatch);
        }
    }
}

// Rewinds the tape to 2010, clears the portfolio, and returns the first state
vector<double> Environment::reset()
{
    // Reset the portfolio
    portfolio.reset();

    // Define the Safe Zone
    // We subtract 252 to ensure the AI has at least one year of trades available
    int maxSpawnIndex = market.getTotalTradingDays() - 252;

    // Generate the random starting index using a random number generator
    random_device rd;
    mt19937 gen(rd());    
    uniform_int_distribution<> distrib(0, maxSpawnIndex);

    int randomStartIndex = distrib(gen);

    // Teleport the market
    market.reset(randomStartIndex);

    // Reset episode end flag
    isDone = false;

    // Fetch Day 0 data exactly once
    currentCallOptions = market.getTodaysCallOptions();

    // Build the Fixed Grid so executeAgentAction has targets
    updateDailyGrid(market.getCurrentSpxPrice(), currentCallOptions);

    // Hand the AI its first look at the new world
    return getObservation(currentCallOptions,
                          market.getCurrentSpxPrice(),
                          market.calculateHistoricalVolatility(),
                          market.getTodaysInterestRate());
}

// Executes the AI's trades, hedges, advances time, and calculates the reward
StepResult Environment::step(const vector<double>& agentActions)
{
    // Get today's data market data before time moves
    string todaysDate = market.getTodaysDate();
    double currentSpxPrice = market.getCurrentSpxPrice();

    // Execute: Translates agent action array into actual trades
    // We use the current call options and grid (generated by reset or the previous step)
    executeAgentAction(agentActions, currentSpxPrice);

    // Hedge: Force the delta hedge (hardcoded risk management)
    portfolio.hedgeNetDelta(currentSpxPrice, currentCallOptions);

    // Time Step: Advance Time
    market.endTradingDay();

    // Gather the new day's data
    string newDate = market.getTodaysDate();
    double newSpxPrice = market.getCurrentSpxPrice();

    // Fetch: Gather the new day's data exactly once
    currentCallOptions = market.getTodaysCallOptions();

    // Update: Rebuild the fixed grid so the AI's "eyes" and "hands" map to the new day
    updateDailyGrid(newSpxPrice, currentCallOptions);

    // Settle: Settle any options that expire this day
    portfolio.processExpirations(newDate, newSpxPrice);

    double currentNAV = portfolio.getNetAssetValue(newSpxPrice, currentCallOptions);

    // The Circuit Breaker & Asymmetric Death Penalty
    // We check for ruin before calculating any statistical returns.
    if (currentNAV <= 0)
    {
        isDone = true;

        // In finance, Sharpe ratios rarely drop below -3.0 or exceed +3.0.
        // A flat -10.0 acts as a massive mathematical wall, forcing the 
        // gradient strictly away from actions that cause early bankruptcy.
        double deathPenalty = -10.0; 

        StepResult result;
        result.stateFeatures = getObservation(currentCallOptions, newSpxPrice, 
                                              market.calculateHistoricalVolatility(), 
                                              market.getTodaysInterestRate());
        result.reward = deathPenalty;
        result.isDone = true;
        result.currentNAV = currentNAV;
        return result; // Exit immediately
    }

    // The Risk-Adjusted Reward
    // If we survived, calculate the rolling Sharpe Ratio
    double reward = portfolio.getRollingSharpeReward(newSpxPrice, currentCallOptions);

    // Normal Termination (Data exhaustion)
    if (newDate == Market::END_DATE)
    {
        isDone = true;
    }

    // Build and return the final step result struct
    StepResult result;
    result.stateFeatures = getObservation(currentCallOptions, newSpxPrice, 
                                          market.calculateHistoricalVolatility(), 
                                          market.getTodaysInterestRate());
    result.reward = reward;
    result.isDone = isDone;
    result.currentNAV = currentNAV;

    return result;
}

// Getters for episode analysis
bool Environment::getIsDone() { return isDone; }